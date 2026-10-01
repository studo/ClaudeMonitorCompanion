// bell.h for the desktop: the buzzer and the bell, through the speakers.
//
// The board has two noise makers — the onboard piezo and a servo-driven desk
// bell — and the user picks which one sounds for each cue on the settings
// page. Both are synthesised here from the same settings, so a tone, gap or
// repeat count changed on the page changes the desktop's sound the same way.
//
// The piezo is reproduced as what it is: a square wave from an LEDC channel
// whose duty cycle is the "Volume" setting. Generating the actual PWM shape
// means the volume curve comes out right for free — a square wave's
// fundamental is loudest at 50% duty, which is why the board's hint says
// "loudest near 128".
//
// The desktop also has a job the board does not: drawing attention on a desk
// with other windows on it. play() is where the firmware says something
// deserves attention, so that is where the window is told.

#include "bell.h"

#include <SDL3/SDL.h>
#include <math.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "config.h"
#include "desktop.h"
#include "mood.h"
#include "tuning.h"

namespace {

const int kRate = 48000;

// Sounds are rendered whole and handed to SDL, which mixes the two voices.
struct Job {
  int voice;  // 0 piezo, 1 bell
  std::vector<float> pcm;
};

std::mutex s_mutex;
std::condition_variable s_cv;
std::vector<Job> s_jobs;
bool s_available = false;

// The audio device is opened only while something is playing. Holding it open
// would keep a silent stream running forever, which Plasma shows as a speaker
// icon on the taskbar entry and which keeps the sound card awake.
const uint32_t kCloseAfterMs = 1500;

void audio_worker() {
  SDL_AudioSpec spec = {SDL_AUDIO_F32, 1, kRate};
  SDL_AudioDeviceID dev = 0;
  SDL_AudioStream *voices[2] = {nullptr, nullptr};
  uint64_t idle_since = 0;

  for (;;) {
    std::vector<Job> jobs;
    {
      std::unique_lock<std::mutex> lock(s_mutex);
      if (dev) {
        s_cv.wait_for(lock, std::chrono::milliseconds(100),
                      [] { return !s_jobs.empty(); });
      } else {
        s_cv.wait(lock, [] { return !s_jobs.empty(); });
      }
      jobs.swap(s_jobs);
    }

    if (!jobs.empty() && !dev) {
      dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
      if (!dev) {
        SDL_Log("sound : cannot open audio device: %s", SDL_GetError());
        continue;
      }
      for (SDL_AudioStream *&v : voices) v = SDL_CreateAudioStream(&spec, &spec);
      SDL_BindAudioStreams(dev, voices, 2);
      SDL_ResumeAudioDevice(dev);
    }

    // A new sound on a voice replaces whatever it was playing, as on the
    // board, where a new buzz restarts the sequence.
    for (Job &j : jobs) {
      SDL_ClearAudioStream(voices[j.voice]);
      SDL_PutAudioStreamData(voices[j.voice], j.pcm.data(),
                             (int)(j.pcm.size() * sizeof(float)));
    }

    if (!dev) continue;
    const bool busy = SDL_GetAudioStreamQueued(voices[0]) > 0 ||
                      SDL_GetAudioStreamQueued(voices[1]) > 0;
    const uint64_t now = SDL_GetTicks();
    if (busy || !jobs.empty()) {
      idle_since = now;
    } else if (now - idle_since > kCloseAfterMs) {
      for (SDL_AudioStream *&v : voices) {
        SDL_DestroyAudioStream(v);
        v = nullptr;
      }
      SDL_CloseAudioDevice(dev);
      dev = 0;
    }
  }
}

void submit(int voice, std::vector<float> pcm) {
  if (!s_available || desktop::options().mute || pcm.empty()) return;
  std::lock_guard<std::mutex> lock(s_mutex);
  s_jobs.push_back(Job{voice, std::move(pcm)});
  s_cv.notify_one();
}

// ---------------------------------------------------------------- piezo --

const float kPiezoGain = 0.22f;
const int kOversample = 8;  // tames the aliasing a raw square wave produces

void append_silence(std::vector<float> &out, int ms) {
  out.insert(out.end(), (size_t)(kRate * ms / 1000), 0.0f);
}

void append_tone(std::vector<float> &out, int hz, int ms, int duty255) {
  const int n = kRate * ms / 1000;
  const float duty = duty255 / 255.0f;
  // A 2 ms ramp at each end. The real piezo clicks a little too, but a hard
  // edge through laptop speakers clicks a lot more.
  const int ramp = kRate * 2 / 1000;
  double phase = 0.0;
  const double step = (double)hz / kRate / kOversample;
  for (int i = 0; i < n; i++) {
    float acc = 0.0f;
    for (int k = 0; k < kOversample; k++) {
      // PWM with the DC removed, so 0% and 100% duty are both silence.
      acc += ((phase < duty) ? 1.0f : 0.0f) - duty;
      phase += step;
      if (phase >= 1.0) phase -= 1.0;
    }
    float env = 1.0f;
    if (i < ramp) env = (float)i / ramp;
    if (n - i < ramp) env = (float)(n - i) / ramp;
    out.push_back(kPiezoGain * env * acc / kOversample);
  }
}

// The firmware's celebration sequence, step for step: tone, gap, tone, and the
// whole thing repeated with a wider gap between repeats.
std::vector<float> render_celebration() {
  std::vector<float> out;
  const int repeats = config::v::piezo_repeats();
  for (int r = 0; r < repeats; r++) {
    append_tone(out, config::v::piezo_hz1(), PIEZO_BEEP1_MS, config::v::piezo_duty());
    append_silence(out, config::v::piezo_gap_ms());
    append_tone(out, config::v::piezo_hz2(), PIEZO_BEEP2_MS, config::v::piezo_duty());
    if (r + 1 < repeats) append_silence(out, config::v::piezo_repeat_gap_ms());
  }
  return out;
}

std::vector<float> render_long_buzz() {
  std::vector<float> out;
  append_tone(out, PIEZO_LONG_HZ, config::v::piezo_long_ms(), config::v::piezo_duty());
  return out;
}

// ----------------------------------------------------------------- bell --

// A small desk bell: a few inharmonic partials, each decaying at its own rate,
// struck once per ring. The board's bell is a cam that winds up and releases;
// in timed mode it gets roughly one release per 400 ms of spin.
std::vector<float> render_bell() {
  const int rings = config::v::bell_mode() == 1
                        ? config::v::bell_rings()
                        : std::max(1, (config::v::bell_spin_ms() + 399) / 400);
  const int interval = kRate * 280 / 1000;
  const int tail = kRate * 1800 / 1000;
  std::vector<float> out((size_t)(interval * (rings - 1) + tail), 0.0f);

  struct Partial { float ratio, amp, decay_s; };
  const Partial kPartials[] = {
      {1.00f, 1.00f, 0.90f}, {2.42f, 0.45f, 0.50f},
      {4.13f, 0.25f, 0.25f}, {6.30f, 0.12f, 0.12f},
  };
  const float f0 = 2350.0f;
  const float gain = 0.16f;

  for (int r = 0; r < rings; r++) {
    const int start = r * interval;
    for (int i = 0; start + i < (int)out.size(); i++) {
      const float t = (float)i / kRate;
      float v = 0.0f;
      for (const Partial &p : kPartials) {
        v += p.amp * expf(-t / p.decay_s) * sinf(2.0f * (float)M_PI * f0 * p.ratio * t);
      }
      out[(size_t)(start + i)] += gain * v;
    }
  }
  for (float &s : out) s = std::max(-1.0f, std::min(1.0f, s));
  return out;
}

}  // namespace

void desktop::sound_begin() {
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    SDL_Log("sound : no audio (%s); running silent", SDL_GetError());
    return;
  }
  s_available = true;
  std::thread(audio_worker).detach();
}

namespace bell {

void begin() {}
void tick() {}
void reconfigure() {}

// There is always a bell on the desktop, whatever GPIO the settings name.
bool wired() { return true; }

void strike() { submit(1, render_bell()); }

void celebrate() {
  if (config::v::piezo_repeats() < 1) return;
  submit(0, render_celebration());
}

void long_buzz() { submit(0, render_long_buzz()); }

void play(mood::Cue cue) {
  int choice = 0;
  switch (cue) {
    case mood::Cue::Alert:
      // The firmware raises the same cue for three things. Only two of them
      // are Claude asking for something; the third is the mascot being poked
      // too often, which the person doing the poking already knows about.
      if (mood::current() == mood::Mood::Limit) {
        desktop::request_attention(desktop::Attention::Limit);
      } else if (mood::current() == mood::Mood::Excited) {
        desktop::request_attention(desktop::Attention::NeedsInput);
      }
      choice = config::v::alert_sound();
      break;
    case mood::Cue::Error:
      desktop::request_attention(desktop::Attention::Error);
      choice = config::v::error_sound();
      break;
    case mood::Cue::Done:
      desktop::request_attention(desktop::Attention::Done);
      choice = config::v::done_sound();
      break;
    default:
      return;
  }
  // 0 none, 1 bell, 2 buzzer, 3 both — same encoding as the board.
  if (choice & 1) strike();
  if (choice & 2) {
    if (cue == mood::Cue::Done) celebrate();
    else long_buzz();
  }
}

}  // namespace bell

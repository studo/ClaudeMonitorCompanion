# Claude Monitor Companion

![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-yellow.svg)
![Status](https://img.shields.io/badge/Status-Working_V1.1-orange)
![Platform](https://img.shields.io/badge/Platform-ESP32--S3-blue)
![Language-C++](https://img.shields.io/badge/Language-C++-blue)
![Language-Python](https://img.shields.io/badge/Language-Python-blue)
![Version](https://img.shields.io/badge/Version-V1.1-purple)

A desk mascot that shows what Claude Code is doing.

<p align="center">
  <img src="images/hero.jpg" width="70%" alt="The mascot hooked over the top of a monitor, eyes lit">
</p>

A Waveshare ESP32-S3-Touch-LCD-1.69 sits in a 3D-printed shell shaped like the Claude
mascot — the chunky pixel-art crab. The LCD is set into the middle as its face: a flat
orange field with two black bar eyes. The eyes doze when nothing is happening, get bored
when you wander off, focus while Claude works, and snap into a `><` grin the moment Claude
needs you. Hit the five-hour usage limit and the whole screen goes red with a blinking
exclamation mark. A mechanical bell rings for anything that wants your attention.

Claude Code hooks POST bare event names at the board over your LAN. The board decides how it
feels about them.

<p align="center">
  <img src="images/concept.png" width="85%" alt="Concept sketch of the mascot and the bell striker mechanism">
</p>

## What it does

### Eight moods

Driven partly by events from your computer, partly by time the board tracks itself.

| Mood | When |
|---|---|
| `SLEEPING` | nothing for ~30 minutes |
| `CHILL` | recent activity, nothing running |
| `BORED` | no new prompt for ~5 minutes |
| `WORKING` | between your prompt and Claude finishing |
| `EXCITED` | Claude needs you, or a turn just finished |
| `CONFUSED` | something failed |
| `ANGRY` | you poked it five times in four seconds |
| `LIMIT` | usage limit reached — red field, blinking `!` |

| | | | |
|:---:|:---:|:---:|:---:|
| <img src="images/face-default.jpg" width="190"> | <img src="images/face-sleeping.jpg" width="190"> | <img src="images/face-bored.jpg" width="190"> | <img src="images/face-excited.jpg" width="190"> |
| **Chill** — resting | **Sleeping** — lids shut, breathing | **Bored** — lids low, gaze adrift | **Excited** — the `><` grin |
| <img src="images/face-needs-input.jpg" width="190"> | <img src="images/face-limit.jpg" width="190"> | <img src="images/face-custom.jpg" width="190"> | <img src="images/mascot-on-monitor.jpg" width="190"> |
| **Needs input** — red until you answer | **Limit reached** — blinking `!` | **A face you drew yourself** | Perched on the monitor |

Two of those moods — `BORED` and `SLEEPING` — are the *absence* of events. No hook can fire
to say "nothing has happened for five minutes", so the board keeps its own clock. A useful
side effect: the face keeps behaving correctly when the computer sleeps, the Wi-Fi drops, or
Claude Code is closed.

### Five faces, plus four of your own

Taken from the printed mascot's variants: bare eyes, a `><` grin, round spectacles with a
grill mouth, chunky pixel glasses with a smile, and deal-with-it shades with a moustache.
Swipe sideways to cycle them.

| | | |
|:---:|:---:|:---:|
| <img src="images/face-spectacles.jpg" width="250"> | <img src="images/face-shades.jpg" width="250"> | <img src="images/face-glasses.jpg" width="250"> |
| Round spectacles, grill mouth | Shades and a moustache | Pixel glasses and a smile |

The settings page has a pixel editor. Draw a face on a 24×28 grid, give it a second frame if
you want it to blink, and it joins the rotation — stored in flash, so it survives a reboot.
Drawn faces get the same mood vocabulary as the built-in ones by *translating* within the
frame: the working sweep, the bored droop and the excited bounce all still work.

### Four screens

Swipe up or down for the mascot, a ring gauge showing how much of your five-hour limit is
used and how long until it resets, the same for the seven-day all-models limit, and a plain
stats screen.

### Sound

An optional mechanical bell striker rings for anything that wants your attention, and the
onboard piezo buzzes twice when a turn finishes. Which of bell, buzzer or both fires for each
cue is configurable.

### A settings page, served by the board

Forty-six settings — sound, face geometry, mood thresholds, brightness, Wi-Fi — all editable
from a phone. Flash once and you never open the firmware again. Get the Wi-Fi wrong and the
board hosts its own access point so you can fix it.

## The hardware

<p align="center">
  <img src="images/bell-front.jpg" width="46%" alt="The bell striker assembly">
  <img src="images/bell-side.jpg" width="46%" alt="Side view of the servo, cam and bell">
</p>

The bell is a continuous-rotation servo turning a cam against a desk bell's striker. The cam
gradually builds tension and releases it all at once. A microswitch counts the rings, so the
firmware can stop after a set number of strikes rather than guessing at a duration.

<p align="center">
  <img src="images/bell-wiring.jpg" width="46%" alt="Perfboard wiring for the servo and switch">
  <img src="images/assembly.jpg" width="46%" alt="Shell and bell mechanism assembled together">
</p>

| Signal | Pin |
|---|---|
| Servo | GPIO 18 — 50 Hz PWM, 2500 µs runs, 1500 µs stops |
| Microswitch | GPIO 17 — COM to 3V3, NO to the pin, 10 kΩ pulldown |

Both are configurable at runtime; setting either to `-1` disables that half. The bell is
entirely optional — everything else works without it.

Full wiring diagram: **[Hardware/Schematics.png](Hardware/Schematics.png)**. Pin map, measured
performance figures and the traps worth knowing: **[Code/docs/HARDWARE.md](Code/docs/HARDWARE.md)**.

### Bill of materials

| # | Product name | Manufacturer | Qty | Farnell | Newark |
|---|---|---|---|---|---|
| 1 | Continuous servo | Adafruit | 1 | 2816371 | 85W1247 |
| 2 | Microswitch | Multicomp Pro | 1 | 3553972 | 84AH0093 |

Everything else, no particular source needed:

| # | Name | Description |
|---|---|---|
| 1 | Waveshare ESP32-S3 Touchscreen 1.69 | https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.69 |
| 2 | Mechanical bell | |
| 3 | 3D printing filament | |
| 4 | M3 and M2 screws | |
| 5 | Wires and perfboard | |

## The 3D-printed shell

<p align="center">
  <img src="images/shell-bare.jpg" width="46%" alt="The bare orange shell">
  <img src="images/shell-standing.jpg" width="46%" alt="The shell standing up">
</p>

Print files are in [`CAD/`](CAD) as both 3MF and STEP: the front and back shells, the monitor
hook, the bell plate, the bell cam and a USB-C plate. Orange for the body, black for the bell
frame.

## How it talks to Claude Code

Claude Code has a feature called **hooks**: entries in its settings file that run a command
when something happens. The command here is literally `curl`.

```json
{
  "type": "command",
  "command": "curl",
  "args": ["-s", "-m", "2", "-X", "POST",
           "-H", "Authorization: Bearer <token>",
           "http://<board-ip>/e/turn_finished"],
  "async": true
}
```

That is the entire mechanism. There are twelve of these and they differ only in the last word
of the URL. Because the event name rides in the URL path there is no body to build, quote or
escape — so no script is needed, and the same config works byte-for-byte on macOS, Windows
and Linux.

**[HOW-IT-WORKS.md](HOW-IT-WORKS.md)** explains the whole thing in a page.

## Platforms

Hooks work everywhere Claude Code runs, including the desktop app: moods, faces, sounds and
the usage-limit alert all come from them.

The two ring gauges need the **CLI**, and specifically the session you are working in. They
are fed by the status line, which the desktop app never runs, and whose figures only refresh
when a session makes API calls — so an idle CLI window parked beside the app reports a frozen
number rather than a live one. The board notices and labels it.

Hooks are exec form and identical on macOS, Linux and Windows. The status line needs a
per-platform script, and the generator emits the right one — Python on Unix, PowerShell on
Windows, where it needs nothing that Windows does not already ship.

### No board? Run it on Linux

The same firmware also builds as a Linux desktop app: the mascot in a window, driven by the
same hooks, with the mouse standing in for your finger. It runs as a user service, highlights
its taskbar entry and posts a notification when Claude needs you, and can run alongside a
board — the hook generator takes a list of targets, so a board, the desktop or both is a
configuration choice. Setup: [Code/README.md → Linux desktop
companion](Code/README.md#linux-desktop-companion).

## Repository layout

| | |
|---|---|
| [`Code/`](Code) | Firmware. PlatformIO + Arduino, ESP32-S3. Start at [Code/README.md](Code/README.md) |
| [`Code/desktop/`](Code/desktop) | The same firmware as a Linux desktop app (CMake + SDL3) |
| [`CAD/`](CAD) | Printable parts, 3MF and STEP |
| [`Hardware/`](Hardware) | Wiring schematic |
| [`images/`](images) | Build and result photos |
| [`HOW-IT-WORKS.md`](HOW-IT-WORKS.md) | How the board and Claude Code actually talk, in a page |

## Quick start

```bash
cd Code
cp include/secrets.h.example include/secrets.h   # first-boot Wi-Fi defaults
pio run -t upload
pio device monitor                               # prints the board's IP
```

Then open that IP in a browser to configure everything else, and wire up the Claude Code
hooks:

```bash
python3 tools/gen_hooks.py <board-ip> <your-token>
```

Merge the output into `~/.claude/settings.json`.
[`Code/docs/settings.example.json`](Code/docs/settings.example.json) shows what that output
looks like, with placeholders in place of the address and token. The full walkthrough,
including why the hook config is shaped the way it is, is in [Code/README.md](Code/README.md).

## Notes on the build

No external libraries. No LVGL, no async web server, no JSON library — just the Arduino core
and the ESP-IDF components it already ships. The face is drawn as quantised rectangles
straight onto the panel, which is both the correct pixel-art look and fast: 62 fps at under
5% of the frame budget.

## Licence

GPL-3.0. See [LICENSE](LICENSE).

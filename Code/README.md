# Claude Monitor Companion

A desk mascot that shows what Claude Code is doing.

A Waveshare ESP32-S3-Touch-LCD-1.69 sits in a 3D-printed shell shaped like the Claude
mascot — the chunky pixel-art crab. The LCD is set into the middle as its face: a flat
orange field with two black bar eyes. The eyes doze when nothing is happening, get bored
when you wander off, focus while Claude works, and snap into a `><` grin the moment Claude
needs you.

Claude Code hooks POST bare event names at the board over the LAN. The board decides how it
feels about them.

- [docs/SPEC.md](docs/SPEC.md) — design, mood engine, open questions
- [docs/HARDWARE.md](docs/HARDWARE.md) — pin map, measured numbers, traps

## Setup

You flash once. After that everything is configured from the board's own web page — Wi-Fi
included — so there is never a second reason to open the firmware.

### 1. Credentials

```bash
cp include/secrets.h.example include/secrets.h
```

Fill in `WIFI_SSID` and `WIFI_PASS`, and set `NOTIFY_TOKEN` to any random string. These are
only the **first-boot defaults**: they are copied into NVS on first run and everything after
that is edited from the settings page. `include/secrets.h` is gitignored.

If you get them wrong, nothing is lost. After four failed joins the board gives up on your
network and hosts its own open access point called `<hostname>-setup`, serving the same
settings page at `http://192.168.4.1/`. Join it from a phone and fix the Wi-Fi there.

### 2. Build and flash

PlatformIO Core lives at `~/.platformio/penv/bin/pio` on this machine and is not on `PATH`.

```bash
~/.platformio/penv/bin/pio run -t upload
```

### 3. Find the board

```bash
~/.platformio/penv/bin/pio device monitor
```

It prints its IP on connect. **Give it a DHCP reservation and use the IP in the hook
config** — on both platforms.

The board does advertise `claude-notifier.local` over mDNS and it resolves fine from a
browser or an interactive `curl`. It is still the wrong choice for hooks: resolution was
measured at **5.1 s per lookup on macOS, uncached**, against the hook's 2 s timeout, so every
hook fails silently. The same request to the raw IP takes **86 ms**. Windows cannot generally
resolve `.local` at all without Bonjour.

Both ends must also be on the **same access point**. Traffic that crosses a range extender's
wireless backhaul was measured at 100% packet loss here; moving the Mac onto the same AP as
the board took it to 0% loss and 27 ms.

### 4. Wire up the hooks

```bash
python3 tools/gen_hooks.py 192.168.1.42 your-token
```

Paste the output into the `hooks` key of `~/.claude/settings.json`. User scope, not project
scope — this is a personal desk peripheral, not something to commit into every repo.

[`docs/settings.example.json`](docs/settings.example.json) is exactly what the generator
prints, with the address and token replaced by `BOARD-IP` and `YOUR-TOKEN`. Read it if you
want to see the shape before running anything; generate the real one rather than editing it
by hand, because it repeats the address twelve times. On Windows the `hooks` block is
byte-identical and only `statusLine.command` differs:

```
powershell -NoProfile -ExecutionPolicy Bypass -File "C:/path/to/Code/tools/statusline.ps1" BOARD-IP YOUR-TOKEN
```

Merge it in rather than overwriting the file — `~/.claude/settings.json` also holds your
theme and notification preferences, and Claude Code will already have written some of it.

Check it works:

```bash
curl -s -X POST -H "Authorization: Bearer your-token" http://192.168.1.42/e/needs_input
```

The face should snap to `><`. Tap the screen to acknowledge.

## Why the hook config looks like that

Two details in the generated config are load-bearing.

**`args` is present, which selects exec form.** Claude Code spawns `curl` directly with no
shell. Without `args` it is shell form, which goes through `sh` on macOS but **PowerShell**
on Windows when Git Bash is absent — and PowerShell aliases `curl` to `Invoke-WebRequest`,
which takes entirely different flags. Exec form is byte-identical on both platforms, and
`curl` ships with macOS and with every Windows version Claude Code supports.

**`"async": true`.** Hook handlers block Claude Code's agentic loop by default, with a 600 s
timeout. A board that is switched off could otherwise stall a session for ten minutes.
`curl -m 2` bounds the network call as well, so dead-board processes cannot pile up.

Together these mean the host needs no script, no runtime, and no compiled helper. That is
only possible because the event rides in the URL path with no request body — which in turn
is only possible because the board owns the mood state machine and the host reports nothing
but raw facts.

## Linux desktop companion

No board, or a second screen for the same events? [`desktop/`](desktop) builds this firmware
for Linux and runs it in a window: the same mood engine, faces, screens and settings page,
compiled from the same sources, with the panel shown pixel for pixel. Where the board has its
printed shell, the window draws the Claude Code mascot's body around the screen — the wide
pixel crab from the CLI's start-up banner — with the panel's eyes as its eyes. It listens for the same
HTTP requests as the board, so the same hooks drive it, and it can run alongside a board.

Built and tested on Kubuntu 26.04 (Plasma 6, Wayland). It should work on any Linux desktop
with SDL 3.2 or newer and systemd; the taskbar highlight needs a dock that implements the
LauncherEntry interface, as Plasma's task manager does.

**Mouse as finger.** Everything the touchscreen does, the mouse does:

| Do this | Same as on the board | Effect |
|---|---|---|
| Click the face | tap | acknowledge an alert, poke it (five quick pokes make it cross) |
| Drag sideways | swipe sideways | next or previous face style |
| Drag up or down | swipe up or down | face, five-hour gauge, weekly gauge, stats |
| Space or Enter | tap | — |
| Arrow keys, mouse wheel | swipes | — |
| Any other key | a keystroke on the USB serial console | `p` `n` `f` `1`–`9` `d` `[` `]` `v` … see [Serial commands](#serial-commands) |

**Getting your attention, without taking focus.** When Claude needs input or hits the usage
limit, the field goes red and the bell and buzzer sound, as on the board. Its taskbar entry is
also highlighted, and a desktop notification appears. Both clear by themselves once the alert
is dealt with, wherever you answered it. The window never raises itself or takes the keyboard.

**Sound** comes from your speakers, synthesised from the same settings as the board's buzzer
and bell. One default differs from the board: failures are silent, because the buzz for every
failed tool call is a lot to hear from laptop speakers. Change it under *Sound on errors* on the
settings page. The eyes also start further apart than on the board (*Gap between eyes* 120), to
sit where the mascot's do; 120 is the widest at which the glasses styles still fit.

### 1. Install the build dependencies

```bash
sudo apt install build-essential cmake pkg-config libsdl3-dev libsystemd-dev
```

`curl` and `python3`, which the hooks and the tools use, are already part of Kubuntu.

### 2. Build and install

```bash
cd Code/desktop
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build -j
ctest --test-dir build          # optional: the touch and end-to-end tests, headless
cmake --install build
```

This installs into your home directory only:

| | |
|---|---|
| `~/.local/bin/claude-monitor-companion` | the program |
| `~/.local/share/systemd/user/claude-monitor-companion.service` | starts it with your session |
| `~/.local/share/applications/claude-monitor-companion.desktop` | menu entry, taskbar icon, notification settings |
| `~/.local/share/icons/hicolor/scalable/apps/claude-monitor-companion.svg` | icon |
| `~/.local/share/claude-monitor-companion/claude-monitor-companion.kwinrule` | optional KWin rule (below) |

Give the prefix when configuring, as above, not to `cmake --install --prefix`: the service and
the menu entry contain the installed path.

### 3. Start it now and at every login

```bash
systemctl --user daemon-reload
systemctl --user enable --now claude-monitor-companion.service
```

The service belongs to your graphical session: it starts when Plasma does, stops when you log
out, and comes back by itself if it ever crashes. Closing the window is taken as meaning it, and
it stays closed until the next login or until you start it from the application menu (the menu
entry starts the service, so there is only ever one copy).

To run it in a terminal instead, for its log or to try options, stop the service first:

```bash
systemctl --user stop claude-monitor-companion
~/.local/bin/claude-monitor-companion --help
```

| Option | |
|---|---|
| `--port N` | listen on another port (default 8765) |
| `--scale N` | starting window size as a multiple of the 240×280 panel, from 0.25 to 8 (default 2). `0.25` opens it at its smallest. The window can then be resized freely, down to a quarter of the panel |
| `--no-shell` | show only the 240×280 screen, without the mascot's body around it |
| `--mute` | no sound at all |
| `--notify LIST` | which events notify: `needs_input`, `limit`, `error`, `done`, or `none` (default `needs_input,limit`) |
| `--no-attention` | do not highlight the taskbar entry |
| `--listen ADDR` | listen on another address than 127.0.0.1. The settings page is then open to that network, as the board's is |

To give the service options, override its command:

```bash
systemctl --user edit claude-monitor-companion
```

```ini
[Service]
ExecStart=
ExecStart=%h/.local/bin/claude-monitor-companion --scale 3
```

### 4. Set the hook token

Open <http://127.0.0.1:8765/>, the same settings page the board serves, and set *Hook token*
under Wi-Fi. If you also have a board, use the same token on both. From a terminal instead:

```bash
curl -d token=your-token http://127.0.0.1:8765/api/config
```

### 5. Register the Claude Code hooks

The generator takes a comma-separated list of targets, and `desktop` is shorthand for
`127.0.0.1:8765`:

```bash
python3 Code/tools/gen_hooks.py desktop your-token                # the desktop only
python3 Code/tools/gen_hooks.py 192.168.1.42,desktop your-token   # a board and the desktop
```

Merge the output into `~/.claude/settings.json` exactly as in
[step 4 of the board setup](#4-wire-up-the-hooks). With several targets every hook runs one
`curl` per target, each async with its own 2 s limit, so a board that is switched off never
delays the desktop. Choosing the board, the desktop or both is only ever this command — no
code changes. In Claude Code, `/hooks` shows what is registered.

### 6. Try it without Claude Code

[`tools/simulate_events.py`](tools/simulate_events.py) sends exactly what the hooks and the
status line would, and prints the mood the mascot ends up in:

```bash
python3 Code/tools/simulate_events.py --token your-token                # a guided tour of every state
python3 Code/tools/simulate_events.py --token your-token needs_input    # one event
python3 Code/tools/simulate_events.py --token your-token limits:42:17   # usage figures for the gauges
python3 Code/tools/simulate_events.py --list
```

It takes `--host` with the same target list as the generator, so it can drive a board and the
desktop at once. `MASCOT_TOKEN` in the environment saves typing the token.

### Keeping it on top

A Wayland program cannot put itself above other windows or pick where it opens; the compositor
decides. To have KWin do both, import the bundled rule: *System Settings → Window Management →
Window Rules → Import…*, and choose
`~/.local/share/claude-monitor-companion/claude-monitor-companion.kwinrule`. It keeps the window
above others and remembers its position; adjust it there.

### When it is not running

A hook sent to a companion that is not running fails at once with "connection refused", and
since every hook is async, Claude Code never notices. The event is simply lost, exactly as with
a board that is switched off; the face picks up from the next one. Settings, drawn faces and
the last usage figures are kept on disk, and the status line resends usage within a minute.

### Where things live

| | |
|---|---|
| `~/.config/claude-monitor-companion/` | settings, drawn faces and usage figures, one small text file per store |
| `journalctl --user -u claude-monitor-companion` | the log — the same lines the board prints over USB |
| `systemctl --user kill -s USR1 claude-monitor-companion` | saves the panel as `~/.config/claude-monitor-companion/screenshot.png` |

### How it is built

The firmware is not ported; it is compiled as it is. `desktop/shim/` supplies the small part of
Arduino-ESP32 and FreeRTOS the firmware calls (`Serial`, `String`, `Preferences`, `WebServer`,
tasks and queues), and `desktop/src/` implements the three hardware headers —
[`display.h`](include/display.h), [`touch.h`](include/touch.h) and [`bell.h`](include/bell.h)
— for a window, a mouse and a sound card. Nothing in `src/` or `include/` has a desktop
`#ifdef`, and PlatformIO never sees `desktop/`, so the ESP32 build is unaffected.

Deliberate differences from the board:

- It listens on 127.0.0.1:8765 rather than port 80, and refuses requests from other web sites
  and rebound host names — a loopback port is reachable from every tab in your browser.
- No Wi-Fi. The Wi-Fi settings have no effect beyond restarting the companion, the way they
  reboot the board; the Wi-Fi serial commands do nothing; the stats screen shows the
  listening address and a signal of 0 dBm.
- The clock and timezone are the computer's; the timezone setting is ignored.
- There is always a bell, whatever GPIO the settings name.
- Mouse coordinates are the panel's own. The board's touch axes have not been verified against
  its panel (see `src/board/touch.cpp`), so if a swipe moves the other way on a real board,
  that is the board's mapping, not the desktop's.

### Troubleshooting

**"Port 8765 is already in use".** A copy is already running (`systemctl --user status
claude-monitor-companion`), or something else has the port. In that case use `--port`, and
generate the hooks with `127.0.0.1:<port>` instead of `desktop`.

**No window at login.** Check `systemctl --user status claude-monitor-companion`. "cannot open
a window" in the log means the service started without the session's display; `systemctl
--user show-environment | grep WAYLAND` should list `WAYLAND_DISPLAY`. Plasma provides it
normally; on other desktops, run `systemctl --user import-environment WAYLAND_DISPLAY DISPLAY`
from the session's autostart.

**Events do not arrive.** Send one by hand:
`python3 Code/tools/simulate_events.py --token your-token needs_input`. `HTTP 401` means the
token on the settings page and the one in the hooks differ. If the simulator works but Claude
Code does not move the face, check `/hooks` in Claude Code, and remember that `statusLine`
changes only apply to new sessions.

**No sound.** Look for "no audio" or "cannot open audio device" in the log, check that the
service is not running with `--mute`, and check the three *Sound* choices on the settings page.
The audio device is opened only while a sound plays, so the companion does not show up in the
volume applet in between.

**No notification or taskbar highlight.** Both are skipped while the companion's window has
focus — you are already looking at it. Otherwise check Do Not Disturb, and *System Settings →
Notifications → Applications → Claude Monitor Companion*. The highlight needs the installed
`.desktop` file, so a build run straight from `build/` notifies but does not highlight.

**The gauges say `EST NO HOST`.** The status line is not reaching the companion; see
[Where the gauge's numbers come from](#where-the-gauges-numbers-come-from). It only runs in the
CLI, not the desktop app.

**Start from scratch.** `systemctl --user stop claude-monitor-companion`, delete
`~/.config/claude-monitor-companion/`, and start it again.

**Uninstall.** `systemctl --user disable --now claude-monitor-companion`, then delete the files
listed in step 2 and `~/.config/claude-monitor-companion/`, and remove the hooks from
`~/.claude/settings.json`.

## The settings page

Open the board's IP in a browser. Everything is on one page, grouped:

| Group | What you can change |
|---|---|
| **Sound** | piezo on/off, how many buzzes, both tones, volume, both gaps, the bell's GPIO, whether it rings on alerts and on errors |
| **Face** | style, background shade, eye width / height / gap / vertical position, pixel size, blink timings |
| **Timing** | bored-after, asleep-after, celebration length, sulk length, alert timeout, pokes before annoyed, poke window |
| **Screen** | the four backlight levels |
| **Wi-Fi** | network, password, hostname, hook token |

Plus test buttons for the buzzer, the bell and every mood, and a two-step factory reset.

Changes apply immediately — drag the eye width and the face changes as you watch. Only
Wi-Fi settings restart the board.

**The whole thing is table-driven.** One row in [src/app/config.cpp](src/app/config.cpp)
declares a setting, and its storage, validation, JSON and web form all follow from it. The
page renders whatever the board reports, so adding a setting never means editing HTML. Even
the mood test buttons come from a list the firmware publishes.

Some details that matter:

- **Passwords are never sent to the browser.** A blank password field means "unchanged", so
  saving other settings can never wipe your Wi-Fi key.
- **Out-of-range values are rejected, not clamped silently**, and the page reports the
  failure rather than looking like it worked.
- **Editing is not clobbered by the live refresh.** The page polls every few seconds to show
  the current mood and to pick up changes made on the device itself — swiping the style, for
  instance — but it skips any control you are focused on or have unsaved changes in.
- **No authentication.** It is LAN-only, and the bearer token still protects the `/e/*`
  routes a hook uses. Requiring a password to change a buzzer tone would defeat the point.

## Serial commands

The board takes single keystrokes over USB serial, so the animation can be tuned without a
host in the loop.

| | |
|---|---|
| `p` `f` `n` `e` `i` `s` `x` `a` | fire an event: prompt, turn finished, needs input, error, idle, session start/end, ack |
| `1`–`8` | force a mood: chill, working, excited, confused, angry, bored, sleeping, waking |
| `d` | auto-cycle every mood, 5 s each |
| `[` `]` | cycle the face style — same as swiping |
| `c` | cycle the orange candidates |
| `b` | test the bell |
| `w` | scan for 2.4 GHz networks — marks your SSID with `*` and shows its auth mode |
| `h` | health line |
| `?` | help |

Forcing moods matters because `BORED` is five minutes away and `SLEEPING` is thirty.

## Screens

**Swipe up or down** to move between three screens. **Swipe sideways** to change the face's
style, as before. If your panel reports its axes the other way round, flip
`Swap swipe axes` on the settings page rather than rebuilding.

| Screen | What it shows |
|---|---|
| **Face** | the mascot. The resting screen, and what the shell is built around |
| **Gauge** | a ring showing how much of the five-hour limit is used, with time to reset below |
| **Weekly** | the same ring for the seven-day, all-models limit. No countdown — the number is the point |
| **Stats** | IP, signal, uptime, mood, energy, prompts this window, current time |

### Where the gauge's numbers come from

The ring shows the real percentage of your five-hour limit, and the line beneath it the real
time to reset. Both come from Claude Code's **status line**, which is the only place it
exposes them:

```
rate_limits.five_hour.used_percentage    0-100
rate_limits.five_hour.resets_at          unix epoch seconds
rate_limits.seven_day.used_percentage    the all-models weekly limit
rate_limits.seven_day.resets_at
```

There is **no credit or balance figure** anywhere in the status line or the telemetry docs —
`cost.total_cost_usd` is a client-side estimate of the current session only. So there is no
credits screen: an invented number on a dial looks exactly as authoritative as a real one.

**No hook carries this.** The only limit-related hook is `StopFailure` with
`error: rate_limit`, and that fires once you have already hit the wall. So
[tools/statusline.py](tools/statusline.py) reads the status line JSON, forwards those two
numbers to `POST /limits`, and prints an ordinary status line. `gen_hooks.py` emits its
config alongside the hooks.

Two details in that script are load-bearing. The POST is **spawned detached and never waited
on**, because Claude Code cancels an in-flight status line script when a new update arrives —
a blocking call would be killed part-way, and would stall the status bar whenever the board
was off. And the status line **prints regardless** of whether forwarding worked: a desk
ornament must never be able to break the editor it decorates.

Without the status line configured, the gauge falls back to timing the window from the
prompts the board has seen and labels itself `EST - NO HOST`. That fallback is only ever a
lower bound — it cannot know about work done before the board was switched on, so it reads
the window as opening later than it really did. This is why the fallback is labelled rather
than dressed up as a quota.

Wall-clock time comes from NTP through the POSIX timezone string in settings. Until the first
sync the gauge reads `NO CLOCK` rather than guessing.

### Platform notes

The two halves are portable to different degrees, and it is worth knowing which is which.

**The hooks are genuinely OS-independent.** They use exec form — `command` plus an `args`
array — so Claude Code spawns `curl` directly with no shell involved: no quoting rules, no
PATH surprises. `curl` ships with macOS, with Windows since 1809 (which is below Claude
Code's own minimum), and with essentially every Linux. The same block works byte-identically
on all three. Moods, faces, the red field, the exclamation mark and the sounds are all on
this side.

**The status line is not.** `statusLine` takes only a `command` string — there is no `args`
field, so it is always run through a shell, and a different shell on each platform. Hence two
scripts:

| | Script | Needs |
|---|---|---|
| macOS, Linux | [tools/statusline.py](tools/statusline.py) | `python3` on PATH |
| Windows | [tools/statusline.ps1](tools/statusline.ps1) | nothing beyond what Windows ships |

The PowerShell version exists because Python is *not* dependable on Windows — it is usually
`python` rather than `python3`, and the Microsoft Store stub hijacks the name — whereas
PowerShell always is. It parses the JSON with `ConvertFrom-Json` and posts with `curl.exe`,
both built in.

Two Windows details the generator handles for you, and which are easy to get wrong by hand:
the script path must use **forward slashes**, because Claude Code routes the status line
through Git Bash when it is installed and Git Bash silently eats backslashes in a Windows
path; and the path is **double quoted**, so a space in it does not split the command.

**The status line only runs in the CLI.** The desktop app has no status bar, so it never
invokes the command — confirmed by instrumenting the script and watching a full
`refreshInterval` pass with zero calls.

**And it must be the session you are actually working in.** A Claude Code session refreshes
its rate-limit snapshot from API responses, so an *idle* session forwards the same figure
forever, punctually, while the real work happens elsewhere. Parking an idle CLI window next
to the desktop app does not work: the gauge looks live, because reports keep arriving on
time, while the number is frozen. That is worse than showing nothing.

The board detects it. The hooks keep telling it prompts are being submitted, so if five
prompts arrive with no change in the reported percentage, the gauge labels itself
`PCT STALE` and `/api/config` reports `limit_suspect: true`. Five prompts is comfortably more
than a percentage point of a five-hour budget, so an unchanged figure across that many is not
plausible.

So: **use the CLI as your main interface if you want the gauges live.** If you prefer the
desktop app, remove the `statusLine` entry — the board then falls back to its own elapsed-time
estimate and labels it `EST NO HOST`, which is honest about being an estimate.

**`statusLine` is read when a session starts.** Hooks hot-reload when you edit settings, the
status line does not — after changing it, restart the session or it keeps running the old
command.

The gauge shows `EST NO HOST` until the first report arrives, and `device.limit_age_s` on
`/api/config` reads -1 until then. That is the quickest way to tell "not running" from
"nothing has changed", and `MASCOT_DEBUG=1` on the status line command logs every invocation
and its payload to `~/.claude/mascot-statusline.log` when you need to know which.

### How often it refreshes

The percentage comes from the host, so it updates whenever the status line runs: on every
assistant message, and at least once a minute via `refreshInterval`. Claude Code debounces
those at 300 ms.

The countdown does not depend on the host at all. `resets_at` is an absolute epoch, so once
the board has it, NTP time is enough to tick the remaining time down every minute — even
with Claude Code closed.

### Drawing a smooth ring cheaply

The ring is a real circle, not a ring of blocks. The two costs are worth separating: testing
every pixel in the bounding box is a few thousand float operations and costs nothing, while
one SPI transaction per pixel would be ruinous. So each row is scanned, runs of identical
colour are coalesced, and one rectangle is pushed per run — a few hundred transactions for
the whole ring, and only when the value changes.

The unfilled part of the track is the background colour darkened at runtime, so it reads
correctly on the orange field and on the red alert field without a second hardcoded colour.

Everything else — the percentage, the countdown, the face — stays on the pixel grid. The
ring is the one place a curve beats blocks, because a dial has to be readable at a glance
from across a desk.

## Faces

The printed mascot exists in several faces, so the firmware does too. **Swipe across the
screen** to cycle them, or `[` and `]` over serial. The choice is saved to NVS and survives
a reboot.

Swipes are derived from the raw coordinate stream, not from the CST816's own gesture
register. The chip only decides a gesture happened once the finger lifts, and it latches the
code afterwards — so a second swipe the same way reads as unchanged and gets dropped, which
is exactly the motion you make flicking through styles. Watching coordinates instead means
the swipe fires the moment it crosses the threshold, mid-drag. Thresholds are `SWIPE_MIN_PX`
and friends in `tuning.h`; sampling runs at 5 ms.

| Style | On screen |
|---|---|
| `plain` | two bar eyes, nothing else — the default character |
| `grin` | `><` chevrons, always, regardless of mood |
| `round` | round spectacles with a bridge, pupils animating inside them, barred grill mouth |
| `pixel` | chunky rectangular glasses with white glints, open smile |
| `shades` | deal-with-it sunglasses and a handlebar moustache |

Style is a costume; mood is the expression underneath. Both apply at once — a bored mascot
in shades is still bored, it just shows it differently.

That last part is why the opaque styles work at all. When the lenses hide the eyes, the mood
has nowhere to go, so the glasses inherit the motion instead: they bounce when excited and
droop when bored. The eyes are pinned still in those styles precisely so the accessories can
carry it.

Spectacles are the other interesting case. The eyes shrink to pupils and their travel is
penned inside the lenses, but a blink still scales proportionally, so it reads as a blink at
pupil size rather than a clipped version of the full-size one.

Accessories are static art, drawn once when the style changes and again only when they move.
Curves — the lens rings, the smile, the moustache — are drawn one grid row at a time, so a
circle comes out as a staircase. That is the point.

## Which hooks drive what

| Hook | Matcher | Board event | Face |
|---|---|---|---|
| `UserPromptSubmit` | — | `prompt_submitted` | `WORKING` |
| `PermissionRequest` | — | `needs_input` | **red, sticky** |
| `PreToolUse` | `AskUserQuestion\|ExitPlanMode` | `needs_input` | **red, sticky** |
| `PostToolUse` | `AskUserQuestion\|ExitPlanMode` | `answered` | back to `WORKING` |
| `Notification` | `permission_prompt` | `needs_input` | **red, sticky** |
| `Notification` | `idle_prompt` | `idle` | `CHILL` |
| `Stop` | — | `turn_finished` | `EXCITED`, 6 s |
| `PostToolUseFailure` / `StopFailure` | `*` / — | `error` | `CONFUSED` |
| `SessionStart` / `SessionEnd` | — | session lifecycle | — |

**`PermissionRequest` is the one that matters** for "Claude is waiting on me". The docs are
explicit that it is narrower than `PreToolUse`: it fires *only* when Claude Code is about to
show a permission dialog, not before every tool call. So it costs nothing when nothing is
being asked. `Notification`/`permission_prompt` covers the same ground but ~6 s later; it is
kept because re-asserting the same state is harmless and it catches anything the first one
misses.

`PreToolUse` matches on tool name, so scoping it to `AskUserQuestion` and `ExitPlanMode` —
the two tools whose entire purpose is asking the user something — means no other tool call
ever spawns a process.

`needs_input` deliberately does **not** clear the running-turn flag: a permission prompt
happens *during* a turn, so answering it returns the face to `WORKING`, not `CHILL`. The
paired `PostToolUse` hook is what clears it. A sticky alert also self-expires after
`ALERT_MAX_MS` as a safety net, so a missed clear can never strand the face red.

## Drawing your own face

The settings page has a pixel editor. Four slots, 24 x 28 cells each, at ten screen pixels a
cell — exactly 240 x 280, so what you draw is literally what appears, with no scaling.

Three inks: black, white and erase. White because the built-in sunglasses use it for lens
glints, and a custom face should be able to do the same.

Each slot has **two frames**, resting and blink. A static face would sit visibly dead next to
the built-in ones, which all blink, and one extra frame is the cheapest animation there is.
There is a "copy resting to blink" button because a blink is almost always the resting face
with the eyes closed. Leave the blink frame empty and the face simply does not blink.

**Only slots with something drawn join the swipe rotation**, so empty ones are never dead
screens. Clearing a slot drops it back out. Occupancy is judged from the resting frame alone.

Saved to NVS, one key per slot so saving one face does not rewrite the others, and written
once when you press save rather than on every stroke — flash write cycles are finite. It
survives reboots and reflashing, since flashing does not erase the NVS partition.

**A drawn face animates too.** It cannot change shape the way the parametric eyes do, but it
can move, and moving carries most of the expression. The whole drawing translates within the
panel and the background fills whatever the shift vacates — so it sweeps while Claude works,
droops and looks aside when bored, bounces when excited, shakes off a poke, and breathes
slowly while asleep. Exactly the same mood vocabulary as the built-in faces, with no extra
frames to draw.

Motion is in whole cells. The art is ten-pixel blocks, so a ten-pixel step is what keeps it
reading as the same object moving, rather than a bitmap sliding underneath a grid. Amplitudes
are `CUSTOM_*_CELLS` in [tuning.h](include/tuning.h).

Only cells that differ from what is already on screen get redrawn, so a one-cell drift or a
blink is a handful of rectangles rather than 672.

## Mood model

Two inputs: events pushed from the host, and time the board tracks itself.

| Mood | Enters when | Eyes |
|---|---|---|
| `SLEEPING` | nothing for ~30 min | closed to flat bars, slow breathing drift |
| `WAKING` | first event after sleeping | bars grow with a slight overshoot |
| `CHILL` | recent activity, nothing running | full bars, random blinks, slow drift |
| `BORED` | no new prompt for 5 min | 60% height, drooped and looking away, long blinks |
| `WORKING` | between prompt and turn finished | full height, sweeping left-right scan |
| `EXCITED` | needs input, or turn finished | `><` chevrons, bounce, bell |
| `CONFUSED` | a turn failed | one eye squinting, slow shake |
| `ANGRY` | poked five times in four seconds | one fixed set of slanted brows, red field |

**The field turns red whenever the mascot wants something** — an unacknowledged `needs
input`, or a fit of pique. Visible across a room, and it does the bell's job until the bell
exists. A finished turn does *not* go red: that is a celebration, not a demand.

**Poke it and it shakes the poke off.** Every tap runs a fast head-shake over whatever mood
is playing. Keep poking — five taps inside four seconds — and it snaps to `ANGRY`: one fixed
set of slanted brows, no animation, red background, seven seconds to cool off. Boredom and
sleep are suppressed while it sulks.

`BORED` and `ASLEEP` cannot come from hooks — `idle_prompt` fires once and never repeats,
and nothing fires at all when Claude Code is not running. They are elapsed-time decay on the
board, which is also why the face keeps behaving when Wi-Fi drops.

Alerts are sticky. `needs_input` waits to be acknowledged; boredom and sleep are suppressed
until it is, so something you still have to deal with never quietly turns into a yawn. A
finished turn is a six-second celebration that decays on its own.

**Energy** is a separate 0–100 value that decays continuously and is bumped by every event.
It modulates blink rate, sweep speed and bounce amplitude *within* a mood, so a quiet
afternoon and a heavy refactor look different even when both are nominally `WORKING`.

## Tuning the face

Everything lives in [include/tuning.h](include/tuning.h): palette, grid quantum, eye
geometry, per-mood motion amplitudes, mood thresholds, energy rates, backlight levels. One
edit and a reflash. Nothing is hardcoded elsewhere.

`GRID_Q` is the interesting one. Every coordinate is snapped to it, which is what makes the
motion step like pixel art instead of gliding. It also sets how many distinct steps a blink
has — about 20 at the current 5 px, which reads as deliberate. At 10 px it would be 10,
which reads as broken.

## Performance

Measured on the actual board, 240x280 at 80 MHz SPI:

| | |
|---|---|
| Frame rate | 62 fps, held during a Wi-Fi association retry storm |
| Frame cost, `CHILL` / `BORED` / `CONFUSED` | 78–160 µs |
| Frame cost, `EXCITED`, bare eyes | 703 µs |
| Frame cost, `EXCITED`, accessory styles | 0.6–2.0 ms |
| Full-screen fill | 18.6 ms |
| Flash / RAM | 12.5% / 14.6% |

Under 5% of the 16 ms frame budget at worst. Three things get it there: only the pixels that
actually changed are pushed, rendering runs in its own task on core 1 while Wi-Fi stays on
core 0, and every solid colour has its own pre-filled DMA buffer so transfers pipeline
without ever waiting.

## Dependencies

None. No LVGL, no ESPAsyncWebServer, no JSON library, no vendor SDK — just the Arduino core
and the IDF components it already ships.

## Sound

Two separate noises, deliberately.

Three cues, and you choose independently for each whether it uses the striker, the buzzer,
both, or nothing:

| Cue | When |
|---|---|
| **Blocked** | anything that turns the field red — a permission prompt, a question, being poked past patience, or hitting the usage limit |
| **Error** | a tool call or a turn failed |
| **Finished** | a turn ended normally |

Not everyone wants to build a mechanical bell, so the buzzer alone covers all three.

**The buzzer has two shapes, not two pitches.** Good news is a short rising double chirp,
2.7 kHz then 3.5 kHz, repeated so it lands as *buzz buzz*. Bad news is one long 650 ms tone.
Different shapes rather than different notes, so you can tell them apart with your back to
the desk. A long buzz never repeats — that would be alarming rather than informative.

Press `z` for the short one and `l` for the long one, or use the test buttons on the settings
page.

`PIEZO_REPEATS` in `tuning.h` sets how many times; `PIEZO_REPEAT_GAP_MS` is deliberately
wider than the gap inside the pattern, so it reads as two buzzes rather than four beeps.

That second one looks like it contradicts the "hold the piezo low" rule, and does not. The
hazard is the pin *idling* high or floating, which makes a passive buzzer draw current
continuously and cook the LDO. Driving it briefly is what it is for. Every path out of the
chirp sequence — including the gap between the two beeps and the end — goes through
`piezo_silence()`, which parks the duty at zero. It uses `ledc_set_duty(0)` rather than
`ledc_stop()` because stop disables the channel and the next chirp would have to re-enable
it.

Three LEDC timers, one each for backlight, striker and piezo. Channels sharing a timer
corrupt each other's frequency, and the piezo changes frequency mid-sequence.

### The servo is released, not held

A continuous-rotation servo does not move without a signal, so the bell cuts the PWM and
parks the pin low when idle rather than holding a neutral pulse. Holding neutral keeps the
servo drawing current, and if its neutral trim is even slightly off it creeps forever — in a
sealed case, next to the antenna, for no reason.

The servo is also attached on demand rather than at boot, so start-up and joining the network
happen with it completely inert.

## Wi-Fi troubleshooting

The board supervises its own reconnection: `setAutoReconnect()` stops trying after enough
failures, which leaves the board offline forever with nothing in the log. Every
`WIFI_RETRY_MS` it tears the association down and starts again, logging the attempt.

Press `w` for a scan. It disconnects first — scanning mid-handshake returns an empty list
that looks like "no networks" and is not — then lists what it can see, marks your SSID with
`*`, and prints each network's auth mode. Read the disconnect reason alongside it:

| Reason | Means |
|---|---|
| `201 NO_AP_FOUND` | SSID not visible. Usually a 5 GHz-only network — the ESP32-S3 has no 5 GHz radio |
| `202 AUTH_FAIL` | AP rejected the key. Wrong password |
| `15 4WAY_HANDSHAKE_TIMEOUT` | Handshake started and stalled. Wrong password, or WPA3/PMF required |

If the password contains a backslash or a double quote it must be escaped in `secrets.h` —
it is a C string literal, so `\` and `\"`.

## Verified on hardware

Everything below was measured on the real board, not inferred.

| | |
|---|---|
| HTTP endpoints | 12/12 — every event route, plus 401 on missing/bad token, 404 on unknown event and on the wrong verb |
| Link reliability | 20/20 single-shot, once both ends share an access point |
| Hook argv | 8/8 fire, 43–166 ms each, using the exact `command` + `args` Claude Code will execute |
| Face | all eight moods, all five styles, red alert field, angry face, poke shake |
| Render | 62 fps sustained, 78 µs–2 ms per frame, held through a Wi-Fi retry storm |

## Not done yet

- The mechanical bell. An external striker on LEDC PWM; the actuator, drive stage and GPIO
  are undecided, so `PIN_BELL` is `-1` and `bell::strike()` logs instead. Everything that
  should ring it is already wired — see Sound below.
- Eye geometry is placeholder until the shell's screen cutout is measured.
- Which of the four orange candidates matches the filament.
- Touch coordinate orientation is unverified against a real finger.
- `tools/statusline.ps1` has been written and reviewed but never executed — it was developed
  on a Mac with no PowerShell available. The logic mirrors the Python version, which is
  tested, but treat the first Windows run as the real test. Swipe cycling keys off
  whichever axis dominates, so a horizontal flick works even if the driver's axes turn out
  swapped; only the direction could need flipping, via `SWIPE_INVERT`.
- Hats are not rendered. The top hat and the sailor hat in the reference photos sit above
  the head, outside the screen cutout — they belong to the printed shell, not the firmware.

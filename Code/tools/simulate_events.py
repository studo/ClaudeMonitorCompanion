#!/usr/bin/env python3
"""Send the mascot the events Claude Code would, without running Claude Code.

Usage:
    python3 tools/simulate_events.py [--host HOSTS] [--token TOKEN]
                                     [--delay SECONDS] [EVENT ...]

    python3 tools/simulate_events.py                    # the guided tour
    python3 tools/simulate_events.py needs_input        # one event
    python3 tools/simulate_events.py prompt_submitted turn_finished
    python3 tools/simulate_events.py limits:42:17       # what the status line sends
    python3 tools/simulate_events.py --list

EVENT is any of the board's event names (see --list), "limits:PCT[:WPCT]" to
report usage the way tools/statusline.py does, or "tour" (the default), which
walks through every state the face can be in with a pause between steps.

--host takes the same comma-separated list as gen_hooks.py, so one run can
drive the board and the desktop companion together. "desktop" is shorthand
for the desktop companion's default address, 127.0.0.1:8765, which is also
the default here. --token defaults to $MASCOT_TOKEN, then to "change-me".

Works against the ESP32 board and the desktop companion alike: they accept
the same requests. After each event the current mood is read back from
/health so you can see what the mascot made of it.
"""

import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request

DESKTOP = "127.0.0.1:8765"

# The routes in src/net/net.cpp, with what each one stands for in Claude Code.
EVENTS = {
    "session_started":  "SessionStart: a Claude Code session opens",
    "prompt_submitted": "UserPromptSubmit: you send a prompt",
    "needs_input":      "PermissionRequest / AskUserQuestion: Claude waits for you",
    "answered":         "PostToolUse on AskUserQuestion: you answered",
    "turn_finished":    "Stop: Claude finished its turn",
    "error":            "PostToolUseFailure / StopFailure: something failed",
    "limit_reached":    "StopFailure rate_limit: the usage limit is hit",
    "idle":             "Notification idle_prompt: a minute of nothing",
    "session_ended":    "SessionEnd: the session closes",
}

# Every state the face has, in an order that makes sense to watch.
TOUR = [
    ("session_started",  "a session opens; the face is awake and calm"),
    ("prompt_submitted", "WORKING: the eyes sweep while Claude runs"),
    ("needs_input",      "Claude wants you: red field, >< eyes, bell and buzz"),
    ("answered",         "you answered it: back to WORKING"),
    ("turn_finished",    "done: the >< grin and a double chirp, then calm"),
    ("limits:37:12",     "the status line reports usage; swipe up/down to see the dials"),
    ("prompt_submitted", "another prompt"),
    ("error",            "a tool call failed: one eye squints, head shakes"),
    ("limit_reached",    "usage limit: red field and a blinking !"),
    ("prompt_submitted", "work resumes, which clears the limit"),
    ("turn_finished",    "done again"),
    ("idle",             "a quiet minute"),
    ("session_ended",    "the session closes"),
]


def expand_hosts(spec):
    hosts = [h.strip() for h in spec.split(",") if h.strip()]
    return [DESKTOP if h == "desktop" else h for h in hosts]


def post(host, token, path):
    req = urllib.request.Request(
        f"http://{host}{path}", method="POST", data=b"",
        headers={"Authorization": f"Bearer {token}"})
    try:
        with urllib.request.urlopen(req, timeout=2) as r:
            return r.status, r.read().decode(errors="replace").strip()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode(errors="replace").strip()
    except (urllib.error.URLError, OSError) as e:
        reason = getattr(e, "reason", e)
        return None, f"unreachable ({reason})"


def mood(host):
    try:
        with urllib.request.urlopen(f"http://{host}/health", timeout=2) as r:
            return json.loads(r.read()).get("mood", "?")
    except Exception:
        return "?"


def path_for(item):
    """The request path for an event name or a limits:PCT[:WPCT] item."""
    if item.startswith("limits:"):
        parts = item.split(":")[1:]
        now = int(time.time())
        params = [f"pct={int(parts[0])}", f"reset={now + 2 * 3600}"]
        if len(parts) > 1:
            params += [f"wpct={int(parts[1])}", f"wreset={now + 4 * 86400}"]
        return "/limits?" + "&".join(params)
    if item not in EVENTS:
        raise ValueError(f"unknown event '{item}' (try --list)")
    return f"/e/{item}"


def send(hosts, token, item, note=None):
    path = path_for(item)
    if note:
        print(f"\n> {item}: {note}")
    for host in hosts:
        status, body = post(host, token, path)
        if status is None:
            print(f"  {host:22} {body}")
            continue
        # The board applies events on its next frame; give it one.
        time.sleep(0.15)
        flag = "" if status == 200 else f"  <- {body}"
        print(f"  {host:22} HTTP {status}  mood {mood(host)}{flag}")


def main():
    ap = argparse.ArgumentParser(
        description="Send the mascot simulated Claude Code events.",
        epilog="Events: " + ", ".join(EVENTS) + ", limits:PCT[:WPCT], tour")
    ap.add_argument("events", nargs="*", default=["tour"], metavar="EVENT")
    ap.add_argument("--host", default=DESKTOP,
                    help="comma-separated targets (default: desktop = %s)" % DESKTOP)
    ap.add_argument("--token", default=os.environ.get("MASCOT_TOKEN", "change-me"))
    ap.add_argument("--delay", type=float, default=None,
                    help="seconds between events (default: 4 in the tour, 1 otherwise)")
    ap.add_argument("--list", action="store_true", help="list the events and exit")
    a = ap.parse_args()

    if a.list:
        for name, what in EVENTS.items():
            print(f"  {name:17} {what}")
        print("  limits:PCT[:WPCT] the status line's usage report")
        return 0

    hosts = expand_hosts(a.host)
    if not hosts:
        print("no host given", file=sys.stderr)
        return 2

    if a.events == ["tour"]:
        steps = TOUR
        delay = 4.0 if a.delay is None else a.delay
    else:
        steps = [(e, None) for e in a.events]
        delay = 1.0 if a.delay is None else a.delay

    try:
        for item, _ in steps:
            path_for(item)  # validate everything before sending anything
    except ValueError as e:
        print(e, file=sys.stderr)
        return 2

    for i, (item, note) in enumerate(steps):
        if i:
            time.sleep(delay)
        send(hosts, a.token, item, note or EVENTS.get(item))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)

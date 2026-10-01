#!/usr/bin/env python3
"""Claude Code status line that also feeds the mascot its rate-limit figures.

Usage, as the `statusLine.command` in ~/.claude/settings.json:

    python3 /path/to/tools/statusline.py <board-host>[,<host>...] <token>

Several hosts, comma-separated, feed several mascots: a board and the desktop
companion, say. tools/gen_hooks.py writes this line for you.

Why this exists: the board wants to show how much of the five-hour limit is
gone and when it resets, and *hooks cannot tell it*. No hook payload carries
that. The status line's JSON does:

    rate_limits.five_hour.used_percentage    0-100
    rate_limits.five_hour.resets_at          unix epoch seconds

So this reads that, forwards it, and prints an ordinary status line.

Two details are load-bearing:

  Fire and forget. Claude Code cancels an in-flight status line script when a
  new update arrives, so a blocking HTTP call would be killed part-way and, on
  a slow or absent board, would stall the status bar every time it ran. The
  POST is spawned detached and never waited on.

  Printing happens regardless. If the board is off, unreachable, or the
  arguments are wrong, the status line still renders. A desk ornament must
  never be able to break the editor it is decorating.
"""

import json
import os
import subprocess
import sys


def forward(hosts, token, five, seven):
    """Spawn one detached POST per host. Never blocks, never raises into the caller."""
    params = []
    pct = five.get("used_percentage")
    if pct is not None:
        params.append(f"pct={int(round(float(pct)))}")
        params.append(f"reset={int(five.get('resets_at') or 0)}")
    wpct = seven.get("used_percentage")
    if wpct is not None:
        params.append(f"wpct={int(round(float(wpct)))}")
        params.append(f"wreset={int(seven.get('resets_at') or 0)}")
    if not params:
        return
    query = "&".join(params)
    for host in hosts:
        try:
            subprocess.Popen(
                ["curl", "-s", "-m", "2", "-X", "POST",
                 "-H", f"Authorization: Bearer {token}",
                 f"http://{host}/limits?{query}"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                stdin=subprocess.DEVNULL,
                start_new_session=True,
            )
        except Exception:
            pass


def bar(pct, width=10):
    filled = int(round(pct / 100 * width))
    return "[" + "#" * filled + "-" * (width - filled) + "]"


def main():
    # Set MASCOT_DEBUG=1 to record every invocation. The only reliable way to
    # tell "Claude Code is not running this" from "it ran and the POST failed".
    raw = sys.stdin.read()
    try:
        data = json.loads(raw)
    except Exception:
        print("claude")
        return 0

    # MASCOT_DEBUG=1 records each invocation and what arrived on stdin. The only
    # reliable way to tell "Claude Code never ran this" from "it ran and the
    # POST failed" -- a distinction that cost real time to chase.
    if os.environ.get("MASCOT_DEBUG"):
        try:
            import time as _t
            with open(os.path.expanduser("~/.claude/mascot-statusline.log"), "a") as f:
                f.write(f"{_t.time():.0f} bytes={len(raw)} "
                        f"rate_limits={json.dumps(data.get('rate_limits'))[:200]}\n")
        except Exception:
            pass

    host = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("MASCOT_HOST")
    token = sys.argv[2] if len(sys.argv) > 2 else os.environ.get("MASCOT_TOKEN", "")

    limits = data.get("rate_limits") or {}
    five = limits.get("five_hour") or {}
    seven = limits.get("seven_day") or {}
    hosts = [h.strip() for h in (host or "").split(",") if h.strip()]
    if hosts:
        forward(hosts, token, five, seven)

    model = (data.get("model") or {}).get("display_name", "claude")
    ctx = (data.get("context_window") or {}).get("used_percentage")
    cwd = os.path.basename((data.get("workspace") or {}).get("current_dir", "") or "")

    parts = [model]
    if cwd:
        parts.append(cwd)
    if ctx is not None:
        parts.append(f"ctx {int(float(ctx))}%")

    pct = five.get("used_percentage")
    if pct is not None:
        parts.append(f"5h {bar(float(pct))} {int(round(float(pct)))}%")

    print(" | ".join(parts))
    return 0


if __name__ == "__main__":
    sys.exit(main())

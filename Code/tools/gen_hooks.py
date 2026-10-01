#!/usr/bin/env python3
"""Print the Claude Code hook config for the mascot.

Usage:
    python3 tools/gen_hooks.py <host>[,<host>...] <token> [--windows | --unix]

    python3 tools/gen_hooks.py 192.168.1.42 my-token           # the board
    python3 tools/gen_hooks.py desktop my-token                # the Linux companion
    python3 tools/gen_hooks.py 192.168.1.42,desktop my-token   # both at once

Each host is a board's IP or a host:port. "desktop" is shorthand for the
desktop companion's default address, 127.0.0.1:8765 (see Code/desktop). With
several hosts, every hook fires one curl per host, so a board that is switched
off never delays the others: each curl is its own async process with its own
2 s limit. One curl given two URLs would try them in turn, and the second
would wait out the first's timeout. The token must be the same on all of them.

The platform is detected from the machine you run this on; the flags override
it, which is what you want when generating a config for someone else.

Use the board's IP, not its .local name. mDNS resolution was measured at 5.1 s
per lookup on macOS with no caching, which blows straight past the hook's 2 s
timeout and makes every hook silently fail. Give the board a DHCP reservation
so the address is stable.

Merge the output into ~/.claude/settings.json. It contains two top-level keys:
"hooks" for the mood events, and "statusLine" for the usage gauge -- the
five-hour limit percentage and reset time exist only in the status line's JSON,
not in any hook payload.

Two things about the generated config are load-bearing:

  "args" present  -> exec form. Claude Code spawns curl directly with no shell,
                     so the config is byte-identical on macOS and Windows. Shell
                     form would go through sh on macOS but PowerShell on Windows
                     when Git Bash is absent, and PowerShell aliases `curl` to
                     Invoke-WebRequest, which takes entirely different flags.

  "async": true   -> fire and forget. Hook handlers block Claude Code's agentic
                     loop by default with a 600 s timeout, so an unreachable
                     board could otherwise stall a session for ten minutes.
                     `curl -m 2` bounds the call itself so dead-board processes
                     cannot pile up either.
"""

import json
import os
import shlex
import sys

# Where the desktop companion listens unless told otherwise.
DESKTOP = "127.0.0.1:8765"

# Claude Code hook -> matcher -> the raw fact the board is told about.
# WORKING is inferred board-side as the span between prompt_submitted and
# turn_finished. BORED and ASLEEP cannot come from hooks at all: idle_prompt
# fires once and never repeats, and nothing fires when Claude Code is not
# running. Those are elapsed-time decay on the board.
MAPPING = [
    ("UserPromptSubmit",   "",                  "prompt_submitted"),
    ("Stop",               "",                  "turn_finished"),

    # The three ways Claude actually asks the human for something. All of them
    # fire the instant the request is made, with no delay:
    #
    #   PermissionRequest  - fires only when Claude Code is about to show a
    #                        permission dialog. The docs are explicit that this
    #                        is narrower than PreToolUse, which runs before
    #                        every tool call whether or not permission is
    #                        needed. No matcher: any tool asking counts.
    #   PreToolUse         - matches on tool name, so scoping it to the two
    #                        tools that exist to ask the user costs nothing on
    #                        any other tool call.
    ("PermissionRequest",  "",                            "needs_input"),
    ("PreToolUse",         "AskUserQuestion|ExitPlanMode", "needs_input"),

    # ...and the matching clear, so the red field goes away as soon as the
    # question is answered rather than hanging around until the turn ends.
    ("PostToolUse",        "AskUserQuestion|ExitPlanMode", "answered"),

    # Fires ~60 s after a turn ends with no reply. Kept as a plain idle signal;
    # map it to needs_input instead if you want the face to nag.
    ("Notification",       "idle_prompt",       "idle"),

    # The delayed permission notification. Redundant now that
    # PermissionRequest is wired, but harmless: it re-asserts the same state.
    ("Notification",       "permission_prompt", "needs_input"),

    ("PostToolUseFailure", "*",                 "error"),

    # StopFailure's matcher is the API error type. rate_limit is the five-hour
    # usage limit; the rest are ordinary failures. Splitting them means the
    # board can show something you cannot fix by answering a prompt.
    ("StopFailure",        "rate_limit",        "limit_reached"),
    ("StopFailure",        "overloaded|authentication_failed|billing_error|"
                           "oauth_org_not_allowed|invalid_request|"
                           "model_not_found|server_error|max_output_tokens|"
                           "unknown",           "error"),
    ("SessionStart",       "",                  "session_started"),
    ("SessionEnd",         "",                  "session_ended"),
]


def entry(host: str, token: str, event: str) -> dict:
    return {
        "type": "command",
        "command": "curl",
        "args": [
            "-s", "-m", "2", "-X", "POST",
            "-H", f"Authorization: Bearer {token}",
            f"http://{host}/e/{event}",
        ],
        "async": True,
        "timeout": 5,
    }


def statusline_command(host: str, token: str, windows: bool) -> str:
    """The status line has no exec form.

    Unlike hooks, statusLine takes a shell string rather than an argv array, so
    it cannot be made byte-identical across platforms. It also runs through a
    different shell on each: sh on Unix, Git Bash or PowerShell on Windows. So
    there are two scripts and two command lines.

    On Windows the path must use forward slashes and be double quoted. Git Bash
    treats unquoted backslashes as escapes and silently mangles the path;
    PowerShell is happy with forward slashes either way; and both honour double
    quotes, which matters the moment the path contains a space.
    """
    here = os.path.dirname(os.path.abspath(__file__))

    if windows:
        script = os.path.join(here, "statusline.ps1").replace("\\", "/")
        # Double quotes, because the path may contain spaces and they work in
        # both shells Claude Code might use here.
        return (
            "powershell -NoProfile -ExecutionPolicy Bypass "
            f'-File "{script}" {host} {token}'
        )

    script = shlex.quote(os.path.join(here, "statusline.py"))
    return f"python3 {script} {shlex.quote(host)} {shlex.quote(token)}"


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = {a for a in sys.argv[1:] if a.startswith("--")}
    if len(args) != 2 or flags - {"--windows", "--unix"}:
        print(__doc__.strip(), file=sys.stderr)
        return 2

    hosts_arg, token = args
    hosts = [DESKTOP if h == "desktop" else h
             for h in (h.strip() for h in hosts_arg.split(",")) if h]
    if not hosts:
        print("no host given", file=sys.stderr)
        return 2
    windows = "--windows" in flags or (
        "--unix" not in flags and sys.platform.startswith("win")
    )

    hooks: dict = {}
    for hook_name, matcher, event in MAPPING:
        hooks.setdefault(hook_name, []).append(
            {"matcher": matcher, "hooks": [entry(h, token, event) for h in hosts]}
        )

    # The status line is the only place Claude Code exposes rate-limit figures,
    # so the usage gauge depends on it. refreshInterval keeps the countdown
    # moving while you are idle, when no assistant message would trigger it.
    # Both status line scripts split the comma-separated list themselves.
    statusline = {
        "type": "command",
        "command": statusline_command(",".join(hosts), token, windows),
        "refreshInterval": 60,
    }

    print(json.dumps({"hooks": hooks, "statusLine": statusline}, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())

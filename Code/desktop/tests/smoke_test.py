#!/usr/bin/env python3
"""End-to-end check of the desktop companion, headless.

Usage: smoke_test.py <path to claude-monitor-companion>

Runs the real binary with SDL's dummy video and audio drivers, then talks to
it the way the hooks, the status line and the settings page do. Run by
`ctest`; also fine to run by hand.
"""

import json
import os
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

FAILURES = []


def check(ok, what):
    print(("ok    " if ok else "FAIL  ") + what)
    if not ok:
        FAILURES.append(what)


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Companion:
    def __init__(self, binary, data_dir, port):
        self.binary, self.data_dir, self.port = binary, data_dir, port
        self.base = f"http://127.0.0.1:{port}"
        self.proc = None

    def start(self):
        env = dict(os.environ, SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy")
        self.log = open(os.path.join(self.data_dir, "log.txt"), "ab")
        self.proc = subprocess.Popen(
            [self.binary, "--port", str(self.port), "--data-dir", self.data_dir,
             "--notify", "none"],
            stdin=subprocess.DEVNULL, stdout=self.log, stderr=subprocess.STDOUT, env=env)
        deadline = time.time() + 5
        while time.time() < deadline:
            try:
                self.get("/health")
                return
            except OSError:
                time.sleep(0.1)
        raise RuntimeError("companion did not start")

    def stop(self):
        self.proc.send_signal(signal.SIGTERM)
        try:
            return self.proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            return None
        finally:
            self.log.close()

    def request(self, path, method="GET", data=None, headers=None):
        req = urllib.request.Request(self.base + path, method=method,
                                     data=data, headers=headers or {})
        try:
            with urllib.request.urlopen(req, timeout=2) as r:
                return r.status, r.read()
        except urllib.error.HTTPError as e:
            return e.code, e.read()

    def get(self, path):
        return self.request(path)

    def event(self, name, token="change-me"):
        status, _ = self.request(f"/e/{name}", "POST", b"",
                                 {"Authorization": f"Bearer {token}"})
        time.sleep(0.12)  # applied on the next frame
        return status

    def config(self):
        return json.loads(self.get("/api/config")[1])

    def setting(self, key):
        for g in self.config()["groups"]:
            for f in g["fields"]:
                if f["key"] == key:
                    return f["value"]
        return None

    def mood(self):
        return json.loads(self.get("/health")[1])["mood"]


def main(binary):
    data_dir = tempfile.mkdtemp(prefix="cmc-smoke-")
    port = free_port()
    c = Companion(binary, data_dir, port)
    try:
        c.start()
        check(c.get("/health")[0] == 200, "starts and answers /health")
        check(c.get("/")[0] == 200, "serves the settings page")

        check(c.setting("error_sound") == 0, "first run: failures are silent")
        check((c.setting("eye_w"), c.setting("eye_gap"), c.setting("eye_h"), c.setting("eye_center_y"))
              == (20, 120, 90, 100), "first run: eyes start where the mascot has them")
        check(c.setting("bl_normal") == 255, "first run: full brightness, for the mascot's colour")
        check(c.setting("alert_sound") == 3, "first run: needs-input sound kept (bell and buzzer)")
        check(c.setting("done_sound") == 2, "first run: finished sound kept (buzzer)")

        # The same sequence the guided tour plays, with what the board's mood
        # engine makes of each event.
        expected = [
            ("session_started", "CHILL"), ("prompt_submitted", "WORKING"),
            ("needs_input", "EXCITED"), ("answered", "WORKING"),
            ("turn_finished", "EXCITED"), ("prompt_submitted", "WORKING"),
            ("error", "CONFUSED"), ("limit_reached", "LIMIT"),
            ("prompt_submitted", "WORKING"), ("turn_finished", "EXCITED"),
            ("idle", "CHILL"), ("session_ended", "CHILL"),
        ]
        for name, mood in expected:
            status = c.event(name)
            got = c.mood()
            check(status == 200 and got == mood, f"{name} -> {mood} (got {status} {got})")

        c.event("needs_input")
        check(c.config()["device"]["alerting"] is True, "needs_input turns the field red")

        check(c.event("needs_input", token="wrong") == 401, "wrong token is refused")
        check(c.event("no_such_event") == 404, "unknown event is a 404, not silence")

        reset = int(time.time()) + 3600
        status, _ = c.request(f"/limits?pct=42&reset={reset}&wpct=17&wreset={reset + 86400}",
                              "POST", b"", {"Authorization": "Bearer change-me"})
        dev = c.config()["device"]
        check(status == 200 and dev["limit_pct"] == 42 and dev["weekly_pct"] == 17,
              "status line figures reach the gauges")

        origin = f"http://127.0.0.1:{port}"
        status, _ = c.request("/api/config", "POST", b"eye_w=45",
                              {"Origin": origin,
                               "Content-Type": "application/x-www-form-urlencoded"})
        check(status == 200 and c.setting("eye_w") == 45, "the settings page can save")
        status, _ = c.request("/api/config", "POST", b"eye_w=50",
                              {"Origin": "https://example.com",
                               "Content-Type": "application/x-www-form-urlencoded"})
        check(status == 403 and c.setting("eye_w") == 45, "another site cannot change settings")
        status, _ = c.request("/api/config", headers={"Host": f"rebind.example:{port}"})
        check(status == 403, "a rebound host name is refused")

        status, _ = c.request("/api/reset", "POST", b"", {"Origin": origin})
        check(status == 200 and c.setting("error_sound") == 0 and c.setting("eye_w") == 20
              and c.setting("eye_gap") == 120,
              "factory reset keeps the desktop's defaults")
        # The user's own choice, which a restart must not overwrite.
        c.request("/api/config", "POST", b"eye_w=45&error_sound=2",
                  {"Origin": origin, "Content-Type": "application/x-www-form-urlencoded"})

        second = subprocess.run(
            [binary, "--port", str(port), "--data-dir", data_dir],
            env=dict(os.environ, SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy"),
            stdin=subprocess.DEVNULL, capture_output=True, timeout=5)
        check(second.returncode == 1 and b"already in use" in second.stderr,
              "a second copy exits with a message")

        shot = os.path.join(data_dir, "screenshot.png")
        c.proc.send_signal(signal.SIGUSR1)
        for _ in range(30):
            if os.path.exists(shot):
                break
            time.sleep(0.1)
        size = None
        if os.path.exists(shot):
            with open(shot, "rb") as f:
                head = f.read(24)
            if head[:8] == b"\x89PNG\r\n\x1a\n":
                size = struct.unpack(">II", head[16:24])
        check(size == (240, 280), f"SIGUSR1 saves a 240x280 PNG of the panel (got {size})")

        check(c.stop() == 0, "SIGTERM exits cleanly")

        c.start()
        check(c.setting("eye_w") == 45, "settings survive a restart")
        check(c.setting("error_sound") == 2, "a restart keeps the user's sound choice")
        c.stop()
    finally:
        if c.proc and c.proc.poll() is None:
            c.proc.kill()
        if FAILURES:
            print(f"\nlog and data kept in {data_dir}")
        else:
            shutil.rmtree(data_dir, ignore_errors=True)

    print("FAILED: %d" % len(FAILURES) if FAILURES else "all passed")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))

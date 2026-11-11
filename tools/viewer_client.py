#!/usr/bin/env python3
"""A client for HWVX-SDL's viewer mode (src/port/viewer.c, docs: viewer-protocol.md).

As a module: Viewer.start(...) runs the game with HWVX_VIEWER=0 and connects; v.call("load",
archive="HW_Cars", type="car", id=5) sends one request and returns its reply (ViewerError when
the game answers ok: false); v.close() force-kills the game.

As a tool, one game run and a few requests, each reply printed as JSON:

    viewer_client.py --build build --data orig/files [--out build/viewer-out] REQUEST...
    viewer_client.py --port 5599 REQUEST...            a viewer that is already running

A REQUEST is a command and key=value arguments in one shell word, values read as JSON where
they parse: 'load archive=HW_Cars type=car id=5'  'get node=CarAInfo/0'
'set node=CarAInfo/0 field=maxRpm value=9000'  'screenshot path=/abs/shot.bmp'.
"""
import argparse
import json
import os
import re
import shlex
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent


class ViewerError(Exception):
    pass


class Viewer:
    def __init__(self, sock, proc=None, log=None):
        self.sock = sock
        self.proc = proc
        self.log = log
        self.buf = b""
        self.next_id = 1
        self.events = []

    @classmethod
    def connect(cls, port, host="127.0.0.1", timeout=30.0):
        s = socket.create_connection((host, port), timeout=timeout)
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        return cls(s)

    @classmethod
    def start(cls, build, data, out, renderer=None, window="640x480", schemas=None, env=None, wait=60.0):
        """Run BUILD/hwvx in viewer mode on DATA; its preferences folder and log go under OUT."""
        out = Path(out).resolve()
        pref = out / "pref"
        pref.mkdir(parents=True, exist_ok=True)
        log = out / "viewer.log"
        e = dict(os.environ)
        e.update({
            "HWVX_VIEWER": "0",                     # any free port: read back from the log
            "HWVX_VIEWER_SCHEMAS": str(schemas or HERE.parent / "schemas"),
            "HWVX_PREF_DIR": str(pref),             # never the player's own files
            "HWVX_NO_DIALOG": "1",
            "HWVX_NO_MOVIE": "1",
            "HWVX_DETERMINISTIC": "1",
        })
        e.update(env or {})
        cmd = [str(Path(build) / "hwvx"), "--windowed", "--window", window, "--data", str(data)]
        if renderer:
            cmd += ["--renderer", renderer]
        f = open(log, "wb")
        proc = subprocess.Popen(cmd, stdout=f, stderr=subprocess.STDOUT, env=e)
        f.close()
        port = None
        end = time.time() + wait
        while time.time() < end and port is None:
            if proc.poll() is not None:
                raise ViewerError(f"the game exited ({proc.returncode}) before listening:\n" + log.read_text(errors="replace")[-2000:])
            m = re.search(r"viewer: listening on 127\.0\.0\.1:(\d+)", log.read_text(errors="replace"))
            if m:
                port = int(m.group(1))
            else:
                time.sleep(0.05)
        if port is None:
            proc.kill()
            raise ViewerError("the game did not start listening:\n" + log.read_text(errors="replace")[-2000:])
        v = cls.connect(port)
        v.proc, v.log, v.port = proc, log, port
        return v

    def call(self, cmd, timeout=60.0, **args):
        rid = self.next_id
        self.next_id += 1
        req = {"id": rid, "cmd": cmd}
        req.update(args)
        self.sock.sendall(json.dumps(req).encode() + b"\n")
        self.sock.settimeout(timeout)
        while True:
            while b"\n" not in self.buf:
                chunk = self.sock.recv(1 << 16)
                if not chunk:
                    raise ViewerError(f"the viewer closed the connection during {cmd}")
                self.buf += chunk
            line, self.buf = self.buf.split(b"\n", 1)
            msg = json.loads(line)
            if "event" in msg:
                self.events.append(msg)
                continue
            if msg.get("id") != rid:
                continue
            if not msg.get("ok"):
                raise ViewerError(msg.get("error", "failed"))
            return msg

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass
        if self.proc is not None and self.proc.poll() is None:
            self.proc.send_signal(signal.SIGKILL)     # always force-killed, never left running
            self.proc.wait()


def parse_request(word):
    parts = shlex.split(word)
    args = {}
    for p in parts[1:]:
        k, _, v = p.partition("=")
        try:
            args[k] = json.loads(v)
        except ValueError:
            args[k] = v
    return parts[0], args


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", default="build")
    ap.add_argument("--data")
    ap.add_argument("--out", default="build/viewer-out")
    ap.add_argument("--port", type=int)
    ap.add_argument("--renderer")
    ap.add_argument("--window", default="640x480")
    ap.add_argument("--brief", action="store_true", help="print long lists as their length")
    ap.add_argument("requests", nargs="*")
    a = ap.parse_args()
    if a.port:
        v = Viewer.connect(a.port)
    elif a.data:
        v = Viewer.start(a.build, a.data, a.out, renderer=a.renderer, window=a.window)
    else:
        ap.error("--data (to run the game) or --port (a running viewer)")
    rc = 0
    try:
        for word in a.requests:
            cmd, args = parse_request(word)
            try:
                r = v.call(cmd, **args)
                if a.brief:
                    r = {k: (f"<{len(x)} entries>" if isinstance(x, list) and len(x) > 8 else x) for k, x in r.items()}
                print(json.dumps(r, indent=1))
            except ViewerError as e:
                print(f"{cmd}: {e}", file=sys.stderr)
                rc = 1
    finally:
        if not a.port:
            v.close()
    return rc


if __name__ == "__main__":
    sys.exit(main())

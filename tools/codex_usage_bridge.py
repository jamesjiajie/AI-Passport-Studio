#!/usr/bin/env python3
"""Send Codex account quota windows to AI Passport over USB."""

from __future__ import annotations

import argparse
import glob
import json
import os
import select
import shutil
import subprocess
import time
from datetime import datetime


def find_codex():
    cli = shutil.which("codex")
    if cli:
        return cli
    bundled = "/Applications/ChatGPT.app/Contents/Resources/codex-cli/CodexCLI.app/Contents/MacOS/codex"
    return bundled if os.path.isfile(bundled) and os.access(bundled, os.X_OK) else None


def normalize_limits(result: dict, now: int) -> dict:
    if not isinstance(result, dict):
        raise ValueError("invalid Codex quota response")
    buckets = result.get("rateLimitsByLimitId") or {}
    if not isinstance(buckets, dict):
        raise ValueError("invalid Codex quota buckets")
    snapshot = buckets.get("codex") or result.get("rateLimits") or {}
    if not isinstance(snapshot, dict):
        raise ValueError("invalid Codex quota window")
    windows = {}
    for key in ("primary", "secondary"):
        window = snapshot.get(key)
        if not isinstance(window, dict):
            continue
        duration = window.get("windowDurationMins")
        used = window.get("usedPercent")
        reset = window.get("resetsAt")
        if duration not in (300, 10080) or type(used) is not int or not 0 <= used <= 100:
            continue
        if reset is not None and (type(reset) is not int or reset <= 0):
            continue
        windows["five_hour" if duration == 300 else "weekly"] = {
            "used_percent": used,
            "resets_at": reset,
            "reset_local": datetime.fromtimestamp(reset).strftime("%m-%d %H:%M") if reset else "--",
        }
    if not windows:
        raise ValueError("Codex quota windows unavailable")
    return {"version": 1, "updated_at": now,
            "updated_local": datetime.fromtimestamp(now).strftime("%m-%d %H:%M"),
            "five_hour": windows.get("five_hour"), "weekly": windows.get("weekly")}


def read_limits(codex: str, timeout: float = 20) -> dict:
    process = subprocess.Popen(
        [codex, "app-server", "--stdio"], stdin=subprocess.PIPE,
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, bufsize=1,
    )

    def request(message: dict) -> None:
        assert process.stdin is not None
        process.stdin.write(json.dumps(message, separators=(",", ":")) + "\n")
        process.stdin.flush()

    def response(request_id: int) -> dict:
        assert process.stdout is not None
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if not select.select([process.stdout], [], [], max(0, deadline - time.monotonic()))[0]:
                break
            line = process.stdout.readline()
            if not line:
                break
            message = json.loads(line)
            if message.get("id") == request_id:
                if "error" in message:
                    raise RuntimeError("Codex app-server returned an error")
                return message["result"]
        raise TimeoutError("Codex app-server did not respond")

    try:
        request({"id": 1, "method": "initialize", "params": {
            "clientInfo": {"name": "ai-passport-bridge", "version": "1"}}})
        response(1)
        request({"method": "initialized", "params": {}})
        request({"id": 2, "method": "account/rateLimits/read", "params": {}})
        return normalize_limits(response(2), int(time.time()))
    finally:
        if process.poll() is None:
            process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def send_usb(fd: int, snapshot: dict) -> None:
    data = (json.dumps(snapshot, separators=(",", ":")) + "\n").encode()
    if len(data) > 513:
        raise ValueError("quota snapshot exceeds USB frame limit")
    deadline = time.monotonic() + 3
    while data:
        remaining = deadline - time.monotonic()
        if remaining <= 0 or not select.select([], [fd], [], remaining)[1]:
            raise TimeoutError("USB write timed out")
        count = os.write(fd, data)
        if count <= 0:
            raise OSError("USB write failed")
        data = data[count:]


def usb_session(fd: int, codex: str) -> None:
    next_update = 0.0
    ack_deadline = 0.0
    pending = bytearray()
    while True:
        if time.monotonic() >= next_update:
            try:
                snapshot = read_limits(codex)
            except (OSError, ValueError, RuntimeError, TimeoutError):
                send_usb(fd, {"error": "unavailable"})
                print("Codex quota unavailable; retrying in 15 seconds.", flush=True)
                next_update = time.monotonic() + 15
                ack_deadline = 0
            else:
                send_usb(fd, snapshot)
                ack_deadline = time.monotonic() + 5
                next_update = time.monotonic() + 60
        if ack_deadline and time.monotonic() > ack_deadline:
            raise RuntimeError("Device did not acknowledge quota; install the USB firmware")
        if not select.select([fd], [], [], 0.25)[0]:
            continue
        data = os.read(fd, 4096)
        if not data:
            raise OSError("USB device disconnected")
        pending.extend(data)
        while b"\n" in pending:
            line, _, rest = pending.partition(b"\n")
            pending = bytearray(rest)
            line = line.strip()
            if line == b"MONITOR_USB_OK":
                ack_deadline = 0
                print("USB quota delivered.", flush=True)
            elif line == b"MONITOR_USB_REFRESH":
                next_update = 0
            elif line == b"MONITOR_USB_ERROR" and ack_deadline:
                raise RuntimeError("Device rejected the quota snapshot")
        if len(pending) > 4096:
            pending.clear()


def stream_usb(port: str, codex: str) -> None:
    import termios
    import tty
    waiting = False
    while True:
        try:
            path = port
            if port == "auto":
                ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
                if not ports:
                    raise FileNotFoundError("no USB device")
                if len(ports) != 1:
                    raise ValueError("multiple USB devices; choose one with --serial <port>")
                path = ports[0]
            fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            try:
                tty.setraw(fd)
                settings = termios.tcgetattr(fd)
                settings[4] = settings[5] = termios.B115200
                settings[2] |= termios.CLOCAL | termios.CREAD
                termios.tcsetattr(fd, termios.TCSANOW, settings)
                # Synchronize newline framing after an interrupted transfer.
                os.write(fd, b"\n")
                print(f"USB connected: {path}; updates every 60 seconds.", flush=True)
                waiting = False
                usb_session(fd, codex)
            finally:
                os.close(fd)
        except OSError:
            if not waiting:
                print("Waiting for USB device; reconnect the data cable.", flush=True)
                waiting = True
            time.sleep(2)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", default="auto", help="USB port; auto-detect by default")
    args = parser.parse_args()
    codex = find_codex()
    if not codex:
        parser.error("codex CLI is unavailable")
    try:
        stream_usb(args.serial, codex)
    except KeyboardInterrupt:
        pass
    except (ImportError, ValueError, RuntimeError, TimeoutError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()

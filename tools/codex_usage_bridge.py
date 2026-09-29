#!/usr/bin/env python3
"""Serve Codex account quota windows to one AI Passport on a trusted LAN."""

from __future__ import annotations

import argparse
import hmac
import json
import os
import secrets
import select
import shutil
import subprocess
import time
from datetime import datetime
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOKEN_FILE = ROOT / "build" / "monitor-token"


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


def load_token(path: Path) -> str:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink():
        raise ValueError("bridge token file must not be a symlink")
    try:
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except FileExistsError:
        pass
    else:
        with os.fdopen(fd, "w") as stream:
            stream.write(secrets.token_urlsafe(24) + "\n")
    token = path.read_text(encoding="ascii").strip()
    if len(token) < 24 or path.stat().st_mode & 0o077:
        raise ValueError("bridge token file is invalid")
    return token


def make_handler(token: str, codex: str):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self) -> None:
            if self.path != "/usage":
                self.send_error(404)
                return
            if not hmac.compare_digest(self.headers.get("X-Passport-Token", ""), token):
                self.send_error(403)
                return
            try:
                body = json.dumps(read_limits(codex), separators=(",", ":")).encode()
                status = 200
            except (OSError, ValueError, RuntimeError, TimeoutError):
                body = b'{"error":"unavailable"}'
                status = 503
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    return Handler


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8787)
    args = parser.parse_args()
    codex = shutil.which("codex")
    if not codex:
        parser.error("codex CLI is unavailable")
    token = load_token(TOKEN_FILE)
    print(f"Serving /usage on {args.bind}:{args.port}; token file: {TOKEN_FILE}", flush=True)
    try:
        HTTPServer((args.bind, args.port), make_handler(token, codex)).serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()

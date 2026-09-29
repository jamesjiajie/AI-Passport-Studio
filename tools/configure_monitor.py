#!/usr/bin/env python3
"""Send Wi-Fi and bridge settings to a flashed AI Passport over USB serial."""

import argparse
import getpass
import json
import time

from codex_usage_bridge import TOKEN_FILE


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB Serial/JTAG port, for example /dev/cu.usbmodem...")
    args = parser.parse_args()
    try:
        import serial
    except ImportError:
        parser.error("pyserial is required; activate the ESP-IDF environment")
    ssid = input("2.4 GHz Wi-Fi SSID: ").strip()
    password = getpass.getpass("Wi-Fi password: ")
    url = input("Bridge URL (http://LAN-IP:8787/usage): ").strip()
    if not ssid or not url.startswith("http://") or not url.endswith("/usage"):
        parser.error("SSID and a bridge /usage URL are required")
    token = TOKEN_FILE.read_text(encoding="ascii").strip()
    payload = json.dumps({"ssid": ssid, "password": password,
                          "url": url, "token": token}, separators=(",", ":"))
    with serial.Serial(args.port, 115200, timeout=1) as device:
        time.sleep(2)
        device.reset_input_buffer()
        device.write((payload + "\n").encode("utf-8"))
        device.flush()
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            line = device.readline().decode("utf-8", errors="replace").strip()
            if line == "MONITOR_CONFIG_OK":
                print("Configuration saved; device is restarting.")
                return
            if line == "MONITOR_CONFIG_ERROR":
                parser.error("device rejected configuration")
    parser.error("no configuration acknowledgment from device")


if __name__ == "__main__":
    main()

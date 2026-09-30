<p align="right"><a href="codex-monitor.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Codex quota monitor

The AI Passport displays Codex account five-hour and weekly **quota remaining percentages and reset times**. These are quota windows, not raw token counts or API billing figures. A signed-in computer sends snapshots over a USB data cable; Codex credentials stay on the computer.

## Build and run

1. Activate ESP-IDF 5.5.3 and run `./tools/validate.sh`. Flash the verified `build/FoloToy-AI-Passport-full.bin` at `0x0` only after choosing the device and approving the write. A merged flash can reset stored data.
2. Connect the device using a USB data cable and close other serial monitors.
3. On the signed-in Mac, run `/usr/bin/python3 tools/codex_usage_bridge.py` from the repository root. The script detects a single `/dev/cu.usbmodem*` port on macOS or `/dev/ttyACM*` on Linux. With multiple devices, use `--serial <port>` to select the intended one.

Keep the bridge running for updates. `USB connected` means the port is open; `USB quota delivered` confirms that firmware accepted a snapshot. The bridge uses Python's standard library on macOS/Linux. It finds `codex` on `PATH` or inside `/Applications/ChatGPT.app` when available. A missing login or unavailable quota is reported as an error, never as zero usage.

## Device behavior and protocol

Both percentages and bars show remaining quota, matching Codex Usage remaining. The USB snapshot carries `used_percent`; firmware validates it and displays `100 - used_percent`. Thus 69%/64% used becomes 31%/36% remaining. Unavailable windows display `--`.

The computer sends a snapshot every 60 seconds. OK requests an immediate refresh, UP shows the overview, and DOWN shows connection details. The device retains the last good reading in RAM after an invalid or unavailable response. It marks that reading offline after 120 seconds without a successful snapshot. Times use the computer's local timezone.

Each USB frame is compact JSON terminated by a newline, at most 512 bytes excluding the newline. Firmware acknowledges accepted data with `MONITOR_USB_OK`, rejects invalid data with `MONITOR_USB_ERROR`, and sends `MONITOR_USB_REFRESH` when OK is pressed. Empty lines resynchronize framing. Oversized frames and control characters are rejected. Codex account access uses the local CLI app-server `account/rateLimits/read` protocol, tested against Codex CLI 0.158.0-alpha.2.1.

## Validation

The 2026-09-30 remaining-quota build passed the complete gate and archive verification: merged SHA-256 `9192808e7b9435a4ab1320cba77ebce0289f55718e23cccaa2445b1706486259`, matching ELF SHA-256 `c6b3e412a9e6cba4bf15eee38f2a9181f7468457bbdbb51183b3198c763f8759`. The verified bundle is in `build/firmware/<merged-sha256>/`.

The image was flashed from 0x0 with esptool data-hash verification. Startup matched the ELF, initialized display/LVGL/battery/buttons, and emitted `MONITOR_USB_READY`. The bridge received `MONITOR_USB_OK` for real quota, and the user confirmed that remaining percentages match Codex Usage remaining and Chinese labels render correctly. Host checks cover quota normalization, USB snapshot/acknowledgment/refresh/disconnect exchange, framing, overflow recovery, stale timing, JSON validation, and Chinese glyph coverage. Physical cable recovery, the 120-second offline transition, prolonged runtime, and memory headroom remain unverified.

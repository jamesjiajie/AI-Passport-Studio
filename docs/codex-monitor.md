<p align="right"><a href="codex-monitor.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Codex quota monitor

This application displays the Codex account's five-hour and weekly **quota used percentages and reset times**. These are not raw token counts or OpenAI API billing figures. A computer signed in to Codex runs the bridge; the Passport reads only the bridge's read-only snapshot over a trusted local Wi-Fi network.

## Build and run

1. Activate ESP-IDF 5.5.3 and run `./tools/validate.sh`. Flash the verified `build/FoloToy-AI-Passport-full.bin` at `0x0` only after choosing the target device and approving the write. A merged flash can reset stored data.
2. On the signed-in computer, run `python3 tools/codex_usage_bridge.py`. The bridge creates a private read-only token in `build/monitor-token` and listens on port 8787. Allow the port through the computer firewall on the trusted LAN.
3. Connect the flashed device over USB and run `python3 tools/configure_monitor.py <serial-port>` in the activated ESP-IDF Python environment. Enter the 2.4 GHz Wi-Fi SSID, password, and `http://<computer-LAN-IP>:8787/usage`. This one-time setup stores Wi-Fi settings, the URL, and the read-only bridge token in the device's NVS, then restarts it. Re-run the command to change settings.

The computer must keep the bridge running for live updates. The device refreshes every 60 seconds; OK requests an immediate refresh. UP shows the quota overview, DOWN shows connection details. The device retains the last good reading in RAM and marks the connection offline when a later request fails. The bridge returns no Codex login credentials or account identifiers. The local HTTP link and NVS are not encrypted; use a trusted Wi-Fi network and do not expose port 8787 to the Internet.

The bridge speaks the local Codex CLI app-server `account/rateLimits/read` protocol. It was tested against Codex CLI 0.158.0-alpha.2.1; the protocol may change with Codex updates. Missing usage remains unavailable rather than displaying zero. Display times use the computer's local timezone.

The on-device checks still required are Wi-Fi connection and recovery, USB configuration, button actions, percentage and reset-time rendering (including Chinese glyphs), stale/offline display, battery display, and memory headroom while Wi-Fi and LVGL are active. A successful build does not establish those results.

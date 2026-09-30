import unittest
from unittest.mock import patch
import json
import os
import pty
import select
import threading
import tty

from tools.codex_usage_bridge import find_codex, normalize_limits, usb_session


class BridgeTest(unittest.TestCase):
    def test_usb_frames_refresh_and_disconnect(self):
        master, slave = pty.openpty()
        tty.setraw(slave)
        os.set_blocking(slave, False)
        snapshot = {"version": 1, "updated_local": "09-30 16:00",
                    "five_hour": {"used_percent": 54, "reset_local": "09-30 17:06"},
                    "weekly": None}
        failures = []

        def run():
            try:
                usb_session(slave, "unused")
            except OSError:
                pass  # Closing the emulated USB cable must end this session.
            except Exception as error:
                failures.append(error)

        def frame():
            data = b""
            while b"\n" not in data:
                self.assertTrue(select.select([master], [], [], 3)[0], "no USB frame")
                data += os.read(master, 4096)
            return json.loads(data)

        with patch("tools.codex_usage_bridge.read_limits", return_value=snapshot) as read:
            worker = threading.Thread(target=run, daemon=True)
            worker.start()
            try:
                self.assertEqual(frame(), snapshot)
                os.write(master, b"MONITOR_USB_OK\nMONITOR_USB_REFRESH\n")
                self.assertEqual(frame(), snapshot)
                self.assertEqual(read.call_count, 2)
            finally:
                os.close(master)
                worker.join(3)
                os.close(slave)
            self.assertFalse(worker.is_alive(), "USB disconnect did not end session")
            self.assertFalse(failures, failures)

    def test_cli_discovery_without_terminal_path(self):
        with patch("tools.codex_usage_bridge.shutil.which", return_value=None), \
                patch("tools.codex_usage_bridge.os.path.isfile", return_value=True), \
                patch("tools.codex_usage_bridge.os.access", return_value=True):
            self.assertTrue(find_codex().endswith("CodexCLI.app/Contents/MacOS/codex"))
        with patch("tools.codex_usage_bridge.shutil.which", return_value="/usr/local/bin/codex"):
            self.assertEqual(find_codex(), "/usr/local/bin/codex")
        with patch("tools.codex_usage_bridge.shutil.which", return_value=None), \
                patch("tools.codex_usage_bridge.os.path.isfile", return_value=False):
            self.assertIsNone(find_codex())

    def test_windows_and_missing_data(self):
        result = {"rateLimitsByLimitId": {"codex": {
            "primary": {"windowDurationMins": 300, "usedPercent": 77,
                        "resetsAt": 1790631016},
            "secondary": {"windowDurationMins": 10080, "usedPercent": 38,
                          "resetsAt": 1791092868},
        }}}
        output = normalize_limits(result, 123)
        self.assertEqual(output["five_hour"]["used_percent"], 77)
        self.assertEqual(output["weekly"]["resets_at"], 1791092868)
        self.assertEqual(output["updated_at"], 123)
        result["rateLimitsByLimitId"]["codex"]["secondary"] = None
        self.assertIsNone(normalize_limits(result, 123)["weekly"])

    def test_invalid_percentage_is_not_published(self):
        with self.assertRaises(ValueError):
            normalize_limits({"rateLimits": {"primary": {
                "windowDurationMins": 300, "usedPercent": 101,
                "resetsAt": 1790631016}}}, 123)
        with self.assertRaises(ValueError):
            normalize_limits({"rateLimitsByLimitId": ["bad"]}, 123)


if __name__ == "__main__":
    unittest.main()

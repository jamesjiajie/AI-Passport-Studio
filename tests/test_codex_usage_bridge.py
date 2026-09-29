import unittest

from tools.codex_usage_bridge import normalize_limits


class BridgeTest(unittest.TestCase):
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

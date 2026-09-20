"""Static contract checks for the isolated Wi-Fi setup portal."""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src/wifi_manager.c").read_text()


class WifiPortalContractTests(unittest.TestCase):
    def test_all_portal_routes_refresh_inactivity_window(self):
        for handler in ("portal_get", "portal_scan", "portal_status", "portal_redirect", "portal_post"):
            start = SOURCE.index("static esp_err_t " + handler)
            end = SOURCE.find("\nstatic esp_err_t ", start + 1)
            body = SOURCE[start:] if end < 0 else SOURCE[start:end]
            self.assertIn("portal_activity();", body, handler)

    def test_status_does_not_include_stored_secrets(self):
        start = SOURCE.index("static esp_err_t portal_status")
        end = SOURCE.index("static esp_err_t portal_redirect", start)
        body = SOURCE[start:end]
        self.assertNotIn("wifi_password", body)
        self.assertNotIn("mqtt_password", body)

    def test_timeout_is_activity_based(self):
        self.assertIn("s_ap_last_activity_us", SOURCE)
        self.assertIn("idle_us >= (int64_t)AP_IDLE_TIMEOUT_MS * 1000LL", SOURCE)


if __name__ == "__main__":
    unittest.main()

"""Static contract checks for the isolated Wi-Fi setup portal."""
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src/setup_portal.c").read_text()
WIFI = (ROOT / "src/wifi_manager.c").read_text()
HANDLERS = ("portal_get", "portal_scan", "portal_status", "portal_redirect", "portal_post", "portal_reset")


def _function(signature: str, source: str = SOURCE) -> str:
    """Return one top-level function, from its signature to its closing brace."""
    start = source.index(signature)
    return source[start:source.index("\n}\n", start) + 3]


def _handler(name: str) -> str:
    return _function(f"static esp_err_t {name}(httpd_req_t *req)")


class WifiPortalContractTests(unittest.TestCase):
    def test_all_portal_routes_refresh_inactivity_window(self):
        for handler in HANDLERS:
            self.assertIn("portal_activity();", _handler(handler), handler)

    def test_all_portal_routes_are_limited_to_the_setup_ap(self):
        for handler in HANDLERS:
            first = _handler(handler).split("{", 1)[1].strip().splitlines()[0]
            self.assertEqual(first, "if (!portal_request_from_ap(req)) return portal_forbidden(req);", handler)

    def test_ap_password_is_not_derived_from_the_mac(self):
        start = WIFI.index("esp_err_t wifi_manager_start_ap(void)")
        self.assertNotIn("mac[", WIFI[start:].split("config.ap.password", 1)[1].split(";", 1)[0])
        credentials = WIFI[WIFI.index("esp_err_t wifi_manager_get_ap_credentials("):]
        self.assertNotIn("mac[2]", credentials)

    def test_idle_timeout_keeps_the_ap_of_an_unprovisioned_panel(self):
        self.assertIn("if (s_ap_active && !s_provisioning_mode) {", WIFI)

    def test_portal_never_logs_the_broker_uri(self):
        self.assertNotIn("'%s'\", mqtt_uri", SOURCE)

    def test_stored_mqtt_password_requires_unchanged_trust(self):
        post = _handler("portal_post")
        self.assertIn("if (!has_mqtt_password && same_broker_trust)", post)
        trust = post[post.index("const bool same_broker_trust"):post.index("if (!has_password")]
        for term in ("same_wifi", "current->mqtt_uri", "current->mqtt_username",
                     "current->mqtt_ca_certificate", "current->mqtt_require_tls"):
            self.assertIn(term, trust)

    def test_password_escaping_and_form_fields(self):
        """Compile the portal's pure helpers and exercise them on the host."""
        helpers = _function("static void ap_password_generate(void)", WIFI) + "".join(_function(name) for name in (
            "static void html_escape(",
            "static int hex_digit(", "static void form_url_decode(", "static bool form_field("))
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#define ESP_OK 0
#define AP_PASSWORD_LEN 14
static char s_ap_password[AP_PASSWORD_LEN + 1];
static unsigned random_calls;
static void esp_fill_random(void *buffer, size_t length) {
    /* Start in the rejected tail, then walk the whole accepted range. */
    static uint8_t next = 248;
    assert(length == 1); *(uint8_t *)buffer = next; next = (uint8_t)(next + 37); ++random_calls;
}
static size_t strlcpy(char *dest, const char *source, size_t size) {
    const size_t length = strlen(source);
    if (size) { const size_t n = length < size - 1 ? length : size - 1; memcpy(dest, source, n); dest[n] = 0; }
    return length;
}
/* Same contract as ESP-IDF: exact key match, raw value, error when it does not fit. */
static int httpd_query_key_value(const char *query, const char *key, char *value, size_t size) {
    const size_t key_length = strlen(key);
    while (*query) {
        const char *end = strchr(query, '&');
        const size_t length = end ? (size_t)(end - query) : strlen(query);
        if (length > key_length && query[key_length] == '=' && strncmp(query, key, key_length) == 0) {
            const size_t value_length = length - key_length - 1;
            if (value_length >= size) return 1;
            memcpy(value, query + key_length + 1, value_length); value[value_length] = 0;
            return ESP_OK;
        }
        if (!end) break;
        query = end + 1;
    }
    return 1;
}
''' + helpers + r'''
int main(void) {
    ap_password_generate();
    assert(strlen(s_ap_password) == AP_PASSWORD_LEN && random_calls > 12);
    for (size_t i = 0; i < AP_PASSWORD_LEN; ++i) {
        if (i == 4 || i == 9) assert(s_ap_password[i] == '-');
        else assert(strchr("abcdefghjkmnpqrstuvwxyz23456789", s_ap_password[i]) != NULL);
    }
    char first[AP_PASSWORD_LEN + 1];
    strcpy(first, s_ap_password);
    ap_password_generate();
    assert(strcmp(first, s_ap_password) == 0);

    /* A heap buffer must be escaped with its real size, not sizeof(pointer). */
    const size_t size = 64;
    char *escaped = malloc(size);
    html_escape("-----BEGIN <CERTIFICATE>-----", escaped, size);
    assert(strcmp(escaped, "-----BEGIN &lt;CERTIFICATE&gt;-----") == 0);
    html_escape("\"&'", escaped, 8);
    assert(strlen(escaped) < 8);
    free(escaped);

    const char *body = "ssid=Home+Net&password=&ca_certificate=%2D%2D%2B%2F%0D%0A&name=abcdefgh&mqtt_tls=ON";
    char scratch[128], value[16];
    bool too_long = false;
    assert(form_field(body, "ssid", value, sizeof(value), scratch, sizeof(scratch), &too_long));
    assert(strcmp(value, "Home Net") == 0 && !too_long);
    /* An encoded value longer than the destination still fits once decoded. */
    assert(form_field(body, "ca_certificate", value, sizeof(value), scratch, sizeof(scratch), &too_long));
    assert(strcmp(value, "--+/\r\n") == 0 && !too_long);
    assert(form_field(body, "password", value, sizeof(value), scratch, sizeof(scratch), &too_long));
    assert(value[0] == 0 && !too_long);
    assert(!form_field(body, "mqtt_password", value, sizeof(value), scratch, sizeof(scratch), &too_long) && !too_long);
    /* A value that does not fit is reported, never silently dropped. */
    assert(form_field(body, "name", value, 8, scratch, sizeof(scratch), &too_long));
    assert(too_long && value[0] == 0);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            code = Path(directory) / "portal.c"
            code.write_text(harness)
            executable = Path(directory) / "portal"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(code), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

    def test_status_does_not_include_stored_secrets(self):
        start = SOURCE.index("static esp_err_t portal_status")
        end = SOURCE.index("static esp_err_t portal_redirect", start)
        body = SOURCE[start:end]
        self.assertNotIn("wifi_password", body)
        self.assertNotIn("mqtt_password", body)

    def test_timeout_is_activity_based(self):
        self.assertIn("wifi_manager_ap_activity();", _function("static void portal_activity(void)"))
        self.assertIn("s_ap_last_activity_us", _function("void wifi_manager_ap_activity(void)", WIFI))
        self.assertIn("idle_us >= (int64_t)AP_IDLE_TIMEOUT_MS * 1000LL", WIFI)


if __name__ == "__main__":
    unittest.main()

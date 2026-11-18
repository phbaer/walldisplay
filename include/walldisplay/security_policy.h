#pragma once
#include <stdbool.h>
bool screenshot_authorized(const char *expected_token, const char *authorization);
bool mqtt_replayed_command(const char *base, const char *topic, bool retained);
bool json_depth_safe(const char *text);
/** True for a complete HTTPS URL: a host, then a non-empty path. Rejects a bare
 * scheme, an empty host, whitespace or control characters, and other schemes. */
bool update_manifest_url_valid(const char *url);

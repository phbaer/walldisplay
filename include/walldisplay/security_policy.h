#pragma once
#include <stdbool.h>
bool screenshot_authorized(const char *expected_token, const char *authorization);
bool mqtt_replayed_command(const char *base, const char *topic, bool retained);
bool json_depth_safe(const char *text);

#include "walldisplay/security_policy.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>
int main(void) {
    assert(json_depth_safe("{\"label\":\"[[[[[[[[[[[[[[[[[[[[[\"}"));
    assert(!json_depth_safe("[[[[[[[[[[[[[[[[["));
    assert(!screenshot_authorized("", "Bearer "));
    assert(!screenshot_authorized("secret", 0));
    assert(!screenshot_authorized("secret", "Basic secret"));
    assert(!screenshot_authorized("secret", "Bearer secreu"));
    assert(!screenshot_authorized("secret", "Bearer secret_extra"));
    assert(screenshot_authorized("secret", "Bearer secret"));
    assert(mqtt_replayed_command("panel/a", "panel/a/cmd/update", true));
    assert(!mqtt_replayed_command("panel/a", "panel/a/cmd/update", false));
    assert(!mqtt_replayed_command("panel/a", "panel/a/set/pages", true));
    assert(!mqtt_replayed_command("panel/a", "panel/abc/cmd/update", true));
    assert(!mqtt_replayed_command("panel/a", "panel", true));
    /* Update manifests: the bare scheme and partial URLs must be refused. */
    assert(update_manifest_url_valid("https://git.example.org/releases/download/v1.3.0/walldisplay-v1.3.0.json"));
    assert(update_manifest_url_valid("https://192.168.1.10:8443/manifest.json"));
    assert(update_manifest_url_valid("https://[fd12::10]/manifest.json"));
    assert(!update_manifest_url_valid("https://"));
    assert(!update_manifest_url_valid("https:///manifest.json"));
    assert(!update_manifest_url_valid("https://git.example.org"));
    assert(!update_manifest_url_valid("https://git.example.org/"));
    assert(!update_manifest_url_valid("http://git.example.org/manifest.json"));
    assert(!update_manifest_url_valid("ftp://git.example.org/manifest.json"));
    assert(!update_manifest_url_valid("https://git example.org/manifest.json"));
    assert(!update_manifest_url_valid("https://git.example.org/manifest.json\n"));
    assert(!update_manifest_url_valid("https://git.example.org/a\x7f"));
    assert(!update_manifest_url_valid("https://user@git.example.org/manifest.json"));
    assert(!update_manifest_url_valid(NULL));
}

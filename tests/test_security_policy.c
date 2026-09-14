#include "walldisplay/security_policy.h"
#include <assert.h>
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
}

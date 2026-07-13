#include "walldisplay/display_power_policy.h"

#include <assert.h>

static void test_default_timeouts(void) {
    assert(display_power_policy_brightness(0, 300, 600, 20) == 100);
    assert(display_power_policy_brightness(299, 300, 600, 20) == 100);
    assert(display_power_policy_brightness(300, 300, 600, 20) == 20);
    assert(display_power_policy_brightness(899, 300, 600, 20) == 20);
    assert(display_power_policy_brightness(900, 300, 600, 20) == 0);
}

static void test_screen_off_can_be_disabled(void) {
    assert(display_power_policy_brightness(300, 300, 0, 20) == 20);
    assert(display_power_policy_brightness(86400, 300, 0, 20) == 20);
}

static void test_dimming_can_be_disabled(void) {
    assert(display_power_policy_brightness(0, 0, 600, 20) == 100);
    assert(display_power_policy_brightness(86400, 0, 600, 20) == 100);
}

static void test_selected_dim_brightness_is_preserved(void) {
    assert(display_power_policy_brightness(60, 60, 60, 1) == 1);
    assert(display_power_policy_brightness(60, 60, 60, 100) == 100);
}

int main(void) {
    test_default_timeouts();
    test_screen_off_can_be_disabled();
    test_dimming_can_be_disabled();
    test_selected_dim_brightness_is_preserved();
    return 0;
}

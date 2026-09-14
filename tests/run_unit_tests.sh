#!/bin/sh
set -eu

test_binary="$(mktemp /tmp/walldisplay-unit-tests.XXXXXX)"
trap 'rm -f "$test_binary"' EXIT

cc -std=c11 -Wall -Wextra -Werror -Iinclude \
    src/display_power_policy.c tests/test_display_power_policy.c \
    -o "$test_binary"
"$test_binary"

cc -std=c11 -Wall -Wextra -Werror -Iinclude \
    -Imanaged_components/espressif__cjson/cJSON \
    src/page_manager.c managed_components/espressif__cjson/cJSON/cJSON.c \
    tests/test_page_manager.c -lm -o "$test_binary"
"$test_binary"

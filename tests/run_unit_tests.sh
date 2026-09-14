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

cc -std=c11 -Wall -Wextra -Werror -Iinclude \
    src/mqtt_receive.c src/security_policy.c tests/test_mqtt_receive.c -o "$test_binary"
"$test_binary"
c++ -std=c++17 -Wall -Wextra -Werror -Iinclude \
    tests/test_artwork_policy.cpp -o "$test_binary"
"$test_binary"
cc -std=c11 -Wall -Wextra -Werror -Iinclude \
    src/security_policy.c tests/test_security_policy.c -o "$test_binary"
"$test_binary"
cc -std=c11 -Wall -Wextra -Werror -Itests/stubs -Iinclude \
    -Imanaged_components/espressif__cjson/cJSON \
    tests/test_config_persistence.c src/page_manager.c \
    managed_components/espressif__cjson/cJSON/cJSON.c -lm -o "$test_binary"
"$test_binary"

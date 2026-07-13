#!/bin/sh
set -eu

test_binary="$(mktemp /tmp/walldisplay-unit-tests.XXXXXX)"
trap 'rm -f "$test_binary"' EXIT

cc -std=c11 -Wall -Wextra -Werror -Iinclude \
    src/display_power_policy.c tests/test_display_power_policy.c \
    -o "$test_binary"
"$test_binary"

#!/usr/bin/env bash
# Capture deterministic WallDisplay visual-regression fixtures over MQTT.
set -euo pipefail

: "${PANEL_TOPIC:?Set PANEL_TOPIC, e.g. panel/living-room}"
: "${PANEL_HOST:?Set PANEL_HOST to the panel IP address or hostname}"

MQTT_HOST="${MQTT_HOST:-localhost}"
MQTT_PORT="${MQTT_PORT:-1883}"
OUTPUT_DIR="${OUTPUT_DIR:-screenshots}"
SETTLE_SECONDS="${SETTLE_SECONDS:-1}"
RUN_ID="${RUN_ID:-$(date +%Y%m%d%H%M%S)}"

for command in curl mosquitto_pub mosquitto_sub timeout; do
    command -v "$command" >/dev/null || { echo "Missing required command: $command" >&2; exit 1; }
done

mkdir -p "$OUTPUT_DIR"
status_file="$(mktemp)"
trap 'rm -f "$status_file"' EXIT

publish() {
    mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "$1" -m "$2" -r
}

capture() {
    local page="$1"
    local case_name="$2"
    : > "$status_file"
    timeout 20 mosquitto_sub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "$PANEL_TOPIC/state/screenshot" -F '%p' >> "$status_file" &
    local subscriber=$!
    publish "$PANEL_TOPIC/cmd/page" "$page"
    sleep "$SETTLE_SECONDS"
    mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "$PANEL_TOPIC/cmd/screenshot" -m "$case_name"
    for _ in $(seq 1 40); do
        if grep -q "\"state\":\"ready\".*\"name\":\"$case_name\"" "$status_file"; then
            curl --fail --silent --show-error "http://$PANEL_HOST/screenshot.bmp?name=$case_name" -o "$OUTPUT_DIR/$case_name.bmp"
            kill "$subscriber" 2>/dev/null || true
            wait "$subscriber" 2>/dev/null || true
            echo "Captured $OUTPUT_DIR/$case_name.bmp"
            return
        fi
        sleep 0.25
    done
    kill "$subscriber" 2>/dev/null || true
    wait "$subscriber" 2>/dev/null || true
    echo "Timed out waiting for screenshot '$case_name'" >&2
    return 1
}

publish "$PANEL_TOPIC/set/name" "Screenshot Lab"
publish "$PANEL_TOPIC/set/clock" "09:41"
publish "$PANEL_TOPIC/set/date" "Tue, 17 Jul"
publish "$PANEL_TOPIC/set/chip1" "21.4 °C"
publish "$PANEL_TOPIC/set/chip1/color" "ok"
publish "$PANEL_TOPIC/set/chip2" "67 %"
publish "$PANEL_TOPIC/set/chip2/color" "warn"
publish "$PANEL_TOPIC/set/chip3" "Open"
publish "$PANEL_TOPIC/set/chip3/color" "neutral"

publish "$PANEL_TOPIC/set/weather" '{"condition":"partlycloudy","temperature":21.4,"humidity":58,"pressure":1015,"wind_speed":13.2,"wind_unit":"km/h","rainfall":0.8,"rainfall_unit":"mm","irradiance":640,"irradiance_unit":"W/m²","trend":[15,16,17,17,18,19,20,21,21.4],"forecast":[{"day":"Tomorrow","condition":"sunny","high":24,"low":14},{"day":"+2 days","condition":"rainy","high":18,"low":11},{"day":"+3 days","condition":"cloudy","high":20,"low":12}]}'
capture weather "weather-full-$RUN_ID"

publish "$PANEL_TOPIC/set/weather" '{"condition":"rainy","temperature":12.0,"trend":[11,12],"forecast":[]}'
capture weather "weather-minimal-$RUN_ID"

publish "$PANEL_TOPIC/set/media" '{"state":"idle","title":"Media idle","artist":"","source":"","volume_level":0.35,"artwork_url":""}'
capture media "media-idle-$RUN_ID"

publish "$PANEL_TOPIC/set/media" '{"state":"playing","title":"A Very Long Track Title for Wrapping","artist":"Example Artist","source":"Living Room","volume_level":0.75,"artwork_url":""}'
publish "$PANEL_TOPIC/set/media/favorite1/label" "Radio 1"
publish "$PANEL_TOPIC/set/media/favorite1/icon" "radio"
publish "$PANEL_TOPIC/set/media/favorite2/label" "Playlist"
publish "$PANEL_TOPIC/set/media/favorite2/icon" "playlist"
capture media "media-playing-$RUN_ID"

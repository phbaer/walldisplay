#!/usr/bin/env bash
# Capture deterministic WallDisplay visual-regression fixtures over MQTT.
set -euo pipefail

: "${PANEL_TOPIC:?Set PANEL_TOPIC, e.g. panel/living-room}"
: "${SCREENSHOT_TOKEN:?Set SCREENSHOT_TOKEN to the provisioned panel token}"
: "${PANEL_HOST:?Set PANEL_HOST to the panel IP address or hostname}"

MQTT_HOST="${MQTT_HOST:-localhost}"
MQTT_PORT="${MQTT_PORT:-1883}"
MQTT_USERNAME="${MQTT_USERNAME:-}"
MQTT_PASSWORD="${MQTT_PASSWORD:-}"
MQTT_TLS="${MQTT_TLS:-0}"
MQTT_CAFILE="${MQTT_CAFILE:-}"
MQTT_CAPATH="${MQTT_CAPATH:-/etc/ssl/certs}"
OUTPUT_DIR="${OUTPUT_DIR:-screenshots}"
SETTLE_SECONDS="${SETTLE_SECONDS:-1}"
CAPTURE_TIMEOUT_SECONDS="${CAPTURE_TIMEOUT_SECONDS:-45}"
RUN_ID="${RUN_ID:-$(date +%Y%m%d%H%M%S)}"

for command in curl mosquitto_pub mosquitto_sub timeout; do
    command -v "$command" >/dev/null || { echo "Missing required command: $command" >&2; exit 1; }
done
if ! [[ "$CAPTURE_TIMEOUT_SECONDS" =~ ^[1-9][0-9]*$ ]]; then
    echo "CAPTURE_TIMEOUT_SECONDS must be a positive integer" >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR"
status_file="$(mktemp)"
trap 'rm -f "$status_file"' EXIT

mqtt_args=(-h "$MQTT_HOST" -p "$MQTT_PORT")
if [[ -n "$MQTT_USERNAME" ]]; then
    mqtt_args+=(-u "$MQTT_USERNAME")
fi
if [[ -n "$MQTT_PASSWORD" ]]; then
    mqtt_args+=(-P "$MQTT_PASSWORD")
fi
case "$MQTT_TLS" in
    0|false|no) ;;
    1|true|yes)
        if [[ -n "$MQTT_CAFILE" ]]; then
            mqtt_args+=(--cafile "$MQTT_CAFILE")
        else
            mqtt_args+=(--capath "$MQTT_CAPATH")
        fi
        ;;
    *) echo "MQTT_TLS must be 0/1, false/true, or no/yes" >&2; exit 1 ;;
esac

publish() {
    mosquitto_pub "${mqtt_args[@]}" -t "$1" -m "$2" -r
}

capture() {
    local page="$1"
    local case_name="$2"
    : > "$status_file"
    timeout "$CAPTURE_TIMEOUT_SECONDS" mosquitto_sub "${mqtt_args[@]}" -t "$PANEL_TOPIC/state/screenshot" -F '%p' >> "$status_file" &
    local subscriber=$!
    mosquitto_pub "${mqtt_args[@]}" -t "$PANEL_TOPIC/cmd/page" -m "$page"
    sleep "$SETTLE_SECONDS"
    mosquitto_pub "${mqtt_args[@]}" -t "$PANEL_TOPIC/cmd/screenshot" -m "$case_name"
    for _ in $(seq 1 $((CAPTURE_TIMEOUT_SECONDS * 4))); do
        if grep -q "\"state\":\"ready\".*\"name\":\"$case_name\"" "$status_file"; then
            curl --fail --silent --show-error -H "Authorization: Bearer $SCREENSHOT_TOKEN" "http://$PANEL_HOST/screenshot.bmp?name=$case_name" -o "$OUTPUT_DIR/$case_name.bmp"
            kill "$subscriber" 2>/dev/null || true
            wait "$subscriber" 2>/dev/null || true
            echo "Captured $OUTPUT_DIR/$case_name.bmp"
            return
        fi
        if grep -q "\"state\":\"error\".*\"name\":\"$case_name\"" "$status_file"; then
            kill "$subscriber" 2>/dev/null || true
            wait "$subscriber" 2>/dev/null || true
            echo "Panel reported a screenshot error for '$case_name':" >&2
            tail -n 5 "$status_file" >&2
            return 1
        fi
        sleep 0.25
    done
    kill "$subscriber" 2>/dev/null || true
    wait "$subscriber" 2>/dev/null || true
    echo "Timed out waiting for screenshot '$case_name' on $PANEL_TOPIC/state/screenshot" >&2
    if [[ -s "$status_file" ]]; then
        echo "Panel status messages:" >&2
        tail -n 5 "$status_file" >&2
    else
        echo "No screenshot status was received; check MQTT connectivity, topic ACLs, and that screenshots_enabled is ON." >&2
    fi
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

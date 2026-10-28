#!/usr/bin/env bash

set -euo pipefail

: "${DISPLAY:?DISPLAY must be set by visualApp.env}"
: "${PROJECTOR_OUTPUT:?PROJECTOR_OUTPUT must be set by visualApp.env}"
: "${PROJECTOR_MODE:?PROJECTOR_MODE must be set by visualApp.env}"
: "${PROJECTOR_RATE:?PROJECTOR_RATE must be set by visualApp.env}"

XRANDR="${XRANDR:-/usr/bin/xrandr}"
XSET="${XSET:-/usr/bin/xset}"

if [[ ! -x "$XRANDR" ]]; then
    echo "Required executable not found: $XRANDR" >&2
    exit 1
fi

echo "Waiting for XRandR output $PROJECTOR_OUTPUT ..."
until "$XRANDR" --display "$DISPLAY" --query 2>/dev/null \
    | awk -v output="$PROJECTOR_OUTPUT" '$1 == output && $2 == "connected" { found = 1 } END { exit !found }'; do
    sleep 1
done

if ! "$XRANDR" --display "$DISPLAY" --query \
    | awk -v output="$PROJECTOR_OUTPUT" -v mode="$PROJECTOR_MODE" '
        $2 == "connected" || $2 == "disconnected" { inside = ($1 == output); next }
        inside && $1 == mode { found = 1 }
        END { exit !found }
    '; then
    echo "Output $PROJECTOR_OUTPUT does not advertise mode $PROJECTOR_MODE." >&2
    "$XRANDR" --display "$DISPLAY" --query >&2
    exit 1
fi

if [[ "${PROJECTOR_EXCLUSIVE:-1}" == "1" ]]; then
    while read -r output; do
        if [[ "$output" != "$PROJECTOR_OUTPUT" ]]; then
            "$XRANDR" --display "$DISPLAY" --output "$output" --off
        fi
    done < <("$XRANDR" --display "$DISPLAY" --query \
        | awk '$2 == "connected" || $2 == "disconnected" { print $1 }')
fi

"$XRANDR" --display "$DISPLAY" \
    --output "$PROJECTOR_OUTPUT" \
    --mode "$PROJECTOR_MODE" \
    --rate "$PROJECTOR_RATE" \
    --rotate normal \
    --primary

active_geometry=$("$XRANDR" --display "$DISPLAY" --current \
    | awk -v output="$PROJECTOR_OUTPUT" '$1 == output && $2 == "connected" { print; exit }')

if [[ "$active_geometry" != *" ${PROJECTOR_MODE}+"* ]]; then
    echo "Output $PROJECTOR_OUTPUT did not activate at $PROJECTOR_MODE: $active_geometry" >&2
    exit 1
fi

active_rate=$("$XRANDR" --display "$DISPLAY" --current \
    | awk -v output="$PROJECTOR_OUTPUT" -v mode="$PROJECTOR_MODE" '
        $2 == "connected" || $2 == "disconnected" { inside = ($1 == output); next }
        inside && $1 == mode {
            for (i = 2; i <= NF; i++) {
                if ($i ~ /\*/) {
                    gsub(/[+*]/, "", $i)
                    print $i
                    exit
                }
            }
        }
    ')

if [[ -z "$active_rate" ]] || ! awk -v actual="$active_rate" -v expected="$PROJECTOR_RATE" \
    'BEGIN { difference = actual - expected; if (difference < 0) difference = -difference; exit difference > 0.2 }'; then
    echo "Output $PROJECTOR_OUTPUT is not running at the requested rate: expected $PROJECTOR_RATE, got ${active_rate:-unknown}." >&2
    exit 1
fi

# Prevent the desktop from blanking a continuously running installation.
if [[ -x "$XSET" ]]; then
    "$XSET" -display "$DISPLAY" s off
    "$XSET" -display "$DISPLAY" s noblank
    "$XSET" -display "$DISPLAY" -dpms
fi

echo "Projector ready: $PROJECTOR_OUTPUT, $PROJECTOR_MODE at ${active_rate} Hz, XRandR rotation normal, DISPLAY=$DISPLAY."

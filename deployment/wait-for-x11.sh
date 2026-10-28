#!/usr/bin/env bash

set -euo pipefail

: "${DISPLAY:?DISPLAY must be set by the application environment file}"
: "${XAUTHORITY:?XAUTHORITY must be set by the application environment file}"

display_number="${DISPLAY#:}"
display_number="${display_number%%.*}"

if [[ ! "$display_number" =~ ^[0-9]+$ ]]; then
    echo "Invalid X11 DISPLAY value: $DISPLAY" >&2
    exit 1
fi

x_socket="/tmp/.X11-unix/X${display_number}"
echo "Waiting for X11 socket $x_socket and authority file $XAUTHORITY ..."

until [[ -S "$x_socket" && -r "$XAUTHORITY" ]]; do
    sleep 1
done

echo "X11 prerequisites are available for DISPLAY=$DISPLAY."

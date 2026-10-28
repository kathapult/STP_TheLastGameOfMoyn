#!/usr/bin/env bash

set -euo pipefail

usage() {
    echo "Usage: $0 --check|--apply [environment-file]" >&2
}

action="${1:-}"
environment_file="${2:-/etc/thelastgameofmoyn/visualApp.env}"
cmdline_file="${CMDLINE_FILE:-/boot/firmware/cmdline.txt}"

if [[ "$action" != "--check" && "$action" != "--apply" ]]; then
    usage
    exit 2
fi

if [[ ! -r "$environment_file" ]]; then
    echo "Cannot read projector configuration: $environment_file" >&2
    exit 1
fi

# shellcheck source=/dev/null
source "$environment_file"

: "${PROJECTOR_KMS_CONNECTOR:?PROJECTOR_KMS_CONNECTOR is missing in $environment_file}"
: "${PROJECTOR_MODE:?PROJECTOR_MODE is missing in $environment_file}"
: "${PROJECTOR_RATE:?PROJECTOR_RATE is missing in $environment_file}"

if [[ ! "$PROJECTOR_KMS_CONNECTOR" =~ ^[A-Za-z0-9_-]+$ \
    || ! "$PROJECTOR_MODE" =~ ^[0-9]+x[0-9]+$ \
    || ! "$PROJECTOR_RATE" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "Unsafe or invalid KMS display configuration in $environment_file." >&2
    exit 1
fi

if [[ ! -r "$cmdline_file" ]]; then
    echo "Cannot read kernel command line: $cmdline_file" >&2
    exit 1
fi

non_empty_lines=$(awk 'NF { count++ } END { print count + 0 }' "$cmdline_file")
if [[ "$non_empty_lines" != "1" ]]; then
    echo "$cmdline_file must contain exactly one non-empty line; refusing to rewrite it." >&2
    exit 1
fi

required_token="video=${PROJECTOR_KMS_CONNECTOR}:${PROJECTOR_MODE}M@${PROJECTOR_RATE}D"
current_line=$(awk 'NF { print; exit }' "$cmdline_file")

if [[ " $current_line " == *" $required_token "* ]]; then
    echo "KMS projector mode is configured: $required_token"
    exit 0
fi

if [[ "$action" == "--check" ]]; then
    echo "KMS projector mode is missing. Required token: $required_token" >&2
    exit 1
fi

if [[ "$(id -u)" != "0" ]]; then
    echo "--apply must run as root." >&2
    exit 1
fi

read -r -a current_tokens <<< "$current_line"
new_tokens=()
replaced=0

for token in "${current_tokens[@]}"; do
    if [[ "$token" == "video=${PROJECTOR_KMS_CONNECTOR}:"* ]]; then
        if [[ "$replaced" == "0" ]]; then
            new_tokens+=("$required_token")
            replaced=1
        fi
    else
        new_tokens+=("$token")
    fi
done

if [[ "$replaced" == "0" ]]; then
    new_tokens+=("$required_token")
fi

timestamp=$(date -u '+%Y%m%dT%H%M%SZ')
backup_file="${cmdline_file}.before-thelastgameofmoyn-${timestamp}"
temporary_file=$(mktemp "${cmdline_file}.tmp.XXXXXX")

cleanup() {
    if [[ -n "${temporary_file:-}" && -e "$temporary_file" ]]; then
        rm -f -- "$temporary_file"
    fi
}
trap cleanup EXIT

cp -a -- "$cmdline_file" "$backup_file"
printf '%s\n' "${new_tokens[*]}" > "$temporary_file"
chmod --reference="$cmdline_file" "$temporary_file"
chown --reference="$cmdline_file" "$temporary_file"
mv -f -- "$temporary_file" "$cmdline_file"
temporary_file=""

echo "Configured $required_token in $cmdline_file."
echo "Backup: $backup_file"
echo "Reboot Pi B before accepting the display path."

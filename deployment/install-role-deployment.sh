#!/usr/bin/env bash

# Installs one release's complete, role-specific deployment payload. It is
# called both by the bootstrap installer and by update-app.sh, so adding a new
# unit or helper has one installation path instead of two drifting copies.

set -euo pipefail
umask 022

APP_NAME="${1:-}"
MODE="${2:-}"

if [[ -z "$MODE" ]]; then
    MODE="--full"
fi

if (( $# > 2 )) || [[ "$APP_NAME" != "sensorApp" && "$APP_NAME" != "visualApp" ]]; then
    echo "Usage: $0 sensorApp|visualApp [--full|--bootstrap-only|--environment-only]" >&2
    exit 2
fi
if [[ "$MODE" != "--full" && "$MODE" != "--bootstrap-only" \
    && "$MODE" != "--environment-only" ]]; then
    echo "Usage: $0 sensorApp|visualApp [--full|--bootstrap-only|--environment-only]" >&2
    exit 2
fi
if [[ "$(id -u)" != "0" ]]; then
    echo "Role deployment installation must run as root." >&2
    exit 1
fi

deployment_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
config_dir="${CONFIG_ROOT:-/etc/thelastgameofmoyn}"
libexec_dir="${LIBEXEC_ROOT:-/usr/local/libexec/thelastgameofmoyn}"
environment_example="$deployment_dir/$APP_NAME.env.example"
environment_target="$config_dir/$APP_NAME.env"
environment_reference="$config_dir/$APP_NAME.env.example"

has_expected_metadata() {
    local target="$1"
    local mode="${2#0}"
    local metadata

    metadata=$(stat -c '%U:%G:%a' -- "$target" 2>/dev/null || true)
    [[ "$metadata" == "root:root:$mode" ]]
}

atomic_install() {
    local source="$1"
    local target="$2"
    local mode="$3"
    local target_dir temporary

    if [[ ! -f "$source" ]]; then
        echo "Deployment source missing: $source" >&2
        return 1
    fi
    if [[ -L "$target" || ( -e "$target" && ! -f "$target" ) ]]; then
        echo "Refusing to replace non-regular deployment target: $target" >&2
        return 1
    fi
    if [[ -f "$target" ]] && cmp -s -- "$source" "$target" \
        && has_expected_metadata "$target" "$mode"; then
        return 0
    fi

    target_dir=$(dirname "$target")
    install -d -o root -g root -m 0755 "$target_dir"
    temporary=$(mktemp "$target_dir/.install.XXXXXX")
    if ! install -o root -g root -m "$mode" "$source" "$temporary"; then
        rm -f -- "$temporary"
        return 1
    fi
    if ! mv -fT -- "$temporary" "$target"; then
        rm -f -- "$temporary"
        return 1
    fi
}

merge_environment_defaults() {
    local example="$1"
    local target="$2"
    local reference="$3"
    local line key temporary added

    atomic_install "$example" "$reference" 0644

    if [[ ! -e "$target" ]]; then
        atomic_install "$example" "$target" 0644
        echo "Installed initial role configuration: $target"
        return
    fi
    if [[ -L "$target" || ! -f "$target" ]]; then
        echo "Refusing to merge non-regular environment file: $target" >&2
        return 1
    fi

    temporary=$(mktemp "$config_dir/.${APP_NAME}.env.XXXXXX")
    install -o root -g root -m 0644 "$target" "$temporary"
    added=0

    while IFS= read -r line || [[ -n "$line" ]]; do
        line="${line%$'\r'}"
        if [[ -z "$line" || "$line" == \#* ]]; then
            continue
        fi
        if [[ ! "$line" =~ ^([A-Za-z_][A-Za-z0-9_]*)=(.*)$ ]]; then
            echo "Invalid assignment in $example: $line" >&2
            rm -f -- "$temporary"
            return 1
        fi

        key="${BASH_REMATCH[1]}"
        if ! grep -Eq "^[[:space:]]*${key}=" "$target"; then
            if [[ "$added" == "0" ]]; then
                printf '\n# Added from %s during an incremental installation.\n' \
                    "$(basename "$example")" >> "$temporary"
            fi
            printf '%s\n' "$line" >> "$temporary"
            echo "Added missing environment key $key to $target"
            added=1
        fi
    done < "$example"

    if [[ "$added" == "1" ]]; then
        if ! mv -fT -- "$temporary" "$target"; then
            rm -f -- "$temporary"
            return 1
        fi
    else
        rm -f -- "$temporary"
        if ! has_expected_metadata "$target" 0644; then
            chown root:root "$target"
            chmod 0644 "$target"
        fi
    fi
}

if [[ ! -f "$environment_example" ]]; then
    echo "Deployment source missing: $environment_example" >&2
    exit 1
fi

if [[ "$MODE" == "--environment-only" ]]; then
    install -d -o root -g root -m 0755 "$config_dir" "$libexec_dir"
    merge_environment_defaults "$environment_example" "$environment_target" "$environment_reference"
    exit 0
fi

case "$APP_NAME" in
    sensorApp)
        units=(
            sensorApp.service
            sensorApp-calibration.service
            sensorApp-update.service
            sensorApp-update.timer
        )
        role_helpers=()
        ;;
    visualApp)
        units=(
            visualApp.service
            visualApp-calibration.service
            visualApp-diagnostics.service
            visualApp-update.service
            visualApp-update.timer
        )
        role_helpers=(
            "prepare-visual-display.sh:$libexec_dir/prepare-visual-display"
            "configure-kms-display.sh:/usr/local/sbin/thelastgameofmoyn-configure-kms-display"
        )
        ;;
esac

# Internet sharing deliberately remains an inert repository/release file. It
# changes the host network deeply and is installed only by its explicit
# on-demand --apply path, never by this recurring role reconciliation.
common_helpers=(
    "update-app.sh:/usr/local/sbin/thelastgameofmoyn-update-app"
    "diagnose-update-services.sh:/usr/local/sbin/thelastgameofmoyn-diagnose-updates"
    "install-deployment.sh:/usr/local/sbin/thelastgameofmoyn-install-deployment"
    "install-role-deployment.sh:/usr/local/sbin/thelastgameofmoyn-install-role-deployment"
    "wait-for-x11.sh:$libexec_dir/wait-for-x11"
    "${APP_NAME}-update.sh:/usr/local/bin/update-${APP_NAME}.sh"
)

# The bootstrap profile installs only files needed before the first release is
# built. Full mode additionally validates and installs every role unit.
if [[ "$MODE" == "--full" ]]; then
    for unit in "${units[@]}"; do
        [[ -f "$deployment_dir/$unit" ]] || {
            echo "Deployment source missing: $deployment_dir/$unit" >&2
            exit 1
        }
    done
fi
for helper in "${common_helpers[@]}" "${role_helpers[@]}"; do
    source_file="${helper%%:*}"
    [[ -f "$deployment_dir/$source_file" ]] || {
        echo "Deployment source missing: $deployment_dir/$source_file" >&2
        exit 1
    }
done

install -d -o root -g root -m 0755 "$config_dir" "$libexec_dir"
merge_environment_defaults "$environment_example" "$environment_target" "$environment_reference"

if [[ "$MODE" == "--full" ]]; then
    for unit in "${units[@]}"; do
        atomic_install "$deployment_dir/$unit" "/etc/systemd/system/$unit" 0644
    done
fi
for helper in "${common_helpers[@]}" "${role_helpers[@]}"; do
    source_file="${helper%%:*}"
    target_file="${helper#*:}"
    atomic_install "$deployment_dir/$source_file" "$target_file" 0755
done

if [[ "$MODE" == "--full" ]]; then
    systemctl daemon-reload
    echo "Installed complete $APP_NAME unit, helper and environment payload."
else
    echo "Installed $APP_NAME bootstrap helpers and environment payload."
fi

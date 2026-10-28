#!/usr/bin/env bash

# Bootstrap or incrementally reconcile the managed release layout on exactly
# one Pi role. Future updates install their own versioned deployment payload
# through update-app.sh; rerunning this script repairs every current role file.

set -euo pipefail

APP_NAME="${1:-}"
apply_kms=0

if (( $# > 2 )); then
    echo "Usage: $0 sensorApp|visualApp [--apply-kms]" >&2
    exit 2
elif [[ "${2:-}" == "--apply-kms" ]]; then
    apply_kms=1
elif [[ -n "${2:-}" ]]; then
    echo "Usage: $0 sensorApp|visualApp [--apply-kms]" >&2
    exit 2
fi

if [[ "$APP_NAME" != "sensorApp" && "$APP_NAME" != "visualApp" ]]; then
    echo "Usage: $0 sensorApp|visualApp [--apply-kms]" >&2
    exit 2
fi

if [[ "$(id -u)" != "0" ]]; then
    echo "This installer must run as root." >&2
    exit 1
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if [[ -f "$script_dir/update-app.sh" ]]; then
    deployment_dir="$script_dir"
else
    deployment_dir="${DEPLOYMENT_SOURCE_DIR:-/home/kathapult/thelastgameofmoyn/deployment}"
fi

if [[ ! -f "$deployment_dir/install-role-deployment.sh" ]]; then
    echo "Deployment source missing: $deployment_dir/install-role-deployment.sh" >&2
    exit 1
fi

update_timer="$APP_NAME-update.timer"
update_timer_was_active=0
installation_complete=0

if systemctl is-active --quiet "$update_timer"; then
    update_timer_was_active=1
fi

restore_update_timer_on_failure() {
    local exit_status=$?
    trap - EXIT

    if [[ "$installation_complete" == "0" && "$update_timer_was_active" == "1" ]]; then
        echo "Installation failed; restoring previously active $update_timer." >&2
        if ! systemctl start "$update_timer"; then
            echo "Failed to restore $update_timer." >&2
            exit_status=1
        fi
    fi

    exit "$exit_status"
}
trap restore_update_timer_on_failure EXIT

update_timer_load_state=$(systemctl show --property=LoadState --value \
    "$update_timer" 2>/dev/null || true)
if [[ -n "$update_timer_load_state" && "$update_timer_load_state" != "not-found" ]]; then
    echo "Stopping $update_timer while the role installation is running."
    systemctl stop "$update_timer"
fi

install -d -o root -g root -m 0755 \
    /etc/thelastgameofmoyn \
    /opt/thelastgameofmoyn/releases \
    /opt/thelastgameofmoyn/current \
    /var/lib/thelastgameofmoyn/update \
    /usr/local/libexec/thelastgameofmoyn

# Install everything needed to configure and build the first managed release,
# but do not publish units that point to a current symlink before it exists.
# The same role manifest is used again in full mode after activation.
/usr/bin/bash "$deployment_dir/install-role-deployment.sh" "$APP_NAME" --bootstrap-only
environment_target="/etc/thelastgameofmoyn/$APP_NAME.env"

if [[ "$APP_NAME" == "visualApp" ]]; then
    if ! /usr/local/sbin/thelastgameofmoyn-configure-kms-display --check "$environment_target"; then
        if [[ "$apply_kms" == "1" ]]; then
            /usr/local/sbin/thelastgameofmoyn-configure-kms-display --apply "$environment_target"
            echo "Reboot Pi B, then run this installer again without --apply-kms." >&2
            exit 3
        fi

        echo "Review $environment_target, then run:" >&2
        echo "  sudo bash $0 visualApp --apply-kms" >&2
        exit 1
    fi
elif [[ "$apply_kms" == "1" ]]; then
    echo "--apply-kms is valid only for visualApp." >&2
    exit 2
fi

# The updater builds and publishes the versioned release before it stops
# anything. Its installation mode then stops every loaded production,
# calibration or diagnostics profile together with the update timer, installs
# the unit definitions and starts production only after the current symlink is
# valid. A bootstrap build failure therefore leaves an existing legacy process
# and its next boot untouched. The installer already paused the timer above, so
# no second automatic updater can race this forced build; installation mode
# waits for an update that was already active.
/usr/local/sbin/thelastgameofmoyn-update-app "$APP_NAME" --install

# Reconcile once more from the active release. update-app already performs this
# during activation; the explicit pass makes repeated installer runs a complete
# and idempotent repair operation for every role unit, helper and environment.
active_release=$(readlink -f "/opt/thelastgameofmoyn/current/$APP_NAME")
if [[ -z "$active_release" || ! -d "$active_release/deployment" ]]; then
    echo "Active release has no deployment payload: ${active_release:-missing}" >&2
    exit 1
fi
/usr/bin/bash "$active_release/deployment/install-role-deployment.sh" "$APP_NAME" --full

systemctl enable "$APP_NAME.service" "$APP_NAME-update.timer"
systemctl start "$APP_NAME-update.timer"

installation_complete=1
trap - EXIT
echo "$APP_NAME deployment installed and its managed release is active."

#!/usr/bin/env bash

# Atomic updater for one installation role. It is installed root-owned as
# /usr/local/sbin/thelastgameofmoyn-update-app and invoked by the app-specific
# systemd oneshot unit. Git and make always run as BUILD_USER. The short-lived
# privileged orchestration owns /opt + /var/lib release state, deployment
# installation and systemctl, then exits.

set -Eeuo pipefail
umask 022

APP_NAME="${1:-}"
force_update=0
installation_mode=0
operation="update"
rollback_commit=""

case "${2:-}" in
    "") ;;
    --force)
        force_update=1
        ;;
    --install)
        # The interactive role installer uses the normal build-before-stop
        # path, but additionally quiesces every installed runtime profile and
        # the timer during the final activation transaction.
        force_update=1
        installation_mode=1
        ;;
    --rollback)
        operation="rollback"
        rollback_commit="${3:-}"
        ;;
    *)
        echo "Usage: $0 sensorApp|visualApp [--force|--install|--rollback COMMIT]" >&2
        exit 2
        ;;
esac

if [[ "$operation" == "rollback" ]]; then
    if [[ ! "$rollback_commit" =~ ^[0-9a-f]{40}([0-9a-f]{24})?$ || -n "${4:-}" ]]; then
        echo "Usage: $0 sensorApp|visualApp --rollback COMMIT" >&2
        exit 2
    fi
elif [[ -n "${3:-}" ]]; then
    echo "Usage: $0 sensorApp|visualApp [--force|--install]" >&2
    exit 2
fi

BUILD_USER="${BUILD_USER:-kathapult}"
REPO_ROOT="${REPO_ROOT:-/home/kathapult/thelastgameofmoyn}"
BRANCH="${BRANCH:-main}"
RELEASE_ROOT="${RELEASE_ROOT:-/opt/thelastgameofmoyn}"
STATE_ROOT="${STATE_ROOT:-/var/lib/thelastgameofmoyn}"
BUILD_CACHE="${BUILD_CACHE:-/home/kathapult/.cache/thelastgameofmoyn}"
CONFIG_ROOT="${CONFIG_ROOT:-/etc/thelastgameofmoyn}"
LIBEXEC_ROOT="${LIBEXEC_ROOT:-/usr/local/libexec/thelastgameofmoyn}"

# configure-internet-sharing.sh is deliberately absent from both path lists:
# changing the on-demand emergency tool alone must not build or restart an app.
case "$APP_NAME" in
    sensorApp)
        SERVICE="sensorApp.service"
        CALIBRATION_SERVICE="sensorApp-calibration.service"
        DIAGNOSTICS_SERVICE=""
        UPDATE_SERVICE="sensorApp-update.service"
        UPDATE_TIMER="sensorApp-update.timer"
        BINARY="sensorApp"
        ENV_EXAMPLE="sensorApp.env.example"
        ENV_TARGET="$CONFIG_ROOT/sensorApp.env"
        LEGACY_SETTINGS="$REPO_ROOT/sensorApp/bin/data/settings.json"
        relevant_paths=(
            sensorApp shared
            deployment/update-app.sh
            deployment/diagnose-update-services.sh
            deployment/install-deployment.sh
            deployment/install-role-deployment.sh
            deployment/wait-for-x11.sh
            deployment/sensorApp.env.example
            deployment/sensorApp.service
            deployment/sensorApp-calibration.service
            deployment/sensorApp-update.service
            deployment/sensorApp-update.timer
            deployment/sensorApp-update.sh
        )
        ;;
    visualApp)
        SERVICE="visualApp.service"
        CALIBRATION_SERVICE="visualApp-calibration.service"
        DIAGNOSTICS_SERVICE="visualApp-diagnostics.service"
        UPDATE_SERVICE="visualApp-update.service"
        UPDATE_TIMER="visualApp-update.timer"
        BINARY="visualApp"
        ENV_EXAMPLE="visualApp.env.example"
        ENV_TARGET="$CONFIG_ROOT/visualApp.env"
        LEGACY_SETTINGS=""
        relevant_paths=(
            visualApp shared
            deployment/update-app.sh
            deployment/diagnose-update-services.sh
            deployment/install-deployment.sh
            deployment/install-role-deployment.sh
            deployment/wait-for-x11.sh
            deployment/prepare-visual-display.sh
            deployment/configure-kms-display.sh
            deployment/visualApp.env.example
            deployment/visualApp.service
            deployment/visualApp-calibration.service
            deployment/visualApp-diagnostics.service
            deployment/visualApp-update.service
            deployment/visualApp-update.timer
            deployment/visualApp-update.sh
        )
        ;;
    *)
        echo "Usage: $0 sensorApp|visualApp [--force|--install|--rollback COMMIT]" >&2
        exit 2
        ;;
esac

APP_STATE="$STATE_ROOT/update/$APP_NAME"
RUNTIME_APP_STATE="$STATE_ROOT/$APP_NAME"
RELEASES_DIR="$RELEASE_ROOT/releases/$APP_NAME"
CURRENT_DIR="$RELEASE_ROOT/current"
CURRENT_LINK="$CURRENT_DIR/$APP_NAME"
LAST_SEEN_FILE="$APP_STATE/last-seen-commit"
ACTIVE_COMMIT_FILE="$APP_STATE/active-commit"

BUILD_PARENT=""
BUILD_TREE=""
STAGED_RELEASE=""
TEMPORARY_LINK=""
LEGACY_UNIT_BACKUP=""
LEGACY_EXECUTABLE=""
LEGACY_CWD=""

log() {
    echo "[$(date '+%Y-%m-%dT%H:%M:%S%z')] [$APP_NAME] $*"
}

fail() {
    log "ERROR: $*"
    return 1
}

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        fail "required command not found: $1"
    fi
}

run_as_builder() {
    /usr/sbin/runuser -u "$BUILD_USER" -- "$@"
}

is_release_path() {
    [[ -n "${1:-}" && "$1" == "$RELEASES_DIR/"* && -x "$1/$BINARY" ]]
}

delete_staged_release() {
    local path="${1:-}"
    if [[ -n "$path" && "$path" == "$RELEASES_DIR/.staging-"* && -d "$path" ]]; then
        find "$path" -xdev -depth -delete
    fi
}

cleanup() {
    if [[ -n "$TEMPORARY_LINK" && -L "$TEMPORARY_LINK" ]]; then
        rm -f -- "$TEMPORARY_LINK"
    fi

    if [[ -n "$LEGACY_UNIT_BACKUP" && "$LEGACY_UNIT_BACKUP" == "$APP_STATE/.legacy-unit-"* ]]; then
        rm -f -- "$LEGACY_UNIT_BACKUP"
    fi

    delete_staged_release "$STAGED_RELEASE"

    if [[ -n "$BUILD_TREE" && -e "$BUILD_TREE/.git" ]]; then
        run_as_builder git -C "$REPO_ROOT" worktree remove --force "$BUILD_TREE" >/dev/null 2>&1 || true
    fi

    if [[ -n "$BUILD_PARENT" && "$BUILD_PARENT" == "$BUILD_CACHE/"* && -d "$BUILD_PARENT" ]]; then
        rmdir -- "$BUILD_PARENT" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

write_state() {
    local target="$1"
    local value="$2"
    local temporary
    temporary=$(mktemp "$APP_STATE/.state.XXXXXX")
    printf '%s\n' "$value" > "$temporary"
    chmod 0644 "$temporary"
    chown root:root "$temporary"
    mv -fT -- "$temporary" "$target"
}

atomic_install() {
    local source="$1"
    local target="$2"
    local mode="$3"
    local target_dir temporary

    if [[ ! -f "$source" ]]; then
        fail "deployment source missing: $source"
        return 1
    fi
    target_dir=$(dirname "$target") || return 1
    install -d -o root -g root -m 0755 "$target_dir" || return 1
    temporary=$(mktemp "$target_dir/.install.XXXXXX") || return 1
    if ! install -o root -g root -m "$mode" "$source" "$temporary"; then
        rm -f -- "$temporary"
        return 1
    fi
    if ! mv -fT -- "$temporary" "$target"; then
        rm -f -- "$temporary"
        return 1
    fi
}

switch_current_release() {
    local release="$1"
    is_release_path "$release" || fail "refusing to activate invalid release path: $release" || return

    if [[ -e "$CURRENT_LINK" && ! -L "$CURRENT_LINK" ]]; then
        fail "$CURRENT_LINK exists but is not a symbolic link"
        return
    fi

    TEMPORARY_LINK="$CURRENT_DIR/.${APP_NAME}.new.$$"
    rm -f -- "$TEMPORARY_LINK" || return 1
    ln -s -- "$release" "$TEMPORARY_LINK" || return 1
    mv -fT -- "$TEMPORARY_LINK" "$CURRENT_LINK" || return 1
    TEMPORARY_LINK=""
}

remove_absent_maintenance_units() {
    local deployment="$1"
    local maintenance_service maintenance_target
    local -a maintenance_services=("$CALIBRATION_SERVICE")

    if [[ -n "$DIAGNOSTICS_SERVICE" ]]; then
        maintenance_services+=("$DIAGNOSTICS_SERVICE")
    fi

    # A rollback must reproduce the target release's complete role payload.
    # Remove a newer optional maintenance unit only when that exact unit is
    # absent from the target release, including releases with an older role
    # installer that did not yet know about the unit.
    for maintenance_service in "${maintenance_services[@]}"; do
        [[ -f "$deployment/$maintenance_service" ]] && continue

        maintenance_target="/etc/systemd/system/$maintenance_service"
        if [[ -e "$maintenance_target" || -L "$maintenance_target" ]]; then
            if [[ ! -f "$maintenance_target" && ! -L "$maintenance_target" ]]; then
                fail "refusing to remove non-file maintenance target: $maintenance_target"
                return 1
            fi
            rm -f -- "$maintenance_target" || return 1
            log "removed maintenance unit absent from target release: $maintenance_target"
        fi
    done
}

install_release_deployment() {
    local release="$1"
    local deployment="$release/deployment"

    [[ -d "$deployment" ]] || fail "release has no deployment payload: $release" || return

    # New releases own their complete installation manifest. Calling the
    # target release's installer also makes future unit/environment additions
    # effective during the same incremental update that introduces them.
    if [[ -f "$deployment/install-role-deployment.sh" ]]; then
        remove_absent_maintenance_units "$deployment" || return 1
        CONFIG_ROOT="$CONFIG_ROOT" LIBEXEC_ROOT="$LIBEXEC_ROOT" \
            /usr/bin/bash "$deployment/install-role-deployment.sh" "$APP_NAME" --full || return 1
        return 0
    fi

    # Compatibility path for managed releases created before the role
    # installer existed. Core production/update files remain rollbackable.
    atomic_install "$deployment/$SERVICE" "/etc/systemd/system/$SERVICE" 0644 || return 1
    atomic_install "$deployment/$UPDATE_SERVICE" "/etc/systemd/system/$UPDATE_SERVICE" 0644 || return 1
    atomic_install "$deployment/$UPDATE_TIMER" "/etc/systemd/system/$UPDATE_TIMER" 0644 || return 1
    atomic_install "$deployment/update-app.sh" "/usr/local/sbin/thelastgameofmoyn-update-app" 0755 || return 1
    atomic_install "$deployment/install-deployment.sh" "/usr/local/sbin/thelastgameofmoyn-install-deployment" 0755 || return 1
    atomic_install "$deployment/wait-for-x11.sh" "$LIBEXEC_ROOT/wait-for-x11" 0755 || return 1
    atomic_install "$deployment/${APP_NAME}-update.sh" "/usr/local/bin/update-${APP_NAME}.sh" 0755 || return 1

    if [[ "$APP_NAME" == "visualApp" ]]; then
        atomic_install "$deployment/prepare-visual-display.sh" "$LIBEXEC_ROOT/prepare-visual-display" 0755 || return 1
        atomic_install "$deployment/configure-kms-display.sh" "/usr/local/sbin/thelastgameofmoyn-configure-kms-display" 0755 || return 1
    fi

    remove_absent_maintenance_units "$deployment" || return 1

    if [[ ! -e "$ENV_TARGET" ]]; then
        atomic_install "$deployment/$ENV_EXAMPLE" "$ENV_TARGET" 0644 || return 1
        log "installed initial environment file $ENV_TARGET; verify it before hardware acceptance"
    fi

    systemctl daemon-reload || return 1
}

stop_runtime_services_if_loaded() {
    local runtime_service load_state
    local -a runtime_services=("$CALIBRATION_SERVICE" "$SERVICE")
    local -a loaded_services=()

    if [[ -n "$DIAGNOSTICS_SERVICE" ]]; then
        runtime_services=("$DIAGNOSTICS_SERVICE" "${runtime_services[@]}")
    fi
    if [[ "$installation_mode" == "1" ]]; then
        runtime_services+=("$UPDATE_TIMER")
    fi

    # Stop every loaded profile in one systemd transaction. This is important
    # for visualApp-diagnostics.service: its OnSuccess recovery must not start
    # production or the timer again while an installation is activating.
    for runtime_service in "${runtime_services[@]}"; do
        load_state=$(systemctl show --property=LoadState --value \
            "$runtime_service" 2>/dev/null || true)
        if [[ -n "$load_state" && "$load_state" != "not-found" ]]; then
            loaded_services+=("$runtime_service")
        fi
    done

    if (( ${#loaded_services[@]} == 0 )); then
        return 0
    fi
    systemctl stop "${loaded_services[@]}"
}

capture_legacy_service() {
    local fragment pid

    fragment=$(systemctl show --property=FragmentPath --value "$SERVICE" 2>/dev/null || true)
    if [[ -z "$fragment" || ! -f "$fragment" ]]; then
        return 0
    fi

    LEGACY_UNIT_BACKUP=$(mktemp "$APP_STATE/.legacy-unit-XXXXXX") || return 1
    install -o root -g root -m 0600 "$fragment" "$LEGACY_UNIT_BACKUP" || return 1

    pid=$(systemctl show --property=MainPID --value "$SERVICE" 2>/dev/null || true)
    if [[ "$pid" =~ ^[1-9][0-9]*$ && -d "/proc/$pid" ]]; then
        LEGACY_EXECUTABLE=$(readlink -f "/proc/$pid/exe" || true)
        LEGACY_CWD=$(readlink -f "/proc/$pid/cwd" || true)
    fi

    log "captured the pre-managed service definition for first-activation recovery"
}

restore_legacy_service() {
    local pid executable cwd

    if [[ -z "$LEGACY_UNIT_BACKUP" || ! -f "$LEGACY_UNIT_BACKUP" ]]; then
        log "no previous managed release or legacy service is available for automatic recovery"
        return 1
    fi

    log "restoring the pre-managed service definition"
    atomic_install "$LEGACY_UNIT_BACKUP" "/etc/systemd/system/$SERVICE" 0644 || return 1
    if [[ -L "$CURRENT_LINK" ]]; then
        rm -f -- "$CURRENT_LINK" || return 1
    fi
    systemctl daemon-reload || return 1
    systemctl reset-failed "$SERVICE" >/dev/null 2>&1 || true
    systemctl start "$SERVICE" || return 1
    sleep 5
    systemctl is-active --quiet "$SERVICE" || return 1

    if [[ -n "$LEGACY_EXECUTABLE" && -n "$LEGACY_CWD" ]]; then
        pid=$(systemctl show --property=MainPID --value "$SERVICE")
        [[ "$pid" =~ ^[1-9][0-9]*$ && -d "/proc/$pid" ]] || return 1
        executable=$(readlink -f "/proc/$pid/exe")
        cwd=$(readlink -f "/proc/$pid/cwd")
        [[ "$executable" == "$LEGACY_EXECUTABLE" && "$cwd" == "$LEGACY_CWD" ]] || return 1
    fi
}

verify_running_release() {
    local expected_release="$1"
    local pid executable cwd

    sleep 5
    systemctl is-active --quiet "$SERVICE" || return 1

    pid=$(systemctl show --property=MainPID --value "$SERVICE")
    [[ "$pid" =~ ^[1-9][0-9]*$ && -d "/proc/$pid" ]] || return 1

    executable=$(readlink -f "/proc/$pid/exe")
    cwd=$(readlink -f "/proc/$pid/cwd")

    [[ "$executable" == "$expected_release/$BINARY" && "$cwd" == "$expected_release" ]]
}

start_and_verify() {
    local release="$1"
    systemctl reset-failed "$SERVICE" >/dev/null 2>&1 || true
    # restart also repairs the case where a process is still active from a
    # different release path; on an inactive unit it behaves like start.
    systemctl restart "$SERVICE" || return 1

    if ! verify_running_release "$release"; then
        systemctl --no-pager --full status "$SERVICE" || true
        journalctl --no-pager -n 80 -u "$SERVICE" || true
        return 1
    fi
}

rollback_to() {
    local previous_release="$1"

    stop_runtime_services_if_loaded >/dev/null 2>&1 || true

    if ! is_release_path "$previous_release"; then
        restore_legacy_service
        return
    fi

    log "rolling back to $previous_release"
    install_release_deployment "$previous_release" || return 1
    switch_current_release "$previous_release" || return 1
    start_and_verify "$previous_release"
}

if [[ "$(id -u)" != "0" ]]; then
    fail "this updater must run as root through $UPDATE_SERVICE"
    exit 1
fi

for command_name in git make install cp mv ln find awk readlink systemctl journalctl mktemp flock; do
    require_command "$command_name"
done

if [[ ! -d "$REPO_ROOT/.git" ]]; then
    fail "repository not found: $REPO_ROOT"
    exit 1
fi

if ! id "$BUILD_USER" >/dev/null 2>&1; then
    fail "build user does not exist: $BUILD_USER"
    exit 1
fi
BUILD_GROUP=$(id -gn "$BUILD_USER")
if [[ ! -x /usr/sbin/runuser ]]; then
    fail "required executable not found: /usr/sbin/runuser"
    exit 1
fi

install -d -o root -g root -m 0755 "$RELEASE_ROOT" "$RELEASE_ROOT/releases" "$CURRENT_DIR"
install -d -o root -g root -m 0755 "$RELEASES_DIR" "$STATE_ROOT" "$STATE_ROOT/update" "$CONFIG_ROOT" "$LIBEXEC_ROOT"
install -d -o root -g root -m 0755 "$APP_STATE"
install -d -o "$BUILD_USER" -g "$BUILD_GROUP" -m 0750 "$RUNTIME_APP_STATE" "$BUILD_CACHE"

# The systemd oneshot is serialized by systemd itself, but the same updater can
# also be reached through the bootstrap installer or compatibility wrapper.
# Keep those entry points from building or switching the same role concurrently.
exec 9>"$APP_STATE/update.lock"
if [[ "$installation_mode" == "1" ]]; then
    log "waiting for any already running $APP_NAME update to finish"
    if ! flock 9; then
        fail "could not acquire the $APP_NAME update lock"
        exit 1
    fi
elif ! flock -n 9; then
    fail "another $APP_NAME update is already running"
    exit 1
fi

if [[ "$operation" == "rollback" ]]; then
    rollback_release="$RELEASES_DIR/$rollback_commit"
    if [[ ! -f "$rollback_release/.complete" ]] || ! is_release_path "$rollback_release"; then
        fail "requested rollback release is incomplete or missing: $rollback_release"
        exit 1
    fi

    current_release=""
    if [[ -L "$CURRENT_LINK" ]]; then
        current_release=$(readlink -f "$CURRENT_LINK" || true)
    fi
    if ! is_release_path "$current_release"; then
        capture_legacy_service
    fi

    if [[ "$rollback_release" == "$current_release" ]]; then
        log "requested rollback release is already current; reconciling its deployment and process"
        install_release_deployment "$rollback_release"
        if ! verify_running_release "$rollback_release"; then
            start_and_verify "$rollback_release"
        fi
        exit 0
    fi

    log "manually rolling back from ${current_release:-unmanaged installation} to $rollback_release"
    rollback_result=0
    stop_runtime_services_if_loaded || rollback_result=$?
    if [[ "$rollback_result" == "0" ]]; then
        install_release_deployment "$rollback_release" || rollback_result=$?
    fi
    if [[ "$rollback_result" == "0" ]]; then
        switch_current_release "$rollback_release" || rollback_result=$?
    fi
    if [[ "$rollback_result" == "0" ]]; then
        start_and_verify "$rollback_release" || rollback_result=$?
    fi

    if [[ "$rollback_result" != "0" ]]; then
        log "manual rollback failed; attempting to restore the previously active release"
        rollback_to "$current_release" || true
        exit 1
    fi

    write_state "$ACTIVE_COMMIT_FILE" "$rollback_commit"
    log "manual rollback successful; the update timer will retain this release until a newer relevant commit appears"
    exit 0
fi

log "fetching origin/$BRANCH as $BUILD_USER"
run_as_builder git -C "$REPO_ROOT" fetch --prune origin "$BRANCH"
REMOTE_COMMIT=$(run_as_builder git -C "$REPO_ROOT" rev-parse "origin/$BRANCH")

LAST_SEEN_COMMIT=""
if [[ -r "$LAST_SEEN_FILE" ]]; then
    LAST_SEEN_COMMIT=$(<"$LAST_SEEN_FILE")
fi

if [[ ! "$LAST_SEEN_COMMIT" =~ ^[0-9a-f]{40}([0-9a-f]{24})?$ ]] \
    || ! run_as_builder git -C "$REPO_ROOT" cat-file -e "${LAST_SEEN_COMMIT}^{commit}" 2>/dev/null; then
    LAST_SEEN_COMMIT=$(run_as_builder git -C "$REPO_ROOT" rev-parse HEAD)
fi

CURRENT_RELEASE=""
if [[ -L "$CURRENT_LINK" ]]; then
    CURRENT_RELEASE=$(readlink -f "$CURRENT_LINK" || true)
fi

if ! is_release_path "$CURRENT_RELEASE"; then
    capture_legacy_service
fi

if [[ "$force_update" == "0" && "$REMOTE_COMMIT" == "$LAST_SEEN_COMMIT" ]] \
    && is_release_path "$CURRENT_RELEASE"; then
    log "no new commit; reconciling the active deployment and process"
    install_release_deployment "$CURRENT_RELEASE"
    if ! verify_running_release "$CURRENT_RELEASE"; then
        start_and_verify "$CURRENT_RELEASE"
    fi
    exit 0
fi

if [[ "$force_update" == "0" && "$REMOTE_COMMIT" != "$LAST_SEEN_COMMIT" ]] \
    && is_release_path "$CURRENT_RELEASE"; then
    set +e
    run_as_builder git -C "$REPO_ROOT" diff --quiet "$LAST_SEEN_COMMIT" "$REMOTE_COMMIT" -- "${relevant_paths[@]}"
    diff_result=$?
    set -e

    if [[ "$diff_result" == "0" ]]; then
        log "commit range does not affect $APP_NAME or shared/deployment inputs; no build or restart"
        write_state "$LAST_SEEN_FILE" "$REMOTE_COMMIT"
        install_release_deployment "$CURRENT_RELEASE"
        if ! verify_running_release "$CURRENT_RELEASE"; then
            start_and_verify "$CURRENT_RELEASE"
        fi
        exit 0
    elif [[ "$diff_result" != "1" ]]; then
        fail "git diff failed for $LAST_SEEN_COMMIT..$REMOTE_COMMIT"
        exit 1
    fi
fi

RELEASE_PATH="$RELEASES_DIR/$REMOTE_COMMIT"

if [[ -d "$RELEASE_PATH" ]]; then
    if [[ ! -f "$RELEASE_PATH/.complete" ]] || ! is_release_path "$RELEASE_PATH"; then
        fail "existing release is incomplete and will not be overwritten: $RELEASE_PATH"
        exit 1
    fi
    log "reusing already built release $RELEASE_PATH"
else
    BUILD_PARENT=$(run_as_builder mktemp -d "$BUILD_CACHE/${APP_NAME}.XXXXXX")
    BUILD_TREE="$BUILD_PARENT/worktree"

    log "building $REMOTE_COMMIT in isolated worktree $BUILD_TREE"
    run_as_builder git -C "$REPO_ROOT" worktree add --detach "$BUILD_TREE" "$REMOTE_COMMIT"
    run_as_builder make -C "$BUILD_TREE/$APP_NAME" Release

    BUILT_BIN="$BUILD_TREE/$APP_NAME/bin/$BINARY"
    if [[ ! -x "$BUILT_BIN" ]]; then
        fail "Release build produced no executable binary: $BUILT_BIN"
        exit 1
    fi

    STAGED_RELEASE=$(mktemp -d "$RELEASES_DIR/.staging-${REMOTE_COMMIT}.XXXXXX")
    cp -a -- "$BUILD_TREE/$APP_NAME/bin/." "$STAGED_RELEASE/"
    cp -a -- "$BUILD_TREE/deployment" "$STAGED_RELEASE/deployment"

    if [[ "$APP_NAME" == "sensorApp" ]]; then
        RUNTIME_SETTINGS="$RUNTIME_APP_STATE/settings.json"
        install -d -o root -g root -m 0755 "$STAGED_RELEASE/data"

        if [[ ! -e "$RUNTIME_SETTINGS" ]]; then
            settings_source="$STAGED_RELEASE/data/settings.json"
            if [[ -f "$LEGACY_SETTINGS" ]]; then
                settings_source="$LEGACY_SETTINGS"
            fi
            [[ -f "$settings_source" ]] || fail "no settings.json available for initial migration" || exit 1
            install -o "$BUILD_USER" -g "$BUILD_GROUP" -m 0640 "$settings_source" "$RUNTIME_SETTINGS"
            log "migrated initial calibration to $RUNTIME_SETTINGS"
        fi

        rm -f -- "$STAGED_RELEASE/data/settings.json"
        ln -s -- "$RUNTIME_SETTINGS" "$STAGED_RELEASE/data/settings.json"
    elif [[ "$APP_NAME" == "visualApp" ]]; then
        # Projection size/rounding/base edge, output orientation and
        # sensor-to-table mapping are site calibration, not release content.
        # Seed them once from the versioned default, then make every release
        # write through to /var/lib.
        RUNTIME_PROJECTION="$RUNTIME_APP_STATE/projection-calibration.json"
        install -d -o root -g root -m 0755 "$STAGED_RELEASE/data"

        if [[ ! -e "$RUNTIME_PROJECTION" ]]; then
            projection_source="$STAGED_RELEASE/data/projection-calibration.json"
            [[ -f "$projection_source" ]] \
                || fail "no projection-calibration.json available for initial migration" || exit 1
            install -o "$BUILD_USER" -g "$BUILD_GROUP" -m 0640 \
                "$projection_source" "$RUNTIME_PROJECTION"
            log "installed initial projection calibration at $RUNTIME_PROJECTION"
        fi

        rm -f -- "$STAGED_RELEASE/data/projection-calibration.json"
        ln -s -- "$RUNTIME_PROJECTION" \
            "$STAGED_RELEASE/data/projection-calibration.json"
    fi

    printf 'app=%s\ncommit=%s\nbuilt_at=%s\n' \
        "$APP_NAME" "$REMOTE_COMMIT" "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" \
        > "$STAGED_RELEASE/release-manifest"
    touch "$STAGED_RELEASE/.complete"
    chown -R root:root "$STAGED_RELEASE"
    # mktemp creates the staging root as 0700. Published releases must remain
    # traversable by the unprivileged application user; files keep their
    # executable/read bits while all group/other write bits are removed.
    find "$STAGED_RELEASE" -type d -exec chmod 0755 {} +
    chmod -R go-w "$STAGED_RELEASE"
    mv -- "$STAGED_RELEASE" "$RELEASE_PATH"
    STAGED_RELEASE=""
    log "SUCCESS event=release-created commit=$REMOTE_COMMIT release=$RELEASE_PATH"
fi

previous_release="$CURRENT_RELEASE"
log "stopping loaded $APP_NAME runtime services for the atomic release switch"

activation_result=0
stop_runtime_services_if_loaded || activation_result=$?

if [[ "$activation_result" == "0" && "$APP_NAME" == "sensorApp" \
    && -f "$LEGACY_SETTINGS" && ! -L "$LEGACY_SETTINGS" ]] \
    && ! is_release_path "$previous_release"; then
    # On the first managed switch the legacy process may have written a final
    # calibration value while the new release was building. Copy it only after
    # that process has stopped. Later releases already write through the
    # /var/lib symlink and skip this branch.
    install -o "$BUILD_USER" -g "$BUILD_GROUP" -m 0640 "$LEGACY_SETTINGS" "$RUNTIME_APP_STATE/settings.json" \
        || activation_result=$?
fi

if [[ "$activation_result" == "0" ]]; then
    install_release_deployment "$RELEASE_PATH" || activation_result=$?
fi
if [[ "$activation_result" == "0" ]]; then
    switch_current_release "$RELEASE_PATH" || activation_result=$?
fi
if [[ "$activation_result" == "0" ]]; then
    start_and_verify "$RELEASE_PATH" || activation_result=$?
fi

if [[ "$activation_result" != "0" ]]; then
    log "activation failed; attempting automatic rollback"
    if rollback_to "$previous_release"; then
        log "rollback succeeded; previous show release is active"
    else
        log "rollback failed; manual recovery is required"
    fi
    exit 1
fi

write_state "$ACTIVE_COMMIT_FILE" "$REMOTE_COMMIT"
write_state "$LAST_SEEN_FILE" "$REMOTE_COMMIT"
log "SUCCESS event=update-activated commit=$REMOTE_COMMIT service=$SERVICE binary=$RELEASE_PATH/$BINARY"

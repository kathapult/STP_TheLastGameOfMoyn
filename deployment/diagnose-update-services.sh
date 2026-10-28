#!/usr/bin/env bash

# Diagnoses the managed update path without changing it. Passing --trigger is
# the deliberate exception: it starts the selected systemd oneshot and may
# build, activate or roll back a release through the normal updater.

set -uo pipefail
umask 022
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export PATH

BUILD_USER="${BUILD_USER:-kathapult}"
REPO_ROOT="${REPO_ROOT:-/home/kathapult/thelastgameofmoyn}"
BRANCH="${BRANCH:-main}"
RELEASE_ROOT="${RELEASE_ROOT:-/opt/thelastgameofmoyn}"
STATE_ROOT="${STATE_ROOT:-/var/lib/thelastgameofmoyn}"

selector="all"
selector_set=0
trigger_update=0
network_check=1
journal_lines=80

ok_count=0
warning_count=0
error_count=0
checked_roles=0
remote_commit=""

usage() {
	cat <<'EOF'
Usage: diagnose-update-services.sh [sensorApp|visualApp|all] [OPTIONS]

Diagnoses the installed update timer, oneshot service, Git access, release
state, deployed files and production process for one or both roles. In "all"
mode, roles that are not installed on this Pi are reported and skipped.

Options:
  --trigger       Start each selected update service after the read-only checks.
                  This may build, activate, restart or roll back a release.
  --no-network    Skip the read-only git ls-remote test against origin/main.
  --help          Show this help.

Exit codes:
  0  No blocking error was found; warnings may still require attention.
  1  At least one blocking update-path error was found.
  2  Invalid invocation or missing diagnostic prerequisites.
EOF
}

for argument in "$@"; do
	case "$argument" in
		sensorApp|visualApp|all)
			if [[ "$selector_set" == "1" ]]; then
				echo "Only one role selector may be supplied." >&2
				usage >&2
				exit 2
			fi
			selector="$argument"
			selector_set=1
			;;
		--trigger)
			trigger_update=1
			;;
		--no-network)
			network_check=0
			;;
		--help|-h)
			usage
			exit 0
			;;
		*)
			echo "Unknown argument: $argument" >&2
			usage >&2
			exit 2
			;;
	esac
done

heading() {
	printf '\n========== %s ==========\n' "$1"
}

ok() {
	printf '[ OK ] %s\n' "$*"
	ok_count=$((ok_count + 1))
}

info() {
	printf '[INFO] %s\n' "$*"
}

warn() {
	printf '[WARN] %s\n' "$*"
	warning_count=$((warning_count + 1))
}

problem() {
	printf '[FAIL] %s\n' "$*"
	error_count=$((error_count + 1))
}

property() {
	local unit="$1"
	local name="$2"
	systemctl show "$unit" --property="$name" --value 2>/dev/null || true
}

unit_is_present() {
	local unit="$1"
	local load_state
	load_state=$(property "$unit" LoadState)
	[[ -e "/etc/systemd/system/$unit" || -L "/etc/systemd/system/$unit" \
		|| ( -n "$load_state" && "$load_state" != "not-found" ) ]]
}

role_is_present() {
	local app="$1"
	unit_is_present "$app-update.timer" || unit_is_present "$app-update.service" \
		|| [[ -e "$RELEASE_ROOT/current/$app" || -L "$RELEASE_ROOT/current/$app" ]]
}

require_prerequisites() {
	local missing=0
	local command_name

	if [[ "$(id -u)" != "0" ]]; then
		echo "Run this diagnostic as root, for example with sudo." >&2
		exit 2
	fi

	for command_name in systemctl journalctl git cmp readlink df awk grep id stat test timeout; do
		if ! command -v "$command_name" >/dev/null 2>&1; then
			echo "Required diagnostic command not found: $command_name" >&2
			missing=1
		fi
	done
	if [[ ! -x /usr/sbin/runuser ]]; then
		echo "Required diagnostic executable not found: /usr/sbin/runuser" >&2
		missing=1
	fi
	if [[ "$missing" == "1" ]]; then
		exit 2
	fi
}

check_systemd_manager() {
	local manager_state
	manager_state=$(systemctl is-system-running 2>/dev/null || true)
	case "$manager_state" in
		running)
			ok "systemd reports a running system."
			;;
		degraded)
			warn "systemd reports a degraded system; unrelated failed units may exist."
			;;
		starting|maintenance)
			warn "systemd is currently in state '$manager_state'; results may be transitional."
			;;
		*)
			problem "systemd is not operational (state: ${manager_state:-unknown})."
			;;
	esac
}

check_filesystem_usage() {
	local path="$1"
	local line used_percent available

	if [[ ! -e "$path" ]]; then
		problem "Required filesystem path is missing: $path"
		return
	fi

	line=$(df -Pk "$path" 2>/dev/null | awk 'NR == 2 { print $4, $5 }')
	available=${line%% *}
	used_percent=${line##* }
	used_percent=${used_percent%%%}
	if [[ ! "$used_percent" =~ ^[0-9]+$ ]]; then
		warn "Could not determine filesystem usage for $path."
	elif (( used_percent >= 95 )); then
		problem "$path is ${used_percent}% full (${available:-unknown} KiB available)."
	elif (( used_percent >= 85 )); then
		warn "$path is ${used_percent}% full (${available:-unknown} KiB available)."
	else
		ok "$path has sufficient free space (${used_percent}% used)."
	fi
}

run_as_builder() {
	/usr/sbin/runuser -u "$BUILD_USER" -- "$@"
}

display_remote_url() {
	local value="$1"
	local scheme rest
	if [[ "$value" == *"://"* ]]; then
		scheme=${value%%://*}
		rest=${value#*://}
		if [[ "$rest" == *"@"* ]]; then
			printf '%s://***@%s' "$scheme" "${rest#*@}"
			return
		fi
	fi
	printf '%s' "$value"
}

check_repository() {
	local inside remote_url safe_remote_url remote_output local_remote
	local remote_result remote_candidate remote_user_info
	local -a remote_command

	heading "Repository and network"
	if ! id "$BUILD_USER" >/dev/null 2>&1; then
		problem "Build user does not exist: $BUILD_USER"
		return
	fi
	ok "Build user exists: $BUILD_USER"

	if [[ ! -d "$REPO_ROOT/.git" ]]; then
		problem "Repository not found at $REPO_ROOT."
		return
	fi
	inside=$(run_as_builder git -C "$REPO_ROOT" rev-parse --is-inside-work-tree 2>/dev/null || true)
	if [[ "$inside" != "true" ]]; then
		problem "$BUILD_USER cannot use the Git checkout at $REPO_ROOT."
		return
	fi
	ok "$BUILD_USER can read the Git checkout at $REPO_ROOT."
	if run_as_builder test -w "$REPO_ROOT/.git"; then
		ok "$BUILD_USER can write Git metadata required by fetch and worktree operations."
	else
		problem "$BUILD_USER cannot write $REPO_ROOT/.git; fetch and worktree creation will fail."
	fi

	remote_url=$(run_as_builder git -C "$REPO_ROOT" remote get-url origin 2>/dev/null || true)
	if [[ -z "$remote_url" ]]; then
		problem "Git remote 'origin' is missing or unreadable."
		return
	fi
	safe_remote_url=$(display_remote_url "$remote_url")
	info "origin: $safe_remote_url"

	local_remote=$(run_as_builder git -C "$REPO_ROOT" rev-parse "origin/$BRANCH" 2>/dev/null || true)
	if [[ "$local_remote" =~ ^[0-9a-f]{40}([0-9a-f]{24})?$ ]]; then
		ok "Local origin/$BRANCH resolves to $local_remote."
	else
		warn "Local origin/$BRANCH is missing; the updater must fetch it successfully."
	fi

	if [[ "$network_check" == "0" ]]; then
		info "Network check skipped by --no-network."
		return
	fi

	# Batch mode prevents an unattended diagnostic from waiting for SSH input.
	remote_command=(
		/usr/sbin/runuser -u "$BUILD_USER" -- env
		GIT_TERMINAL_PROMPT=0
		"GIT_SSH_COMMAND=ssh -o BatchMode=yes -o ConnectTimeout=10"
		git -C "$REPO_ROOT" ls-remote origin "refs/heads/$BRANCH"
	)
	remote_command=(timeout 25s "${remote_command[@]}")
	remote_output=$("${remote_command[@]}" 2>&1)
	remote_result=$?
	remote_candidate=$(awk -v expected="refs/heads/$BRANCH" \
		'$2 == expected { print $1; exit }' <<< "$remote_output")
	if [[ "$remote_result" == "0" \
		&& "$remote_candidate" =~ ^[0-9a-f]{40}([0-9a-f]{24})?$ ]]; then
		remote_commit="$remote_candidate"
		ok "Remote refs/heads/$BRANCH is reachable as $BUILD_USER ($remote_commit)."
	else
		problem "Remote refs/heads/$BRANCH is not reachable as $BUILD_USER."
		if [[ "$remote_url" == *"://"* && "${remote_url#*://}" == *"@"* ]]; then
			remote_user_info=${remote_url#*://}
			remote_user_info=${remote_user_info%%@*}
			remote_output=${remote_output//"$remote_user_info"/***}
		fi
		printf '       git ls-remote: %s\n' "${remote_output:-no output}"
	fi
}

check_unit_loaded() {
	local unit="$1"
	local load_state
	load_state=$(property "$unit" LoadState)
	if [[ "$load_state" == "loaded" ]]; then
		ok "$unit is loaded."
	else
		problem "$unit is not loaded (LoadState=${load_state:-unknown})."
	fi
}

check_deployed_file() {
	local source="$1"
	local target="$2"
	local label="$3"
	local expected_mode="$4"
	local metadata

	if [[ ! -f "$target" || -L "$target" ]]; then
		problem "$label is missing or not a regular file: $target"
		return
	fi
	if [[ ! -f "$source" ]]; then
		warn "Cannot compare $label because the release source is missing: $source"
		return
	fi
	if cmp -s -- "$source" "$target"; then
		ok "$label matches the active release."
	else
		problem "$label differs from the active release: $target"
	fi
	metadata=$(stat -c '%U:%G:%a' -- "$target" 2>/dev/null || true)
	if [[ "$metadata" == "root:root:${expected_mode#0}" ]]; then
		ok "$label has expected metadata $metadata."
	else
		problem "$label has unexpected metadata ${metadata:-unknown}; expected root:root:${expected_mode#0}."
	fi
}

check_timer() {
	local timer="$1"
	local maintenance_active="$2"
	local unit_file_state active_state sub_state next_elapse last_trigger

	heading "$timer"
	check_unit_loaded "$timer"
	unit_file_state=$(systemctl is-enabled "$timer" 2>/dev/null || true)
	case "$unit_file_state" in
		enabled|enabled-runtime)
			ok "$timer is enabled."
			;;
		*)
			problem "$timer is not enabled (UnitFileState=${unit_file_state:-unknown})."
			;;
	esac

	active_state=$(property "$timer" ActiveState)
	sub_state=$(property "$timer" SubState)
	if [[ "$active_state" == "active" && "$sub_state" == "waiting" ]]; then
		ok "$timer is active and waiting for its next run."
	elif [[ "$maintenance_active" == "active" && "$active_state" == "inactive" ]]; then
		warn "$timer is inactive because a maintenance service is active."
	else
		problem "$timer is not waiting (ActiveState=${active_state:-unknown}, SubState=${sub_state:-unknown})."
	fi

	last_trigger=$(property "$timer" LastTriggerUSec)
	next_elapse=$(property "$timer" NextElapseUSecRealtime)
	info "Last trigger: ${last_trigger:-unknown}"
	info "Next realtime trigger: ${next_elapse:-monotonic timer or unknown}"
	systemctl status "$timer" --no-pager --full 2>/dev/null || true
	systemctl list-timers --all "$timer" --no-pager 2>/dev/null || true
}

check_update_unit_contract() {
	local app="$1"
	local service="$app-update.service"
	local service_type user group exec_start standard_output standard_error syslog_identifier needs_reload

	check_unit_loaded "$service"
	service_type=$(property "$service" Type)
	user=$(property "$service" User)
	group=$(property "$service" Group)
	exec_start=$(property "$service" ExecStart)
	standard_output=$(property "$service" StandardOutput)
	standard_error=$(property "$service" StandardError)
	syslog_identifier=$(property "$service" SyslogIdentifier)
	needs_reload=$(property "$service" NeedDaemonReload)

	[[ "$service_type" == "oneshot" ]] \
		&& ok "$service uses Type=oneshot." \
		|| problem "$service has unexpected Type=${service_type:-unknown}; expected oneshot."
	[[ "$user" == "root" && "$group" == "root" ]] \
		&& ok "$service runs the privileged orchestrator as root:root." \
		|| problem "$service has unexpected identity ${user:-unknown}:${group:-unknown}; expected root:root."
	if [[ "$exec_start" == *"/usr/local/sbin/thelastgameofmoyn-update-app"* \
		&& "$exec_start" == *"$app"* ]]; then
		ok "$service starts the managed updater for $app."
	else
		problem "$service has an unexpected ExecStart: ${exec_start:-missing}"
	fi
	[[ "$standard_output" == "journal" && "$standard_error" == "journal" ]] \
		&& ok "$service writes stdout and stderr to the system journal." \
		|| problem "$service has unexpected logging targets stdout=${standard_output:-unknown}, stderr=${standard_error:-unknown}; expected journal."
	[[ "$syslog_identifier" == "$app-update" ]] \
		&& ok "$service uses SyslogIdentifier=$app-update." \
		|| problem "$service has unexpected SyslogIdentifier=${syslog_identifier:-missing}; expected $app-update."
	if [[ "$needs_reload" == "yes" ]]; then
		problem "systemd reports NeedDaemonReload=yes for $service."
	else
		ok "systemd does not require daemon-reload for $service."
	fi
}

trigger_update_service() {
	local app="$1"
	local maintenance_active="$2"
	local update_service="$app-update.service"

	if [[ "$trigger_update" == "0" ]]; then
		info "Manual update trigger not requested. Use --trigger to run $update_service."
		return
	fi
	if [[ "$maintenance_active" == "active" ]]; then
		problem "Refusing to trigger $update_service while a maintenance service is active."
		return
	fi
	if [[ "$(property "$update_service" ActiveState)" == "active" ]]; then
		warn "$update_service is already running; no second start was requested."
		return
	fi

	info "Starting $update_service; this may build, activate, restart or roll back a release."
	if systemctl start "$update_service"; then
		ok "$update_service completed successfully."
	else
		problem "$update_service returned a failure."
	fi
}

check_update_result_and_journal() {
	local app="$1"
	local service="$app-update.service"
	local active_state result main_status journal_output

	heading "$service result and journal"
	active_state=$(property "$service" ActiveState)
	result=$(property "$service" Result)
	main_status=$(property "$service" ExecMainStatus)
	if [[ "$active_state" == "active" ]]; then
		info "$service is currently running."
	elif [[ "$active_state" == "inactive" && "$result" == "success" \
		&& ( -z "$main_status" || "$main_status" == "0" ) ]]; then
		ok "$service is inactive after a successful oneshot run; this is normal."
	else
		problem "$service last result is unhealthy (ActiveState=${active_state:-unknown}, Result=${result:-unknown}, ExecMainStatus=${main_status:-unknown})."
	fi

	systemctl status "$service" --no-pager --full 2>/dev/null || true
	journal_output=$(journalctl -u "$service" -n "$journal_lines" --no-pager -o short-iso 2>&1 || true)
	printf '%s\n' "$journal_output"
	if [[ -z "$journal_output" || "$journal_output" == "-- No entries --" ]]; then
		warn "$service has no journal entries; the timer may never have fired."
	elif grep -Eiq 'publickey|host key verification failed|could not read from remote|permission denied' \
		<<< "$journal_output"; then
		warn "$service journal contains a likely Git/SSH permission failure."
	elif grep -Eiq 'repository not found|required command not found|git diff failed' \
		<<< "$journal_output"; then
		warn "$service journal contains a repository or updater prerequisite failure."
	elif grep -Eiq 'activation failed|rollback failed|manual recovery is required' \
		<<< "$journal_output"; then
		warn "$service journal contains an activation or rollback failure."
	elif grep -Eiq '\[.*\] \[.*\] ERROR:|failed with result|failed to start' \
		<<< "$journal_output"; then
		warn "$service journal contains a recent generic failure marker."
	fi
}

read_commit_file() {
	local path="$1"
	local value=""
	if [[ -r "$path" ]]; then
		value=$(<"$path")
	fi
	printf '%s' "$value"
}

check_release_state() {
	local app="$1"
	local maintenance_active="$2"
	local current_link="$RELEASE_ROOT/current/$app"
	local app_state="$STATE_ROOT/update/$app"
	local current_release="" release_commit active_commit last_seen
	local main_service="$app.service"
	local main_active main_pid process_exe process_cwd update_active main_journal

	heading "$app release and production process"
	if [[ ! -L "$current_link" ]]; then
		problem "$current_link is not a symbolic link to a managed release."
		current_release=""
	else
		current_release=$(readlink -f "$current_link" 2>/dev/null || true)
		if [[ "$current_release" == "$RELEASE_ROOT/releases/$app/"* \
			&& -d "$current_release" ]]; then
			ok "$current_link points to managed release $current_release."
		else
			problem "$current_link resolves outside the managed release root: ${current_release:-unresolved}"
		fi
	fi

	if [[ -n "$current_release" && -d "$current_release" ]]; then
		[[ -f "$current_release/.complete" ]] \
			&& ok "The active release has its .complete marker." \
			|| problem "The active release has no .complete marker."
		[[ -x "$current_release/$app" ]] \
			&& ok "The active release binary is executable." \
			|| problem "The active release binary is missing or not executable: $current_release/$app"

		check_deployed_file "$current_release/deployment/$app-update.service" \
			"/etc/systemd/system/$app-update.service" "$app update service" 0644
		check_deployed_file "$current_release/deployment/$app-update.timer" \
			"/etc/systemd/system/$app-update.timer" "$app update timer" 0644
		check_deployed_file "$current_release/deployment/update-app.sh" \
			"/usr/local/sbin/thelastgameofmoyn-update-app" "managed updater" 0755
		check_deployed_file "$current_release/deployment/diagnose-update-services.sh" \
			"/usr/local/sbin/thelastgameofmoyn-diagnose-updates" "installed diagnostic" 0755
		if [[ "$app" == "visualApp" ]]; then
			check_deployed_file "$current_release/deployment/visualApp-diagnostics.service" \
				"/etc/systemd/system/visualApp-diagnostics.service" \
				"visualApp on-demand diagnostics service" 0644
		fi
	fi

	active_commit=$(read_commit_file "$app_state/active-commit")
	last_seen=$(read_commit_file "$app_state/last-seen-commit")
	release_commit=${current_release##*/}
	if [[ "$active_commit" =~ ^[0-9a-f]{40}([0-9a-f]{24})?$ ]]; then
		if [[ "$active_commit" == "$release_commit" ]]; then
			ok "active-commit matches the current release ($active_commit)."
		else
			problem "active-commit ($active_commit) does not match current release ($release_commit)."
		fi
	else
		problem "The active-commit state is missing or invalid: $app_state/active-commit"
	fi
	if [[ "$last_seen" =~ ^[0-9a-f]{40}([0-9a-f]{24})?$ ]]; then
		ok "last-seen-commit is valid ($last_seen)."
		if [[ -n "$remote_commit" && "$last_seen" != "$remote_commit" ]]; then
			warn "last-seen-commit has not reached remote $BRANCH ($remote_commit)."
		fi
	else
		problem "The last-seen-commit state is missing or invalid: $app_state/last-seen-commit"
	fi

	check_unit_loaded "$main_service"
	main_active=$(property "$main_service" ActiveState)
	update_active=$(property "$app-update.service" ActiveState)
	if [[ "$maintenance_active" == "active" && "$main_active" == "inactive" ]]; then
		info "$main_service is inactive as expected during maintenance."
	elif [[ "$update_active" == "active" ]]; then
		info "$main_service state is transitional while the updater is active ($main_active)."
	elif [[ "$main_active" == "active" ]]; then
		ok "$main_service is active."
	else
		problem "$main_service is not active (ActiveState=${main_active:-unknown})."
		main_journal=$(journalctl -u "$main_service" -n 50 --no-pager -o short-iso 2>&1 || true)
		printf '%s\n' "$main_journal"
	fi

	main_pid=$(property "$main_service" MainPID)
	if [[ "$main_active" == "active" && "$main_pid" =~ ^[1-9][0-9]*$ ]]; then
		process_exe=$(readlink -f "/proc/$main_pid/exe" 2>/dev/null || true)
		process_cwd=$(readlink -f "/proc/$main_pid/cwd" 2>/dev/null || true)
		if [[ -n "$current_release" && "$process_exe" == "$current_release/$app" \
			&& "$process_cwd" == "$current_release" ]]; then
			ok "$main_service PID $main_pid runs executable and cwd from the active release."
		else
			problem "$main_service PID $main_pid uses unexpected paths (exe=${process_exe:-unknown}, cwd=${process_cwd:-unknown})."
		fi
	elif [[ "$main_active" == "active" ]]; then
		problem "$main_service is active without a valid MainPID."
	fi
}

check_role() {
	local app="$1"
	local calibration_service="$app-calibration.service"
	local diagnostics_service="visualApp-diagnostics.service"
	local calibration_active diagnostics_active="inactive" diagnostics_unit_state maintenance_active="inactive"

	checked_roles=$((checked_roles + 1))
	heading "$app"
	check_unit_loaded "$calibration_service"
	calibration_active=$(property "$calibration_service" ActiveState)
	if [[ "$calibration_active" == "active" ]]; then
		warn "$calibration_service is active; it intentionally conflicts with the update timer."
	else
		ok "$calibration_service is not active."
	fi

	if [[ "$app" == "visualApp" ]]; then
		check_unit_loaded "$diagnostics_service"
		diagnostics_unit_state=$(systemctl is-enabled "$diagnostics_service" 2>/dev/null || true)
		if [[ "$diagnostics_unit_state" == "static" ]]; then
			ok "$diagnostics_service is installed without a boot enablement path."
		else
			problem "$diagnostics_service has unexpected UnitFileState=${diagnostics_unit_state:-unknown}; expected static."
		fi
		diagnostics_active=$(property "$diagnostics_service" ActiveState)
		if [[ "$diagnostics_active" == "active" ]]; then
			warn "$diagnostics_service is active; it intentionally conflicts with the production and update services."
		else
			ok "$diagnostics_service is not active."
		fi
	fi

	if [[ "$calibration_active" == "active" || "$diagnostics_active" == "active" ]]; then
		maintenance_active="active"
	fi

	check_timer "$app-update.timer" "$maintenance_active"
	check_update_unit_contract "$app"
	trigger_update_service "$app" "$maintenance_active"
	check_update_result_and_journal "$app"
	check_release_state "$app" "$maintenance_active"
}

require_prerequisites

heading "Update service diagnostics"
info "Role selector: $selector"
info "Manual trigger: $([[ "$trigger_update" == "1" ]] && echo enabled || echo disabled)"
check_systemd_manager
check_filesystem_usage /home
check_filesystem_usage /opt
check_filesystem_usage /var
check_repository

for app in sensorApp visualApp; do
	if [[ "$selector" != "all" && "$selector" != "$app" ]]; then
		continue
	fi
	if role_is_present "$app"; then
		check_role "$app"
	elif [[ "$selector" == "all" ]]; then
		info "$app is not installed on this Pi; skipping that role."
	else
		problem "$app is not installed on this Pi."
	fi
done

if [[ "$checked_roles" == "0" && "$selector" == "all" ]]; then
	problem "No installed update role was found."
fi

heading "Summary"
printf 'Successful checks: %d\n' "$ok_count"
printf 'Warnings:          %d\n' "$warning_count"
printf 'Errors:            %d\n' "$error_count"
if (( error_count > 0 )); then
	printf 'Result: blocking update-path problems were found.\n'
	exit 1
fi
if (( warning_count > 0 )); then
	printf 'Result: no blocking error was found, but warnings should be reviewed.\n'
	exit 0
fi
printf 'Result: no update-path problem was found.\n'

#!/usr/bin/env bash

# On-demand emergency tool for configuring symmetric IPv4 forwarding between
# the dedicated Pi-to-Pi link and whichever Pi currently has a working Wi-Fi
# uplink. Deployment installers and update timers must neither invoke it nor
# install it into system paths. It owns only its NetworkManager profile fields,
# sysctl file, nftables table and systemd unit; it never flushes or replaces an
# existing firewall ruleset.

set -Eeuo pipefail
umask 022

ACTION="${1:-}"
ROLE_ARGUMENT="${2:-}"

LAN_INTERFACE="${LAN_INTERFACE:-eth0}"
WIFI_INTERFACE="${WIFI_INTERFACE:-wlan0}"
PI_LINK_CONNECTION="${PI_LINK_CONNECTION:-pi-link}"
WIFI_CONNECTION="${WIFI_CONNECTION:-}"
WIFI_ROUTE_METRIC="${WIFI_ROUTE_METRIC:-100}"
FALLBACK_ROUTE_METRIC="${FALLBACK_ROUTE_METRIC:-2000}"
DNS_SERVERS="${DNS_SERVERS:-1.1.1.1 9.9.9.9}"

CONFIG_ROOT="${CONFIG_ROOT:-/etc/thelastgameofmoyn}"
SYSCTL_ROOT="${SYSCTL_ROOT:-/etc/sysctl.d}"
SYSTEMD_UNIT_ROOT="${SYSTEMD_UNIT_ROOT:-/etc/systemd/system}"
INSTALLED_SCRIPT="${INSTALLED_SCRIPT:-/usr/local/sbin/thelastgameofmoyn-configure-internet-sharing}"

STATE_FILE="$CONFIG_ROOT/internet-sharing.conf"
NFT_RULES_FILE="$CONFIG_ROOT/internet-sharing.nft"
SYSCTL_FILE="$SYSCTL_ROOT/90-thelastgameofmoyn-internet-sharing.conf"
UNIT_NAME="thelastgameofmoyn-internet-sharing.service"
UNIT_FILE="$SYSTEMD_UNIT_ROOT/$UNIT_NAME"
NFTABLES_DROPIN_DIR="$SYSTEMD_UNIT_ROOT/nftables.service.d"
NFTABLES_DROPIN_FILE="$NFTABLES_DROPIN_DIR/20-thelastgameofmoyn-internet-sharing.conf"
NFT_TABLE="thelastgameofmoyn_internet_share"

REBOOT_REQUIRED=0
CHECK_FAILURES=0

usage() {
    cat >&2 <<'EOF'
Usage:
  configure-internet-sharing.sh --apply [sensorApp|visualApp]
  configure-internet-sharing.sh --check
  configure-internet-sharing.sh --remove

The role is auto-detected from 10.0.0.1/24 (sensorApp) or 10.0.0.2/24
(visualApp) on eth0 when omitted. The fixed Wi-Fi addresses are verified but
never changed: sensorApp uses 10.130.131.35, visualApp uses 10.130.131.156.
Useful first-run overrides:

  LAN_INTERFACE=eth0
  WIFI_INTERFACE=wlan0
  PI_LINK_CONNECTION=pi-link
  WIFI_CONNECTION=<NetworkManager connection name or UUID>
  WIFI_ROUTE_METRIC=100
  FALLBACK_ROUTE_METRIC=2000
  DNS_SERVERS="1.1.1.1 9.9.9.9"

Run --remove before changing overrides after the first successful --apply.
EOF
}

log() {
    echo "[internet-sharing] $*"
}

warn() {
    echo "[internet-sharing] WARNING: $*" >&2
}

fail() {
    echo "[internet-sharing] ERROR: $*" >&2
    return 1
}

require_root() {
    if [[ "$(id -u)" != "0" ]]; then
        fail "$ACTION must run as root."
        exit 1
    fi
}

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        fail "required command not found: $1"
        exit 1
    fi
}

validate_interface_name() {
    [[ "$1" =~ ^[A-Za-z0-9_.:-]+$ ]]
}

validate_metric() {
    [[ "$1" =~ ^[0-9]+$ ]] && (( 10#$1 <= 4294967295 ))
}

validate_ipv4() {
    local value="$1"
    local first second third fourth extra octet

    IFS=. read -r first second third fourth extra <<< "$value"
    [[ -n "$first" && -n "$second" && -n "$third" && -n "$fourth" && -z "$extra" ]] \
        || return 1

    for octet in "$first" "$second" "$third" "$fourth"; do
        [[ "$octet" =~ ^[0-9]{1,3}$ ]] || return 1
        (( 10#$octet <= 255 )) || return 1
    done
}

validate_path() {
    [[ "$1" =~ ^/[A-Za-z0-9._/-]+$ ]]
}

validate_dns_servers() {
    local dns_values=()
    local dns

    read -r -a dns_values <<< "$DNS_SERVERS"
    (( ${#dns_values[@]} > 0 )) || fail "DNS_SERVERS must not be empty." || return 1
    for dns in "${dns_values[@]}"; do
        validate_ipv4 "$dns" || fail "invalid DNS server: $dns" || return 1
    done
    DNS_SERVERS="${dns_values[*]}"
}

validate_inputs() {

    validate_interface_name "$LAN_INTERFACE" \
        || fail "unsafe LAN_INTERFACE: $LAN_INTERFACE" || exit 2
    validate_interface_name "$WIFI_INTERFACE" \
        || fail "unsafe WIFI_INTERFACE: $WIFI_INTERFACE" || exit 2
    validate_metric "$WIFI_ROUTE_METRIC" \
        || fail "invalid WIFI_ROUTE_METRIC: $WIFI_ROUTE_METRIC" || exit 2
    validate_metric "$FALLBACK_ROUTE_METRIC" \
        || fail "invalid FALLBACK_ROUTE_METRIC: $FALLBACK_ROUTE_METRIC" || exit 2
    if (( 10#$FALLBACK_ROUTE_METRIC <= 10#$WIFI_ROUTE_METRIC )); then
        fail "FALLBACK_ROUTE_METRIC must be greater than WIFI_ROUTE_METRIC."
        exit 2
    fi

    validate_dns_servers || exit 2

    for path in "$CONFIG_ROOT" "$SYSCTL_ROOT" "$SYSTEMD_UNIT_ROOT" "$INSTALLED_SCRIPT"; do
        validate_path "$path" || fail "unsafe managed path: $path" || exit 2
    done

    if [[ "$PI_LINK_CONNECTION" == *$'\n'* || "$PI_LINK_CONNECTION" == *$'\r'* \
        || "$WIFI_CONNECTION" == *$'\n'* || "$WIFI_CONNECTION" == *$'\r'* ]]; then
        fail "NetworkManager connection identifiers must be single-line values."
        exit 2
    fi
}

has_expected_metadata() {
    local target="$1"
    local mode="${2#0}"
    local metadata

    metadata=$(stat -c '%U:%G:%a' -- "$target" 2>/dev/null || true)
    [[ "$metadata" == "root:root:$mode" ]]
}

atomic_install_file() {
    local source="$1"
    local target="$2"
    local mode="$3"
    local target_dir temporary

    [[ -f "$source" ]] || fail "installation source missing: $source" || return
    if [[ -L "$target" || ( -e "$target" && ! -f "$target" ) ]]; then
        fail "refusing to replace non-regular target: $target"
        return
    fi
    if [[ -f "$target" ]] && cmp -s -- "$source" "$target" \
        && has_expected_metadata "$target" "$mode"; then
        return 0
    fi

    target_dir=$(dirname "$target")
    install -d -o root -g root -m 0755 "$target_dir"
    temporary=$(mktemp "$target_dir/.internet-sharing.XXXXXX")
    if ! install -o root -g root -m "$mode" "$source" "$temporary"; then
        rm -f -- "$temporary"
        return 1
    fi
    if ! mv -fT -- "$temporary" "$target"; then
        rm -f -- "$temporary"
        return 1
    fi
}

atomic_write_content() {
    local target="$1"
    local mode="$2"
    local content="$3"
    local target_dir temporary

    if [[ -L "$target" || ( -e "$target" && ! -f "$target" ) ]]; then
        fail "refusing to replace non-regular target: $target"
        return
    fi

    target_dir=$(dirname "$target")
    install -d -o root -g root -m 0755 "$target_dir"
    temporary=$(mktemp "$target_dir/.internet-sharing.XXXXXX")
    printf '%s\n' "$content" > "$temporary"
    chown root:root "$temporary"
    chmod "$mode" "$temporary"

    if [[ -f "$target" ]] && cmp -s -- "$temporary" "$target" \
        && has_expected_metadata "$target" "$mode"; then
        rm -f -- "$temporary"
        return 0
    fi
    if ! mv -fT -- "$temporary" "$target"; then
        rm -f -- "$temporary"
        return 1
    fi
}

remove_managed_file() {
    local target="$1"

    if [[ -L "$target" || ( -e "$target" && ! -f "$target" ) ]]; then
        fail "refusing to remove non-regular managed target: $target"
        return
    fi
    rm -f -- "$target"
}

nm_get() {
    local uuid="$1"
    local field="$2"
    local value

    value=$(nmcli -g "$field" connection show uuid "$uuid")
    if [[ "$value" == "--" ]]; then
        value=""
    fi
    printf '%s' "$value"
}

connection_uuid() {
    local identifier="$1"
    local uuid

    uuid=$(nmcli -g connection.uuid connection show "$identifier" 2>/dev/null || true)
    [[ -n "$uuid" && "$uuid" != "--" ]] || return 1
    printf '%s' "$uuid"
}

active_uuid_for_device() {
    local interface_name="$1"
    local uuid

    uuid=$(nmcli -g GENERAL.CON-UUID device show "$interface_name" 2>/dev/null || true)
    if [[ "$uuid" == "--" ]]; then
        uuid=""
    fi
    printf '%s' "$uuid"
}

resolve_wifi_uuid() {
    local active_uuid uuid type
    local candidates=()

    if [[ -n "$WIFI_CONNECTION" ]]; then
        connection_uuid "$WIFI_CONNECTION" \
            || fail "Wi-Fi connection not found: $WIFI_CONNECTION" || return
        return 0
    fi

    active_uuid=$(active_uuid_for_device "$WIFI_INTERFACE")
    if [[ -n "$active_uuid" ]]; then
        printf '%s' "$active_uuid"
        return 0
    fi

    while IFS=: read -r uuid type; do
        case "$type" in
            802-11-wireless|wifi)
                candidates+=("$uuid")
                ;;
        esac
    done < <(nmcli -t -f UUID,TYPE connection show)

    if (( ${#candidates[@]} == 1 )); then
        printf '%s' "${candidates[0]}"
        return 0
    fi

    nmcli -f NAME,UUID,TYPE connection show >&2 || true
    fail "cannot choose one Wi-Fi profile; set WIFI_CONNECTION explicitly."
}

set_role_values() {
    case "$1" in
        sensorApp)
            ROLE="sensorApp"
            LOCAL_IP="10.0.0.1"
            PEER_IP="10.0.0.2"
            EXPECTED_WIFI_IP="10.130.131.35"
            ;;
        visualApp)
            ROLE="visualApp"
            LOCAL_IP="10.0.0.2"
            PEER_IP="10.0.0.1"
            EXPECTED_WIFI_IP="10.130.131.156"
            ;;
        *)
            fail "invalid role: $1"
            return 2
            ;;
    esac
}

detect_role() {
    local addresses has_sensor has_visual

    addresses=$(ip -4 -o address show dev "$LAN_INTERFACE" 2>/dev/null) \
        || fail "LAN interface not found: $LAN_INTERFACE" || return
    has_sensor=0
    has_visual=0
    grep -Eq '[[:space:]]10\.0\.0\.1/24[[:space:]]' <<< "$addresses" && has_sensor=1
    grep -Eq '[[:space:]]10\.0\.0\.2/24[[:space:]]' <<< "$addresses" && has_visual=1

    if [[ "$has_sensor" == "1" && "$has_visual" == "0" ]]; then
        set_role_values sensorApp
    elif [[ "$has_visual" == "1" && "$has_sensor" == "0" ]]; then
        set_role_values visualApp
    else
        fail "cannot infer one role from 10.0.0.1/24 or 10.0.0.2/24 on $LAN_INTERFACE."
    fi
}

verify_role_address() {
    local addresses

    addresses=$(ip -4 -o address show dev "$LAN_INTERFACE" 2>/dev/null) \
        || fail "LAN interface not found: $LAN_INTERFACE" || return
    grep -Eq "[[:space:]]${LOCAL_IP//./\\.}/24[[:space:]]" <<< "$addresses" \
        || fail "$LAN_INTERFACE does not currently own $LOCAL_IP/24."
}

verify_profile_interfaces() {
    local lan_profile_addresses lan_profile_interface lan_profile_type
    local wifi_profile_interface wifi_profile_type

    lan_profile_type=$(nm_get "$LAN_CONNECTION_UUID" connection.type)
    if [[ "$lan_profile_type" != "802-3-ethernet" && "$lan_profile_type" != "ethernet" ]]; then
        fail "the Pi-link profile is not Ethernet: $lan_profile_type"
        return
    fi

    lan_profile_addresses=$(nm_get "$LAN_CONNECTION_UUID" ipv4.addresses)
    grep -Eq "(^|,)${LOCAL_IP//./\\.}/24($|,)" <<< "$lan_profile_addresses" \
        || fail "the Pi-link profile does not contain $LOCAL_IP/24: $lan_profile_addresses" || return

    lan_profile_interface=$(nm_get "$LAN_CONNECTION_UUID" connection.interface-name)
    if [[ -n "$lan_profile_interface" && "$lan_profile_interface" != "$LAN_INTERFACE" ]]; then
        fail "the Pi-link profile is bound to $lan_profile_interface, not $LAN_INTERFACE."
        return
    fi

    wifi_profile_interface=$(nm_get "$WIFI_CONNECTION_UUID" connection.interface-name)
    wifi_profile_type=$(nm_get "$WIFI_CONNECTION_UUID" connection.type)
    if [[ "$wifi_profile_type" != "802-11-wireless" && "$wifi_profile_type" != "wifi" ]]; then
        fail "the selected uplink profile is not Wi-Fi: $wifi_profile_type"
        return
    fi
    if [[ -n "$wifi_profile_interface" && "$wifi_profile_interface" != "$WIFI_INTERFACE" ]]; then
        fail "the Wi-Fi profile is bound to $wifi_profile_interface, not $WIFI_INTERFACE."
        return
    fi

    if ip -4 route show dev "$WIFI_INTERFACE" 2>/dev/null \
        | grep -Eq '^10\.0\.0\.0/24([[:space:]]|$)'; then
        fail "the Wi-Fi network overlaps the dedicated 10.0.0.0/24 Pi link."
        return
    fi
}

profile_has_ipv4_address() {
    local addresses="$1"
    local expected_ip="$2"

    grep -Eq "(^|[[:space:],;])${expected_ip//./\\.}/[0-9]{1,2}($|[[:space:],;])" \
        <<< "$addresses"
}

verify_wifi_address_contract() {
    local active_uuid active_addresses wifi_method wifi_profile_addresses

    if ! ip link show dev "$WIFI_INTERFACE" >/dev/null 2>&1; then
        fail "Wi-Fi interface not found: $WIFI_INTERFACE"
        return
    fi

    wifi_method=$(nm_get "$WIFI_CONNECTION_UUID" ipv4.method)
    wifi_profile_addresses=$(nm_get "$WIFI_CONNECTION_UUID" ipv4.addresses)

    case "$wifi_method" in
        manual)
            profile_has_ipv4_address "$wifi_profile_addresses" "$EXPECTED_WIFI_IP" \
                || fail "the Wi-Fi profile must contain $EXPECTED_WIFI_IP: $wifi_profile_addresses" \
                || return
            ;;
        auto)
            warn "$ROLE uses DHCP for Wi-Fi; reserve $EXPECTED_WIFI_IP for this Pi in the Android hotspot."
            ;;
        *)
            fail "the Wi-Fi profile uses unsupported IPv4 method '$wifi_method'; expected auto or manual."
            return
            ;;
    esac

    active_uuid=$(active_uuid_for_device "$WIFI_INTERFACE")
    if [[ -z "$active_uuid" ]]; then
        log "$WIFI_INTERFACE is disconnected; the fixed Wi-Fi address will be checked when it is active."
        return 0
    fi
    if [[ "$active_uuid" != "$WIFI_CONNECTION_UUID" ]]; then
        fail "$WIFI_INTERFACE is using an unconfigured profile ($active_uuid)."
        return
    fi

    active_addresses=$(ip -4 -o address show dev "$WIFI_INTERFACE" 2>/dev/null || true)
    profile_has_ipv4_address "$active_addresses" "$EXPECTED_WIFI_IP" \
        || fail "$ROLE must remain reachable as $EXPECTED_WIFI_IP on $WIFI_INTERFACE; active addresses: ${active_addresses:-<none>}"
}

load_state() {
    local metadata required_name stored_expected_wifi_ip

    if [[ -L "$STATE_FILE" || ! -f "$STATE_FILE" ]]; then
        fail "managed state file missing or unsafe: $STATE_FILE"
        return
    fi
    metadata=$(stat -c '%U:%G:%a' -- "$STATE_FILE" 2>/dev/null || true)
    if [[ "$metadata" != "root:root:600" ]]; then
        fail "unsafe state-file ownership or mode: $STATE_FILE ($metadata)"
        return
    fi

    # The file is generated from validated values and writable only by root.
    # shellcheck source=/dev/null
    source "$STATE_FILE"

    [[ "${CONFIG_VERSION:-}" == "1" ]] || fail "unsupported state-file version." || return
    stored_expected_wifi_ip="${EXPECTED_WIFI_IP:-}"
    set_role_values "${ROLE:-}" || return
    [[ "$stored_expected_wifi_ip" == "$EXPECTED_WIFI_IP" ]] \
        || fail "stored Wi-Fi address does not match the $ROLE contract." || return
    validate_interface_name "${LAN_INTERFACE:-}" || fail "invalid stored LAN interface." || return
    validate_interface_name "${WIFI_INTERFACE:-}" || fail "invalid stored Wi-Fi interface." || return
    validate_metric "${WIFI_ROUTE_METRIC:-}" || fail "invalid stored Wi-Fi metric." || return
    validate_metric "${FALLBACK_ROUTE_METRIC:-}" || fail "invalid stored fallback metric." || return
    if (( 10#$FALLBACK_ROUTE_METRIC <= 10#$WIFI_ROUTE_METRIC )); then
        fail "stored fallback metric must be greater than the stored Wi-Fi metric."
        return
    fi
    validate_dns_servers || return
    [[ "${LAN_CONNECTION_UUID:-}" =~ ^[0-9A-Fa-f-]{36}$ ]] \
        || fail "invalid stored Pi-link connection UUID." || return
    [[ "${WIFI_CONNECTION_UUID:-}" =~ ^[0-9A-Fa-f-]{36}$ ]] \
        || fail "invalid stored Wi-Fi connection UUID." || return
    for required_name in ORIGINAL_LAN_GATEWAY ORIGINAL_LAN_ROUTE_METRIC \
        ORIGINAL_LAN_NEVER_DEFAULT ORIGINAL_LAN_DNS ORIGINAL_WIFI_ROUTE_METRIC \
        ORIGINAL_WIFI_AUTOCONNECT ORIGINAL_WIFI_AUTOCONNECT_RETRIES \
        ORIGINAL_WIFI_METHOD ORIGINAL_WIFI_ADDRESSES ORIGINAL_WIFI_GATEWAY \
        ORIGINAL_IP_FORWARD; do
        [[ -v "$required_name" ]] || fail "state file is missing $required_name." || return
    done
    [[ "$ORIGINAL_IP_FORWARD" == "0" || "$ORIGINAL_IP_FORWARD" == "1" ]] \
        || fail "invalid stored net.ipv4.ip_forward value." || return
}

render_state() {
    local content

    printf -v content '%s\n' \
        "CONFIG_VERSION=1" \
        "ROLE=$(printf '%q' "$ROLE")" \
        "LOCAL_IP=$(printf '%q' "$LOCAL_IP")" \
        "PEER_IP=$(printf '%q' "$PEER_IP")" \
        "EXPECTED_WIFI_IP=$(printf '%q' "$EXPECTED_WIFI_IP")" \
        "LAN_INTERFACE=$(printf '%q' "$LAN_INTERFACE")" \
        "WIFI_INTERFACE=$(printf '%q' "$WIFI_INTERFACE")" \
        "LAN_CONNECTION_UUID=$(printf '%q' "$LAN_CONNECTION_UUID")" \
        "WIFI_CONNECTION_UUID=$(printf '%q' "$WIFI_CONNECTION_UUID")" \
        "WIFI_ROUTE_METRIC=$(printf '%q' "$WIFI_ROUTE_METRIC")" \
        "FALLBACK_ROUTE_METRIC=$(printf '%q' "$FALLBACK_ROUTE_METRIC")" \
        "DNS_SERVERS=$(printf '%q' "$DNS_SERVERS")" \
        "ORIGINAL_LAN_GATEWAY=$(printf '%q' "$ORIGINAL_LAN_GATEWAY")" \
        "ORIGINAL_LAN_ROUTE_METRIC=$(printf '%q' "$ORIGINAL_LAN_ROUTE_METRIC")" \
        "ORIGINAL_LAN_NEVER_DEFAULT=$(printf '%q' "$ORIGINAL_LAN_NEVER_DEFAULT")" \
        "ORIGINAL_LAN_DNS=$(printf '%q' "$ORIGINAL_LAN_DNS")" \
        "ORIGINAL_WIFI_ROUTE_METRIC=$(printf '%q' "$ORIGINAL_WIFI_ROUTE_METRIC")" \
        "ORIGINAL_WIFI_AUTOCONNECT=$(printf '%q' "$ORIGINAL_WIFI_AUTOCONNECT")" \
        "ORIGINAL_WIFI_AUTOCONNECT_RETRIES=$(printf '%q' "$ORIGINAL_WIFI_AUTOCONNECT_RETRIES")" \
        "ORIGINAL_WIFI_METHOD=$(printf '%q' "$ORIGINAL_WIFI_METHOD")" \
        "ORIGINAL_WIFI_ADDRESSES=$(printf '%q' "$ORIGINAL_WIFI_ADDRESSES")" \
        "ORIGINAL_WIFI_GATEWAY=$(printf '%q' "$ORIGINAL_WIFI_GATEWAY")" \
        "ORIGINAL_IP_FORWARD=$(printf '%q' "$ORIGINAL_IP_FORWARD")"
    atomic_write_content "$STATE_FILE" 0600 "$content"
}

prepare_state() {
    local requested_role="$1"
    local target

    if [[ -f "$STATE_FILE" && ! -L "$STATE_FILE" ]]; then
        load_state
        if [[ -n "$requested_role" && "$requested_role" != "$ROLE" ]]; then
            fail "stored role is $ROLE, not $requested_role; run --remove first."
            return
        fi
        log "Reusing managed state for $ROLE."
        return 0
    fi
    if [[ -e "$STATE_FILE" || -L "$STATE_FILE" ]]; then
        fail "refusing unsafe state target: $STATE_FILE"
        return
    fi

    for target in "$NFT_RULES_FILE" "$SYSCTL_FILE" "$UNIT_FILE" "$NFTABLES_DROPIN_FILE"; do
        if [[ -e "$target" || -L "$target" ]]; then
            fail "managed target exists without rollback state: $target"
            return
        fi
    done

    if [[ -n "$requested_role" ]]; then
        set_role_values "$requested_role" || return
    else
        detect_role || return
    fi
    verify_role_address || return

    LAN_CONNECTION_UUID=$(connection_uuid "$PI_LINK_CONNECTION") \
        || fail "Pi-link connection not found: $PI_LINK_CONNECTION" || return
    WIFI_CONNECTION_UUID=$(resolve_wifi_uuid) || return
    verify_profile_interfaces || return
    verify_wifi_address_contract || return

    ORIGINAL_LAN_GATEWAY=$(nm_get "$LAN_CONNECTION_UUID" ipv4.gateway)
    ORIGINAL_LAN_ROUTE_METRIC=$(nm_get "$LAN_CONNECTION_UUID" ipv4.route-metric)
    ORIGINAL_LAN_NEVER_DEFAULT=$(nm_get "$LAN_CONNECTION_UUID" ipv4.never-default)
    ORIGINAL_LAN_DNS=$(nm_get "$LAN_CONNECTION_UUID" ipv4.dns)
    ORIGINAL_WIFI_ROUTE_METRIC=$(nm_get "$WIFI_CONNECTION_UUID" ipv4.route-metric)
    ORIGINAL_WIFI_AUTOCONNECT=$(nm_get "$WIFI_CONNECTION_UUID" connection.autoconnect)
    ORIGINAL_WIFI_AUTOCONNECT_RETRIES=$(nm_get "$WIFI_CONNECTION_UUID" connection.autoconnect-retries)
    ORIGINAL_WIFI_METHOD=$(nm_get "$WIFI_CONNECTION_UUID" ipv4.method)
    ORIGINAL_WIFI_ADDRESSES=$(nm_get "$WIFI_CONNECTION_UUID" ipv4.addresses)
    ORIGINAL_WIFI_GATEWAY=$(nm_get "$WIFI_CONNECTION_UUID" ipv4.gateway)
    ORIGINAL_IP_FORWARD=$(sysctl -n net.ipv4.ip_forward)

    render_state
    log "Recorded rollback state in $STATE_FILE."
}

ensure_nftables() {
    if command -v nft >/dev/null 2>&1; then
        return 0
    fi
    require_command apt-get
    log "Installing the required nftables package."
    DEBIAN_FRONTEND=noninteractive apt-get install -y nftables
    require_command nft
}

render_nft_rules() {
    local table_name="$1"
    local content

    printf -v content '%s\n' \
        "table inet $table_name {" \
        $'\tchain forward {' \
        $'\t\ttype filter hook forward priority 0; policy drop;' \
        $'\t\t'"iifname \"$LAN_INTERFACE\" oifname \"$WIFI_INTERFACE\" ip saddr 10.0.0.0/24 ct state new,established,related accept" \
        $'\t\t'"iifname \"$WIFI_INTERFACE\" oifname \"$LAN_INTERFACE\" ip daddr 10.0.0.0/24 ct state established,related accept" \
        $'\t}' \
        "" \
        $'\tchain postrouting {' \
        $'\t\ttype nat hook postrouting priority 100; policy accept;' \
        $'\t\t'"ip saddr 10.0.0.0/24 oifname \"$WIFI_INTERFACE\" masquerade" \
        $'\t}' \
        "}"
    printf '%s' "$content"
}

install_runtime_files() {
    local nft_binary nft_content nft_check_content nft_check_file
    local sysctl_content unit_content dropin_content

    nft_binary=$(command -v nft)
    [[ "$nft_binary" == /* ]] || fail "nft resolved to a non-absolute path: $nft_binary" || return

    atomic_install_file "${BASH_SOURCE[0]}" "$INSTALLED_SCRIPT" 0755

    sysctl_content=$'# Managed by thelastgameofmoyn-configure-internet-sharing.\nnet.ipv4.ip_forward = 1'
    atomic_write_content "$SYSCTL_FILE" 0644 "$sysctl_content"

    nft_content=$(render_nft_rules "$NFT_TABLE")
    nft_check_content=$(render_nft_rules "${NFT_TABLE}_syntax_check")
    nft_check_file=$(mktemp "$CONFIG_ROOT/.internet-sharing-nft-check.XXXXXX")
    printf '%s\n' "$nft_check_content" > "$nft_check_file"
    if ! nft -c -f "$nft_check_file"; then
        rm -f -- "$nft_check_file"
        fail "generated nftables rules failed syntax validation."
        return
    fi
    rm -f -- "$nft_check_file"
    atomic_write_content "$NFT_RULES_FILE" 0644 "$nft_content"

    printf -v unit_content '%s\n' \
        "[Unit]" \
        "Description=Shares a Pi Wi-Fi uplink over the dedicated Ethernet link" \
        "Wants=NetworkManager.service" \
        "After=NetworkManager.service nftables.service" \
        "PartOf=nftables.service" \
        "Before=sensorApp.service visualApp.service" \
        "" \
        "[Service]" \
        "Type=oneshot" \
        "ExecStartPre=-$nft_binary delete table inet $NFT_TABLE" \
        "ExecStart=$nft_binary -f $NFT_RULES_FILE" \
        "ExecReload=-$nft_binary delete table inet $NFT_TABLE" \
        "ExecReload=$nft_binary -f $NFT_RULES_FILE" \
        "ExecStop=-$nft_binary delete table inet $NFT_TABLE" \
        "RemainAfterExit=yes" \
        "TimeoutStartSec=15" \
        "TimeoutStopSec=10" \
        "" \
        "[Install]" \
        "WantedBy=multi-user.target"
    atomic_write_content "$UNIT_FILE" 0644 "$unit_content"

    printf -v dropin_content '%s\n' \
        "[Unit]" \
        "Wants=$UNIT_NAME" \
        "Before=$UNIT_NAME"
    atomic_write_content "$NFTABLES_DROPIN_FILE" 0644 "$dropin_content"
}

apply_networkmanager_settings() {
    local dns_csv

    dns_csv=${DNS_SERVERS// /,}
    nmcli connection modify uuid "$LAN_CONNECTION_UUID" \
        ipv4.gateway "$PEER_IP" \
        ipv4.route-metric "$FALLBACK_ROUTE_METRIC" \
        ipv4.never-default no \
        ipv4.dns "$dns_csv"

    nmcli connection modify uuid "$WIFI_CONNECTION_UUID" \
        connection.autoconnect yes \
        connection.autoconnect-retries 0 \
        ipv4.route-metric "$WIFI_ROUTE_METRIC"
}

restore_networkmanager_settings() {
    nmcli connection modify uuid "$LAN_CONNECTION_UUID" \
        ipv4.gateway "$ORIGINAL_LAN_GATEWAY" \
        ipv4.route-metric "$ORIGINAL_LAN_ROUTE_METRIC" \
        ipv4.never-default "$ORIGINAL_LAN_NEVER_DEFAULT" \
        ipv4.dns "$ORIGINAL_LAN_DNS"

    nmcli connection modify uuid "$WIFI_CONNECTION_UUID" \
        connection.autoconnect "$ORIGINAL_WIFI_AUTOCONNECT" \
        connection.autoconnect-retries "$ORIGINAL_WIFI_AUTOCONNECT_RETRIES" \
        ipv4.route-metric "$ORIGINAL_WIFI_ROUTE_METRIC"
}

reapply_profile() {
    local interface_name="$1"
    local expected_uuid="$2"
    local activate_when_inactive="$3"
    local active_uuid

    active_uuid=$(active_uuid_for_device "$interface_name")
    if [[ "$active_uuid" == "$expected_uuid" ]]; then
        if ! nmcli device reapply "$interface_name"; then
            warn "could not reapply $interface_name without disconnecting it; reboot required."
            REBOOT_REQUIRED=1
        fi
    elif [[ "$activate_when_inactive" == "1" ]]; then
        if ! nmcli connection up uuid "$expected_uuid" ifname "$interface_name"; then
            warn "could not activate $interface_name; reboot or activate its profile manually."
            REBOOT_REQUIRED=1
        fi
    else
        log "$interface_name is not using the configured Wi-Fi profile; it will apply on its next activation."
    fi
}

reapply_network_profiles() {
    reapply_profile "$LAN_INTERFACE" "$LAN_CONNECTION_UUID" 1
    reapply_profile "$WIFI_INTERFACE" "$WIFI_CONNECTION_UUID" 0
}

check_equal() {
    local label="$1"
    local actual="$2"
    local expected="$3"

    if [[ "$actual" == "$expected" ]]; then
        log "OK: $label = ${actual:-<empty>}"
    else
        echo "[internet-sharing] ERROR: $label is ${actual:-<empty>}; expected ${expected:-<empty>}." >&2
        CHECK_FAILURES=$((CHECK_FAILURES + 1))
    fi
}

check_configuration() {
    local allow_pending_active_state="${1:-0}"
    local routes connectivity forward_hooks dns configured_dns

    CHECK_FAILURES=0
    verify_role_address || CHECK_FAILURES=$((CHECK_FAILURES + 1))
    verify_profile_interfaces || CHECK_FAILURES=$((CHECK_FAILURES + 1))
    verify_wifi_address_contract || CHECK_FAILURES=$((CHECK_FAILURES + 1))

    check_equal "net.ipv4.ip_forward" "$(sysctl -n net.ipv4.ip_forward)" "1"
    check_equal "Pi-link gateway" "$(nm_get "$LAN_CONNECTION_UUID" ipv4.gateway)" "$PEER_IP"
    check_equal "Pi-link route metric" "$(nm_get "$LAN_CONNECTION_UUID" ipv4.route-metric)" "$FALLBACK_ROUTE_METRIC"
    check_equal "Pi-link never-default" "$(nm_get "$LAN_CONNECTION_UUID" ipv4.never-default)" "no"
    check_equal "Wi-Fi route metric" "$(nm_get "$WIFI_CONNECTION_UUID" ipv4.route-metric)" "$WIFI_ROUTE_METRIC"
    check_equal "Wi-Fi autoconnect" "$(nm_get "$WIFI_CONNECTION_UUID" connection.autoconnect)" "yes"
    check_equal "Wi-Fi autoconnect retries" \
        "$(nm_get "$WIFI_CONNECTION_UUID" connection.autoconnect-retries)" "0"
    check_equal "Wi-Fi IPv4 method (unchanged)" \
        "$(nm_get "$WIFI_CONNECTION_UUID" ipv4.method)" "$ORIGINAL_WIFI_METHOD"
    check_equal "Wi-Fi IPv4 addresses (unchanged)" \
        "$(nm_get "$WIFI_CONNECTION_UUID" ipv4.addresses)" "$ORIGINAL_WIFI_ADDRESSES"
    check_equal "Wi-Fi IPv4 gateway (unchanged)" \
        "$(nm_get "$WIFI_CONNECTION_UUID" ipv4.gateway)" "$ORIGINAL_WIFI_GATEWAY"

    configured_dns=$(nm_get "$LAN_CONNECTION_UUID" ipv4.dns)
    for dns in $DNS_SERVERS; do
        if [[ ",$configured_dns," != *",$dns,"* ]]; then
            echo "[internet-sharing] ERROR: Pi-link DNS is missing $dns: $configured_dns" >&2
            CHECK_FAILURES=$((CHECK_FAILURES + 1))
        fi
    done

    if systemctl is-enabled --quiet "$UNIT_NAME"; then
        log "OK: $UNIT_NAME is enabled."
    else
        echo "[internet-sharing] ERROR: $UNIT_NAME is not enabled." >&2
        CHECK_FAILURES=$((CHECK_FAILURES + 1))
    fi
    if systemctl is-active --quiet "$UNIT_NAME"; then
        log "OK: $UNIT_NAME is active."
    else
        echo "[internet-sharing] ERROR: $UNIT_NAME is not active." >&2
        CHECK_FAILURES=$((CHECK_FAILURES + 1))
    fi
    if nft list table inet "$NFT_TABLE" >/dev/null 2>&1; then
        log "OK: nftables table $NFT_TABLE is loaded."
    else
        echo "[internet-sharing] ERROR: nftables table $NFT_TABLE is not loaded." >&2
        CHECK_FAILURES=$((CHECK_FAILURES + 1))
    fi

    routes=$(ip -4 route show default)
    if grep -Eq "^default via ${PEER_IP//./\\.} dev $LAN_INTERFACE([[:space:]].*)?metric $FALLBACK_ROUTE_METRIC([[:space:]]|$)" \
        <<< "$routes"; then
        log "OK: active fallback route uses $PEER_IP via $LAN_INTERFACE."
    elif [[ "$allow_pending_active_state" == "1" ]]; then
        warn "active fallback route is pending until reboot; persistent profile values are correct."
    else
        echo "[internet-sharing] ERROR: active fallback route is missing:" >&2
        sed 's/^/  /' <<< "$routes" >&2
        CHECK_FAILURES=$((CHECK_FAILURES + 1))
    fi

    forward_hooks=$(nft list ruleset 2>/dev/null | grep -c 'hook forward' || true)
    if (( forward_hooks > 1 )); then
        warn "another forward base chain exists; a separate policy-drop firewall can still reject shared traffic."
    fi

    connectivity=$(nmcli networking connectivity 2>/dev/null || true)
    log "NetworkManager connectivity state: ${connectivity:-unknown}"
    if [[ "$connectivity" == "unknown" ]]; then
        warn "failover reacts to a disconnected Wi-Fi link, not necessarily to a dead upstream behind an associated hotspot."
    fi

    if command -v ping >/dev/null 2>&1; then
        if ping -c 1 -W 2 "$PEER_IP" >/dev/null 2>&1; then
            log "OK: peer $PEER_IP answers on the dedicated link."
        else
            warn "peer $PEER_IP did not answer; configuration is present but end-to-end sharing is unverified."
        fi
    fi

    (( CHECK_FAILURES == 0 ))
}

apply_configuration() {
    local requested_role="$1"

    prepare_state "$requested_role"
    verify_role_address
    verify_profile_interfaces
    verify_wifi_address_contract

    # Configure the fallback route before installing nftables. On the second
    # Pi this lets apt reach the Internet through an already configured peer.
    apply_networkmanager_settings
    reapply_network_profiles
    ensure_nftables
    install_runtime_files

    sysctl -p "$SYSCTL_FILE" >/dev/null
    systemctl daemon-reload
    systemctl enable "$UNIT_NAME" >/dev/null
    systemctl restart "$UNIT_NAME"

    if ! check_configuration "$REBOOT_REQUIRED"; then
        fail "configuration was written but its active-state check failed."
        return 1
    fi

    log "$ROLE now prefers its own Wi-Fi and can route through $PEER_IP as fallback."
    if [[ "$REBOOT_REQUIRED" == "1" ]]; then
        warn "persistent settings are complete, but reboot this Pi before acceptance."
        return 3
    fi
}

remove_configuration() {
    if [[ ! -e "$STATE_FILE" && ! -L "$STATE_FILE" ]]; then
        log "No managed Internet-sharing state is installed."
        return 0
    fi

    load_state
    restore_networkmanager_settings
    reapply_network_profiles

    systemctl disable --now "$UNIT_NAME" >/dev/null 2>&1 || true
    if command -v nft >/dev/null 2>&1; then
        nft delete table inet "$NFT_TABLE" >/dev/null 2>&1 || true
    fi

    remove_managed_file "$UNIT_FILE"
    remove_managed_file "$NFTABLES_DROPIN_FILE"
    remove_managed_file "$NFT_RULES_FILE"
    remove_managed_file "$SYSCTL_FILE"
    rmdir -- "$NFTABLES_DROPIN_DIR" >/dev/null 2>&1 || true

    sysctl -w "net.ipv4.ip_forward=$ORIGINAL_IP_FORWARD" >/dev/null
    systemctl daemon-reload
    systemctl reset-failed "$UNIT_NAME" >/dev/null 2>&1 || true
    remove_managed_file "$STATE_FILE"

    log "Removed managed sharing rules and restored the recorded NetworkManager settings."
    if [[ "$REBOOT_REQUIRED" == "1" ]]; then
        warn "reboot this Pi to finish restoring the active network state."
        return 3
    fi
}

case "$ACTION" in
    -h|--help)
        usage
        exit 0
        ;;
    --apply)
        if (( $# > 2 )) || [[ -n "$ROLE_ARGUMENT" \
            && "$ROLE_ARGUMENT" != "sensorApp" && "$ROLE_ARGUMENT" != "visualApp" ]]; then
            usage
            exit 2
        fi
        ;;
    --check|--remove)
        if (( $# != 1 )); then
            usage
            exit 2
        fi
        ;;
    *)
        usage
        exit 2
        ;;
esac

require_root
validate_inputs
for command_name in ip nmcli sysctl systemctl install mktemp mv stat grep sed; do
    require_command "$command_name"
done

case "$ACTION" in
    --apply)
        apply_configuration "$ROLE_ARGUMENT"
        ;;
    --check)
        require_command nft
        load_state
        check_configuration
        ;;
    --remove)
        remove_configuration
        ;;
esac

#!/usr/bin/env bash

# Compatibility entry point for existing manual commands. The systemd update
# unit calls the root-owned orchestrator directly.
set -euo pipefail
exec /usr/local/sbin/thelastgameofmoyn-update-app visualApp "$@"

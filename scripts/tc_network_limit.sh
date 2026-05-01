#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  scripts/tc_network_limit.sh apply [options]
  scripts/tc_network_limit.sh show [options]
  scripts/tc_network_limit.sh clear [options]

Manual Linux tc helper for ORAM client/server network experiments.

Options:
  --dev IFACE                 Network interface to shape. Default: lo
  --bandwidth RATE            Egress bandwidth limit. Default: 50mbit
  --one-way-delay-ms MS       One-way netem delay. Default: 2.5
  --rtt-ms MS                 Set one-way delay to MS / 2
  --dry-run                   Print tc commands without executing them
  -h, --help                  Show this help

Notes:
  - tc shapes egress traffic on the selected interface.
  - For local client/server experiments over loopback, the default 2.5 ms
    one-way delay on lo approximates a 5 ms round trip.
  - For two-machine experiments, apply this script on the egress interface of
    each host with --rtt-ms 5, or use --one-way-delay-ms 5 on one egress side
    if that is the model you want.
  - clear removes the root qdisc installed on the selected interface.
EOF
}

die() {
    echo "error: $*" >&2
    exit 1
}

require_value() {
    local option="$1"
    local value="${2:-}"
    if [[ -z "$value" || "$value" == --* ]]; then
        die "$option requires a value"
    fi
}

format_half_rtt_ms() {
    local rtt_ms="$1"
    awk -v rtt="$rtt_ms" 'BEGIN {
        if (rtt <= 0) {
            exit 1
        }
        printf "%.3fms", rtt / 2
    }' || die "--rtt-ms must be positive"
}

print_cmd() {
    printf '+'
    for arg in "$@"; do
        printf ' %q' "$arg"
    done
    printf '\n'
}

run_cmd() {
    if [[ "$DRY_RUN" -eq 1 ]]; then
        print_cmd "$@"
    else
        "$@"
    fi
}

ensure_can_execute_tc() {
    if [[ "$DRY_RUN" -eq 1 ]]; then
        return
    fi
    command -v tc >/dev/null 2>&1 || die "tc is not installed; install iproute2"
    [[ "${EUID:-$(id -u)}" -eq 0 ]] || die "tc requires root; rerun with sudo or use --dry-run"
}

ACTION="${1:-}"
if [[ -z "$ACTION" || "$ACTION" == "-h" || "$ACTION" == "--help" ]]; then
    usage
    exit 0
fi
shift

DEV="lo"
BANDWIDTH="50mbit"
DELAY="2.5ms"
DRY_RUN=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dev)
            require_value "$1" "${2:-}"
            DEV="$2"
            shift 2
            ;;
        --bandwidth)
            require_value "$1" "${2:-}"
            BANDWIDTH="$2"
            shift 2
            ;;
        --one-way-delay-ms)
            require_value "$1" "${2:-}"
            DELAY="${2}ms"
            shift 2
            ;;
        --rtt-ms)
            require_value "$1" "${2:-}"
            DELAY="$(format_half_rtt_ms "$2")"
            shift 2
            ;;
        --dry-run)
            DRY_RUN=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "unknown option: $1"
            ;;
    esac
done

ensure_can_execute_tc

case "$ACTION" in
    apply)
        run_cmd tc qdisc replace dev "$DEV" root handle 1: htb default 10
        run_cmd tc class add dev "$DEV" parent 1: classid 1:10 htb rate "$BANDWIDTH" ceil "$BANDWIDTH"
        run_cmd tc qdisc add dev "$DEV" parent 1:10 handle 10: netem delay "$DELAY"
        ;;
    show)
        run_cmd tc qdisc show dev "$DEV"
        run_cmd tc class show dev "$DEV"
        ;;
    clear)
        if [[ "$DRY_RUN" -eq 1 ]]; then
            run_cmd tc qdisc del dev "$DEV" root
        elif ! tc qdisc del dev "$DEV" root; then
            echo "warning: no root qdisc removed from $DEV" >&2
        fi
        ;;
    *)
        die "unknown action: $ACTION"
        ;;
esac

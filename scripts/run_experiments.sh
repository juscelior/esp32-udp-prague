#!/usr/bin/env bash
# =============================================================================
# run_experiments.sh — Orchestrates ESP32 UDP Prague experiments from Mac
# =============================================================================
#
# Topology:
#   [ESP32] --WiFi--> [Raspberry Pi (gateway)] --eth0--> [Azure (receiver)]
#             wlan0          NAT/forward              udp_prague_receiver
#
# This script runs on the Mac and:
#   1. Configures the Raspberry Pi gateway (tc qdisc, sysctl) via SSH
#   2. Starts the UDP receiver on Azure via SSH
#   3. Modifies ESP32 source code for the scenario config
#   4. Flashes ESP32 via USB (pio run -t upload)
#   5. Captures ESP32 serial log (pio device monitor)
#   6. Waits for test to complete, collects all logs
#
# Usage:
#   ./run_experiments.sh --list              # list all scenarios
#   ./run_experiments.sh --status            # show which have data
#   ./run_experiments.sh --scenario t06      # run matching scenarios
#   ./run_experiments.sh --all               # run all 36 scenarios
#
# =============================================================================

set -uo pipefail

# --- SSH Configuration ---
RPI_HOST="pi@192.168.10.132"
RPI_PASS="102030"

# SSH wrapper for Raspberry Pi using expect (handles SSH password + sudo -S)
rpi_ssh() {
    local cmd="$1"
    # Pipe password to sudo -S via echo; ssh -tt forces pseudo-terminal
    local wrapped_cmd
    wrapped_cmd=$(echo "$cmd" | sed "s/sudo /echo '$RPI_PASS' | sudo -S /g")
    expect -c "
        set timeout 30
        spawn ssh -o StrictHostKeyChecking=no $RPI_HOST {$wrapped_cmd}
        expect {
            \"*assword*\" { send \"$RPI_PASS\r\"; exp_continue }
            eof
        }
    " 2>/dev/null
}

AZURE_HOST="azureuser@20.62.8.146"
AZURE_KEY="$HOME/Downloads/l4s_key.pem"
AZURE_SSH="ssh -i $AZURE_KEY $AZURE_HOST"

# --- Paths ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
ESP32_DIR="$PROJECT_DIR/esp32dev"
ESP32_SRC="$ESP32_DIR/src/UDPPragueClient.ino"
EXPERIMENTS_DIR="$PROJECT_DIR/experiments"

# --- Receiver config ---
RECEIVER_PORT=5005
AZURE_RECEIVER_PATH="~/esp32-udp-prague/udp_prague_base/udp_prague_receiver"

# --- Raspberry Pi network interfaces ---
RPI_WAN_IFACE="eth0"   # towards Azure
RPI_LAN_IFACE="wlan0"  # towards ESP32

# --- Timing ---
TEST_DURATION_SEC=600
MARGIN_SEC=30
COOLDOWN_SEC=15
REPETITIONS=${REPETITIONS:-1}

# --- All 36 scenarios ---
SCENARIOS=(
    "t01-ecn-sender-ect0-prague-fqcodel-ecn0"
    "t01-ecn-sender-ect1-prague-fqcodel-ecn0"
    "t02-ecn-sender-ect0-prague-fqcodel-ecn1"
    "t02-ecn-sender-ect1-prague-fqcodel-ecn1"
    "t03-ecn-sender-ect0-prague-fqcodel-ecn2"
    "t03-ecn-sender-ect1-prague-fqcodel-ecn2"
    "t04-ecn-sender-ect0-prague-dualpi2-ecn0"
    "t04-ecn-sender-ect1-prague-dualpi2-ecn0"
    "t05-ecn-sender-ect0-prague-dualpi2-ecn1"
    "t05-ecn-sender-ect1-prague-dualpi2-ecn1"
    "t06-ecn-sender-ect0-prague-dualpi2-ecn2"
    "t06-ecn-sender-ect1-prague-dualpi2-ecn2"
    "t07-ecn-sender-ect0-cubic-fqcodel-ecn0"
    "t07-ecn-sender-ect1-cubic-fqcodel-ecn0"
    "t08-ecn-sender-ect0-cubic-fqcodel-ecn1"
    "t08-ecn-sender-ect1-cubic-fqcodel-ecn1"
    "t09-ecn-sender-ect0-cubic-fqcodel-ecn2"
    "t09-ecn-sender-ect1-cubic-fqcodel-ecn2"
    "t10-ecn-sender-ect0-cubic-dualpi2-ecn0"
    "t10-ecn-sender-ect1-cubic-dualpi2-ecn0"
    "t11-ecn-sender-ect0-cubic-dualpi2-ecn1"
    "t11-ecn-sender-ect1-cubic-dualpi2-ecn1"
    "t12-ecn-sender-ect0-cubic-dualpi2-ecn2"
    "t12-ecn-sender-ect1-cubic-dualpi2-ecn2"
    "t13-ecn-sender-ect0-reno-fqcodel-ecn0"
    "t13-ecn-sender-ect1-reno-fqcodel-ecn0"
    "t14-ecn-sender-ect0-reno-fqcodel-ecn1"
    "t14-ecn-sender-ect1-reno-fqcodel-ecn1"
    "t15-ecn-sender-ect0-reno-fqcodel-ecn2"
    "t15-ecn-sender-ect1-reno-fqcodel-ecn2"
    "t16-ecn-sender-ect0-reno-dualpi2-ecn0"
    "t16-ecn-sender-ect1-reno-dualpi2-ecn0"
    "t17-ecn-sender-ect0-reno-dualpi2-ecn1"
    "t17-ecn-sender-ect1-reno-dualpi2-ecn1"
    "t18-ecn-sender-ect0-reno-dualpi2-ecn2"
    "t18-ecn-sender-ect1-reno-dualpi2-ecn2"
)

# --- Helpers ---
log_info()  { echo "[$(date '+%H:%M:%S')] INFO  $*"; }
log_warn()  { echo "[$(date '+%H:%M:%S')] WARN  $*" >&2; }
log_error() { echo "[$(date '+%H:%M:%S')] ERROR $*" >&2; }

parse_scenario() {
    local name="$1"
    IFS='-' read -ra parts <<< "$name"
    S_TEST_NUM="${parts[0]}"
    S_ECT="${parts[3]}"         # ect0 or ect1
    S_CC_ALGO="${parts[4]}"     # prague, cubic, reno
    S_QDISC="${parts[5]}"       # fqcodel or dualpi2
    S_GW_ECN="${parts[6]}"      # ecn0, ecn1, ecn2

    # Derived values
    S_ECN_ENABLE=0
    [[ "$S_ECT" == "ect1" ]] && S_ECN_ENABLE=1

    # ECN sysctl value: ecn0=0, ecn1=1, ecn2=2
    S_ECN_SYSCTL="${S_GW_ECN//ecn/}"

    # Qdisc name for tc: fqcodel -> fq_codel, dualpi2 -> dualpi2
    S_TC_QDISC="$S_QDISC"
    [[ "$S_QDISC" == "fqcodel" ]] && S_TC_QDISC="fq_codel"
}

# --- Step 1: Configure Raspberry Pi gateway ---
configure_gateway() {
    local scenario="$1"
    parse_scenario "$scenario"

    log_info "Configuring gateway: cc=$S_CC_ALGO qdisc=$S_TC_QDISC ecn=$S_ECN_SYSCTL"

    rpi_ssh "sudo /usr/sbin/sysctl -w net.ipv4.tcp_congestion_control=$S_CC_ALGO"
    rpi_ssh "sudo /usr/sbin/sysctl -w net.ipv4.tcp_ecn=$S_ECN_SYSCTL"
    rpi_ssh "sudo /usr/sbin/tc qdisc replace dev $RPI_WAN_IFACE root $S_TC_QDISC"
    rpi_ssh "sudo /usr/sbin/tc qdisc replace dev $RPI_LAN_IFACE root $S_TC_QDISC"

    # Verify
    log_info "Verifying gateway config..."
    rpi_ssh "/usr/sbin/sysctl -n net.ipv4.tcp_congestion_control && /usr/sbin/sysctl -n net.ipv4.tcp_ecn && /usr/sbin/tc qdisc show dev $RPI_WAN_IFACE && /usr/sbin/tc qdisc show dev $RPI_LAN_IFACE"

    log_info "Gateway configured"
}

# --- Step 2: Start Azure receiver ---
start_receiver() {
    log_info "Starting receiver on Azure (port $RECEIVER_PORT)..."

    # Kill any existing receiver
    ssh -i "$AZURE_KEY" "$AZURE_HOST" "pkill -f udp_prague_receiver 2>/dev/null; true"
    sleep 1

    # Start receiver in background (without -v to avoid I/O backpressure)
    ssh -i "$AZURE_KEY" "$AZURE_HOST" "nohup $AZURE_RECEIVER_PATH -p $RECEIVER_PORT > /tmp/server_experiment.log 2>&1 &"

    # Verify it started
    sleep 2
    if ssh -i "$AZURE_KEY" "$AZURE_HOST" "pgrep -f udp_prague_receiver > /dev/null 2>&1"; then
        log_info "Receiver started on Azure"
    else
        log_error "Failed to start receiver on Azure!"
        return 1
    fi
}

stop_receiver() {
    log_info "Stopping receiver on Azure..."
    ssh -i "$AZURE_KEY" "$AZURE_HOST" "pkill -f udp_prague_receiver 2>/dev/null; true"
    sleep 1
}

fetch_server_log() {
    local dest="$1"
    log_info "Fetching server log..."
    scp -i "$AZURE_KEY" "$AZURE_HOST:/tmp/server_experiment.log" "$dest" 2>/dev/null
    if [[ -f "$dest" ]]; then
        local lines
        lines=$(wc -l < "$dest")
        log_info "Server log: $lines lines -> $dest"
    else
        log_warn "Failed to fetch server log"
    fi
}

# --- Step 3: Modify ESP32 source and flash ---
configure_and_flash_esp32() {
    local scenario="$1"
    parse_scenario "$scenario"

    log_info "Configuring ESP32: ECN=$S_ECN_ENABLE CC=$S_CC_ALGO QDISC=$S_TC_QDISC ECN_GW=$S_GW_ECN"

    # Modify ECN_SENDER_ENABLE
    sed -i '' "s/^#define ECN_SENDER_ENABLE .*/#define ECN_SENDER_ENABLE $S_ECN_ENABLE/" "$ESP32_SRC"

    # Modify GW_CC_ALGO
    sed -i '' "s/^const char\* GW_CC_ALGO .*/const char* GW_CC_ALGO = \"$S_CC_ALGO\";/" "$ESP32_SRC"

    # Modify GW_QDISC
    sed -i '' "s/^const char\* GW_QDISC .*/const char* GW_QDISC   = \"$S_TC_QDISC\";/" "$ESP32_SRC"

    # Verify changes
    log_info "ESP32 config:"
    grep -E "ECN_SENDER_ENABLE|GW_CC_ALGO|GW_QDISC" "$ESP32_SRC" | head -3

    # Build and flash
    log_info "Building and flashing ESP32..."
    cd "$ESP32_DIR"
    if pio run -t upload 2>&1 | tail -5; then
        log_info "ESP32 flashed successfully"
    else
        log_error "ESP32 flash failed!"
        return 1
    fi
    cd "$PROJECT_DIR"

    # Wait for ESP32 to boot and connect to WiFi
    log_info "Waiting 10s for ESP32 boot + WiFi connection..."
    sleep 10
}

# --- Step 4: Capture ESP32 serial log ---
capture_esp32_log() {
    local log_file="$1"
    local duration=$((TEST_DURATION_SEC + MARGIN_SEC))

    log_info "Capturing ESP32 serial log for ${duration}s -> $log_file"

    cd "$ESP32_DIR"
    # Use timeout to auto-stop after test duration + margin
    # pio device monitor outputs to stdout
    timeout "${duration}s" pio device monitor 2>/dev/null > "$log_file" || true
    cd "$PROJECT_DIR"

    if [[ -f "$log_file" ]]; then
        local lines
        lines=$(wc -l < "$log_file")
        local cstats
        cstats=$(grep -c "^CSTATS" "$log_file" || echo 0)
        log_info "ESP32 log: $lines lines ($cstats CSTATS samples)"
    else
        log_warn "No ESP32 log captured"
    fi
}

# --- Run a single scenario ---
run_scenario() {
    local scenario="$1"
    local rep="${2:-1}"
    local scenario_dir="$EXPERIMENTS_DIR/$scenario"

    parse_scenario "$scenario"
    mkdir -p "$scenario_dir"

    local log_suffix="_${rep}"

    log_info "╔══════════════════════════════════════════════════════════════╗"
    log_info "║ Scenario: $scenario (rep $rep/$REPETITIONS)"
    log_info "║ ECT=$S_ECT  CC=$S_CC_ALGO  QDISC=$S_TC_QDISC  GW_ECN=$S_GW_ECN"
    log_info "╚══════════════════════════════════════════════════════════════╝"

    # Step 1: Configure gateway
    configure_gateway "$scenario"

    # Step 2: Start receiver
    start_receiver

    # Step 3: Flash ESP32
    configure_and_flash_esp32 "$scenario"

    # Step 4: Capture serial log
    local client_log="$scenario_dir/esp_client${log_suffix}.log"
    local server_log="$scenario_dir/server${log_suffix}.log"
    capture_esp32_log "$client_log"

    # Step 5: Stop receiver and fetch log
    stop_receiver
    fetch_server_log "$server_log"

    # Summary
    log_info "────────────────────────────────────────────────────────────"
    log_info "Scenario $scenario (rep $rep) COMPLETE"
    [[ -f "$client_log" ]] && log_info "  Client: $(wc -l < "$client_log") lines"
    [[ -f "$server_log" ]] && log_info "  Server: $(wc -l < "$server_log") lines"
    log_info "────────────────────────────────────────────────────────────"

    # Cooldown
    if [[ $COOLDOWN_SEC -gt 0 ]]; then
        log_info "Cooldown: ${COOLDOWN_SEC}s..."
        sleep "$COOLDOWN_SEC"
    fi
}

# --- List / Status ---
list_scenarios() {
    echo "All 36 scenarios:"
    local prev_group=""
    for s in "${SCENARIOS[@]}"; do
        parse_scenario "$s"
        local group="${S_CC_ALGO}-${S_QDISC}"
        if [[ "$group" != "$prev_group" ]]; then
            echo "  --- $S_CC_ALGO + $S_TC_QDISC ---"
            prev_group="$group"
        fi
        local status="[ ]"
        [[ -f "$EXPERIMENTS_DIR/$s/esp_client.log" ]] && status="[x]"
        echo "    $status $s"
    done
}

show_status() {
    local total=0 done=0
    for s in "${SCENARIOS[@]}"; do
        total=$((total + 1))
        [[ -f "$EXPERIMENTS_DIR/$s/esp_client.log" ]] && done=$((done + 1))
    done
    echo ""
    echo "Progress: $done/$total scenarios with data"
    echo ""
    if [[ $done -gt 0 ]]; then
        echo "Completed:"
        for s in "${SCENARIOS[@]}"; do
            if [[ -f "$EXPERIMENTS_DIR/$s/esp_client.log" ]]; then
                local cstats
                cstats=$(grep -c "^CSTATS" "$EXPERIMENTS_DIR/$s/esp_client.log" 2>/dev/null || echo "0")
                echo "  $s  ($cstats samples)"
            fi
        done
    fi
    echo ""
    echo "Remaining: $((total - done)) scenarios"
}

# --- Main ---
case "${1:-}" in
    --list)
        list_scenarios
        ;;
    --status)
        show_status
        ;;
    --scenario)
        if [[ -z "${2:-}" ]]; then
            log_error "Usage: $0 --scenario <name_or_prefix>"
            exit 1
        fi
        matched=()
        for s in "${SCENARIOS[@]}"; do
            [[ "$s" == "$2"* || "$s" == *"$2"* ]] && matched+=("$s")
        done
        if [[ ${#matched[@]} -eq 0 ]]; then
            log_error "No scenario matching: $2"
            exit 1
        fi
        echo "Will run ${#matched[@]} scenario(s):"
        for s in "${matched[@]}"; do echo "  $s"; done
        echo ""
        echo "Press ENTER to start (Ctrl+C to cancel)..."
        read -r
        for s in "${matched[@]}"; do
            for rep in $(seq 1 "$REPETITIONS"); do
                run_scenario "$s" "$rep"
            done
        done
        log_info "All matched scenarios complete!"
        ;;
    --all)
        log_info "Running all ${#SCENARIOS[@]} scenarios ($REPETITIONS reps each)"
        echo "Press ENTER to start (Ctrl+C to cancel)..."
        read -r
        for s in "${SCENARIOS[@]}"; do
            for rep in $(seq 1 "$REPETITIONS"); do
                run_scenario "$s" "$rep"
            done
        done
        log_info "All scenarios complete!"
        ;;
    --monitor)
        if [[ -z "${2:-}" ]]; then
            log_error "Usage: $0 --monitor <scenario_name> [run_number]"
            log_error "Example: $0 --monitor t01-ecn-sender-ect0-prague-fqcodel-ecn0 2"
            exit 1
        fi
        run_num="${3:-1}"
        matched=()
        for s in "${SCENARIOS[@]}"; do
            [[ "$s" == "$2"* || "$s" == *"$2"* ]] && matched+=("$s")
        done
        if [[ ${#matched[@]} -eq 0 ]]; then
            log_error "No scenario matching: $2"
            exit 1
        fi
        if [[ ${#matched[@]} -gt 1 ]]; then
            log_error "Multiple matches — be more specific:"
            for s in "${matched[@]}"; do echo "  $s"; done
            exit 1
        fi
        scenario="${matched[0]}"
        scenario_dir="$EXPERIMENTS_DIR/$scenario"
        mkdir -p "$scenario_dir"
        client_log="$scenario_dir/esp_client_${run_num}.log"
        log_info "Monitoring ESP32 serial → $client_log (run $run_num)"
        log_info "Press Ctrl+C to stop"
        cd "$ESP32_DIR"
        pio device monitor | tee "$client_log"
        cd "$PROJECT_DIR"
        log_info "Log saved: $client_log ($(wc -l < "$client_log") lines)"
        ;;
    --help|-h)
        cat << 'HELP'
Usage: ./run_experiments.sh [OPTIONS]

Options:
  --list              List all 36 scenarios with completion status
  --status            Show progress summary
  --scenario <name>   Run matching scenario(s) (supports prefix/substring)
  --monitor <name> [N] Monitor ESP32 serial, save as esp_client_N.log (default N=1)
  --all               Run all scenarios sequentially
  --help              Show this help

Environment:
  REPETITIONS=3       Number of runs per scenario (default: 1)

Examples:
  ./run_experiments.sh --scenario t06              # All t06-* scenarios
  ./run_experiments.sh --monitor t07-ecn-sender-ect1-cubic-fqcodel-ecn0     # run 1
  ./run_experiments.sh --monitor t07-ecn-sender-ect1-cubic-fqcodel-ecn0 2   # run 2
  ./run_experiments.sh --monitor t07-ecn-sender-ect1-cubic-fqcodel-ecn0 3   # run 3
  REPETITIONS=3 ./run_experiments.sh --scenario t06
HELP
        ;;
    *)
        echo "ESP32 UDP Prague — Experiment Runner"
        echo ""
        show_status
        echo "Run: $0 --help"
        ;;
esac

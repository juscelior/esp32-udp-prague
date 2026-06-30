#!/usr/bin/env bash
# monitor_esp32_serial.sh — Monitor serial do ESP32 com preview e opção de salvar log
# Uso:
#   ./monitor_esp32_serial.sh <cenario> [num_teste]
#
# Exemplo preview (apenas console):
#   ./monitor_esp32_serial.sh t01-ecn-sender-ect1-prague-fqcodel-ecn0
#
# Exemplo salvando log:
#   ./monitor_esp32_serial.sh t01-ecn-sender-ect1-prague-fqcodel-ecn0 2

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
ESP32_DIR="$PROJECT_DIR/esp32dev"
EXPERIMENTS_DIR="$PROJECT_DIR/experiments"

if [[ $# -lt 1 ]]; then
    echo "Uso: $0 <cenario> [num_teste]"
    exit 1
fi

SCENARIO="$1"
RUN_NUM="${2:-}"
SCENARIO_DIR="$EXPERIMENTS_DIR/$SCENARIO"

cd "$ESP32_DIR"

if [[ -z "$RUN_NUM" ]]; then
    echo "[Preview] Monitorando serial do ESP32 (apenas console, não salva arquivo)..."
    pio device monitor
else
    mkdir -p "$SCENARIO_DIR"
    LOG_FILE="$SCENARIO_DIR/esp_client_${RUN_NUM}.log"
    echo "[Salvando] Monitorando serial do ESP32 e salvando em $LOG_FILE ..."
    pio device monitor | tee "$LOG_FILE"
fi

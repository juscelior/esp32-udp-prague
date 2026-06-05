#!/usr/bin/env bash
# configure_and_flash_only.sh — Configura o código .ino e faz upload para o ESP32 para um cenário escolhido
# Uso:
#   ./configure_and_flash_only.sh           # lista todos os cenários
#   ./configure_and_flash_only.sh <cenario> # configura e faz upload para o ESP32

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
ESP32_DIR="$PROJECT_DIR/esp32dev"
ESP32_SRC="$ESP32_DIR/src/UDPPragueClient.ino"

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

parse_scenario() {
    local name="$1"
    IFS='-' read -ra parts <<< "$name"
    S_TEST_NUM="${parts[0]}"
    S_ECT="${parts[3]}"         # ect0 or ect1
    S_CC_ALGO="${parts[4]}"     # prague, cubic, reno
    S_QDISC="${parts[5]}"       # fqcodel or dualpi2
    S_GW_ECN="${parts[6]}"      # ecn0, ecn1, ecn2

    S_ECN_ENABLE=0
    if [[ "$S_ECT" == "ect1" ]]; then
        S_ECN_ENABLE=1
    fi
    S_TC_QDISC="$S_QDISC"
    if [[ "$S_QDISC" == "fqcodel" ]]; then
        S_TC_QDISC="fq_codel"
    fi
}

configure_and_flash_esp32() {
    local scenario="$1"
    parse_scenario "$scenario"
    echo "Configurando ESP32: ECN=$S_ECN_ENABLE CC=$S_CC_ALGO QDISC=$S_TC_QDISC ECN_GW=$S_GW_ECN"
    sed -i '' "s/^#define ECN_SENDER_ENABLE .*/#define ECN_SENDER_ENABLE $S_ECN_ENABLE/" "$ESP32_SRC"
    sed -i '' "s/^const char\* GW_CC_ALGO .*/const char* GW_CC_ALGO = \"$S_CC_ALGO\";/" "$ESP32_SRC"
    sed -i '' "s/^const char\* GW_QDISC .*/const char* GW_QDISC   = \"$S_TC_QDISC\";/" "$ESP32_SRC"
    echo "Configuração aplicada. Fazendo build e upload..."
    cd "$ESP32_DIR"
    if pio run -t upload; then
        echo "ESP32 flash concluído com sucesso!"
    else
        echo "ERRO: Falha no build/upload do ESP32!" >&2
        exit 1
    fi
    cd "$PROJECT_DIR"
}

if [[ $# -eq 0 ]]; then
    echo "Selecione um dos cenários disponíveis:"
    select opt in "${SCENARIOS[@]}" "Sair"; do
        if [[ "$REPLY" =~ ^[0-9]+$ ]] && (( REPLY >= 1 && REPLY <= ${#SCENARIOS[@]} )); then
            SCENARIO="${SCENARIOS[$((REPLY-1))]}"
            break
        elif [[ "$REPLY" == $(( ${#SCENARIOS[@]} + 1 )) ]]; then
            echo "Saindo."
            exit 0
        else
            echo "Opção inválida. Tente novamente."
        fi
    done
else
    SCENARIO="$1"
    found=0
    for s in "${SCENARIOS[@]}"; do
        if [[ "$s" == "$SCENARIO" ]]; then
            found=1
            break
        fi
    done
    if [[ $found -eq 0 ]]; then
        echo "ERRO: Cenário '$SCENARIO' não encontrado."
        exit 1
    fi
fi

configure_and_flash_esp32 "$SCENARIO"

#!/usr/bin/env bash
# configure_and_flash_only.sh — Configura o código .ino e faz upload para o ESP32
#
# Regras automáticas aplicadas pelo nome do cenário:
#   ectN    -> ECN_SENDER_ENABLE = 0|1   (Not-ECT / ECT(1), requer o fix do snd_ecn)
#   prague  -> Prague CC: CC_MODE_BASELINE comentado, MAX_WINDOW_ESP32 = -1
#   cubic   -> Baseline:  CC_MODE_BASELINE ativo,     MAX_WINDOW_ESP32 = 10
#   reno    -> Baseline:  idem cubic
#
# Uso:
#   ./configure_and_flash_only.sh                # lista os cenários
#   ./configure_and_flash_only.sh <cenario>      # configura e faz upload
#   ./configure_and_flash_only.sh <cenario> 1    # idem + já inicia o monitor (run 1)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
ESP32_DIR="$PROJECT_DIR/esp32dev"
ESP32_SRC="$ESP32_DIR/src/UDPPragueClient.ino"

# Matriz final (8 condições úteis). Os demais cenários antigos variavam
# dimensões inertes (ecn0/1/2, CC TCP do gateway) e foram removidos.
SCENARIOS=(
    "t01-ecn-sender-ect0-prague-fqcodel-ecn0"   # Prague + FQ-CoDel + Not-ECT
    "t01-ecn-sender-ect1-prague-fqcodel-ecn0"   # Prague + FQ-CoDel + ECT(1)
    "t04-ecn-sender-ect0-prague-dualpi2-ecn0"   # Prague + DualPI2  + Not-ECT
    "t04-ecn-sender-ect1-prague-dualpi2-ecn0"   # Prague + DualPI2  + ECT(1)  [36 runs ja feitos]
    "t07-ecn-sender-ect0-cubic-fqcodel-ecn0"    # Baseline + FQ-CoDel + Not-ECT
    "t07-ecn-sender-ect1-cubic-fqcodel-ecn0"    # Baseline + FQ-CoDel + ECT(1)
    "t16-ecn-sender-ect0-reno-dualpi2-ecn0"     # Baseline + DualPI2  + Not-ECT
    "t16-ecn-sender-ect1-reno-dualpi2-ecn0"     # Baseline + DualPI2  + ECT(1)
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
    # prague -> CC adaptativo; cubic/reno -> modo baseline (janela fixa)
    S_BASELINE=0
    if [[ "$S_CC_ALGO" == "cubic" || "$S_CC_ALGO" == "reno" ]]; then
        S_BASELINE=1
    fi
}

configure_and_flash_esp32() {
    local scenario="$1"
    parse_scenario "$scenario"

    local mode="Prague CC (adaptativo)"
    [[ $S_BASELINE -eq 1 ]] && mode="BASELINE (janela fixa = 10)"
    echo "Configurando ESP32:"
    echo "  ECN sender : $S_ECN_ENABLE ($([[ $S_ECN_ENABLE -eq 1 ]] && echo 'ECT(1)' || echo 'Not-ECT'))"
    echo "  Modo CC    : $mode"
    echo "  Gateway    : CC=$S_CC_ALGO QDISC=$S_TC_QDISC ECN_GW=$S_GW_ECN"

    # --- ECN do remetente ---
    sed -i '' "s/^#define ECN_SENDER_ENABLE .*/#define ECN_SENDER_ENABLE $S_ECN_ENABLE/" "$ESP32_SRC"

    # --- Rótulos do gateway (documentação no log) ---
    sed -i '' "s/^const char\* GW_CC_ALGO .*/const char* GW_CC_ALGO = \"$S_CC_ALGO\";/" "$ESP32_SRC"
    sed -i '' "s/^const char\* GW_QDISC .*/const char* GW_QDISC   = \"$S_TC_QDISC\";/" "$ESP32_SRC"

    # --- Modo de CC do cliente + janela ---
    if [[ $S_BASELINE -eq 1 ]]; then
        # ativa CC_MODE_BASELINE (descomenta, se estiver comentado)
        sed -i '' "s|^// *#define CC_MODE_BASELINE|#define CC_MODE_BASELINE|" "$ESP32_SRC"
        # janela fixa de 10 pacotes (comparável à janela média do Prague)
        sed -i '' "s/^static const int MAX_WINDOW_ESP32 = .*/static const int MAX_WINDOW_ESP32 = 10;/" "$ESP32_SRC"
    else
        # desativa CC_MODE_BASELINE (comenta, se estiver ativo)
        sed -i '' "s|^#define CC_MODE_BASELINE|// #define CC_MODE_BASELINE|" "$ESP32_SRC"
        # sem limite local: janela governada pelo Prague
        sed -i '' "s/^static const int MAX_WINDOW_ESP32 = .*/static const int MAX_WINDOW_ESP32 = -1;/" "$ESP32_SRC"
    fi

    # --- Verificação do que ficou no arquivo ---
    echo "Estado aplicado no .ino:"
    grep -E "^(// )?#define CC_MODE_BASELINE|^#define ECN_SENDER_ENABLE|^static const int MAX_WINDOW_ESP32" "$ESP32_SRC" | sed 's/^/    /'

    echo "Fazendo build e upload..."
    cd "$ESP32_DIR"
    if pio run -t upload; then
        echo "ESP32 flash concluído com sucesso!"
    else
        echo "ERRO: Falha no build/upload do ESP32!" >&2
        exit 1
    fi
    cd "$PROJECT_DIR"

    echo
    echo "Lembretes:"
    echo "  RPi   : sudo ./set_test.sh ${S_TEST_NUM} (qdisc $S_TC_QDISC @512kbit)"
    echo "  Azure : pkill -f udp_prague_receiver; ./udp_prague_receiver -p 5005 > server_N.log 2>&1 &"
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
        echo "ERRO: Cenário '$SCENARIO' não está na matriz final."
        echo "Cenários válidos:"
        printf '  %s\n' "${SCENARIOS[@]}"
        exit 1
    fi
fi

configure_and_flash_esp32 "$SCENARIO"

# Se um número de run foi passado como 2º argumento, já inicia o monitor
# (o monitor reseta a placa ao conectar -> run começa do zero, log completo)
if [[ -n "${2:-}" ]]; then
    exec "$SCRIPT_DIR/monitor_esp32_serial.sh" "$SCENARIO" "$2"
fi
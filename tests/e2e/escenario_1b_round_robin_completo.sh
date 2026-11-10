#!/usr/bin/env bash
# Escenario 1 sin interrupciones: Round Robin (q=2) hasta el final.
# Al no pausar ni cambiar de política, el resultado es determinista y se
# compara exactamente con el cálculo manual.
set -u
BIN=$1
source "$(dirname "$0")/lib.sh"

OUTPUT=$(run_session "$BIN" <<'EOF'
set_sched RR 2
set_speed 50
create P1 5 2
create P2 3 1
create P3 2 3
start
@sleep 1.5
gantt
stats
pcb 2
exit
EOF
)

echo "Escenario 1b: RR q=2 completo"
expect_line "$OUTPUT" "DIAGRAMA DE GANTT (0 - 10 ticks):"
expect_line "$OUTPUT" "[0-2s: P1] -> [2-4s: P2] -> [4-6s: P3] -> [6-8s: P1] -> [8-9s: P2] -> [9-10s: P1]"
expect_line "$OUTPUT" "- Procesos Finalizados        : 3 / 3"
expect_line "$OUTPUT" "- Tiempo Promedio de Espera   : 5.00 ticks"
expect_line "$OUTPUT" "- Tiempo Promedio de Retorno  : 8.33 ticks"
expect_line "$OUTPUT" "- Tiempo Promedio de Respuesta: 2.00 ticks"
expect_line "$OUTPUT" "- Uso de CPU                  : 100.0%"
expect_line "$OUTPUT" "Tiempo de Retorno  : 9 ticks"
expect_line "$OUTPUT" "Tiempo de Respuesta: 2 ticks"

finish "$OUTPUT"

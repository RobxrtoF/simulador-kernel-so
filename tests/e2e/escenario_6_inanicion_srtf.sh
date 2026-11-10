#!/usr/bin/env bash
# Inanición en SRTF y envejecimiento opcional (issue #30), con workloads/inanicion_srtf.txt.
#   1) SRTF puro: Largo (ráfaga 6) no obtiene la CPU hasta que deja de llegar
#      el flujo de cortos (t=16). Espera 16 ticks.
#   2) set_aging 2: la ráfaga efectiva de Largo baja 1 por cada 2 ticks de
#      espera; en t=8 obtiene la CPU. Espera 8 ticks.
# Cada sesión es un proceso nuevo del simulador (22 ticks a 50 ms ~ 1,1 s).
set -u
BIN=$1
source "$(dirname "$0")/lib.sh"

PURO=$(run_session "$BIN" <<'EOF'
set_sched SRTF
set_speed 50
load workloads/inanicion_srtf.txt
start
@sleep 2.5
gantt
stats
pcb 1
exit
EOF
)

ENVEJECIMIENTO=$(run_session "$BIN" <<'EOF'
set_aging -1
set_aging dos
set_aging 2
set_sched SRTF
set_speed 50
load workloads/inanicion_srtf.txt
start
@sleep 2.5
gantt
stats
pcb 1
exit
EOF
)

echo "Escenario 6a: SRTF puro (inanición)"
expect_text "$PURO" "[OK]: 9 proceso(s) cargado(s) desde 'workloads/inanicion_srtf.txt'"
expect_line "$PURO" "DIAGRAMA DE GANTT (0 - 22 ticks):"
expect_line "$PURO" "[0-2s: Corto_1] -> [2-4s: Corto_2] -> [4-6s: Corto_3] -> [6-8s: Corto_4] -> [8-10s: Corto_5] -> [10-12s: Corto_6] -> [12-14s: Corto_7] -> [14-16s: Corto_8] -> [16-22s: Largo]"
expect_line "$PURO" "- Procesos Finalizados        : 9 / 9"
expect_line "$PURO" "- Tiempo Promedio de Espera   : 1.78 ticks"
expect_line "$PURO" "- Tiempo Promedio de Retorno  : 4.22 ticks"
expect_line "$PURO" "Tiempo de Espera   : 16 ticks"
expect_line "$PURO" "Tiempo de Respuesta: 16 ticks"

echo
echo "Escenario 6b: SRTF con envejecimiento cada 2 ticks"
expect_text "$ENVEJECIMIENTO" "[ERROR DE PARAMETRO]: El intervalo de envejecimiento debe ser un entero mayor o igual que 0"
expect_text "$ENVEJECIMIENTO" "[ERROR DE TIPO]: El intervalo de envejecimiento debe ser un número entero de ticks."
expect_line "$ENVEJECIMIENTO" "[OK]: Política de planificación: SRTF (apropiativo, envejecimiento cada 2 ticks)."
expect_line "$ENVEJECIMIENTO" "[0-2s: Corto_1] -> [2-4s: Corto_2] -> [4-6s: Corto_3] -> [6-8s: Corto_4] -> [8-14s: Largo] -> [14-16s: Corto_5] -> [16-18s: Corto_6] -> [18-20s: Corto_7] -> [20-22s: Corto_8]"
expect_line "$ENVEJECIMIENTO" "- Procesos Finalizados        : 9 / 9"
expect_line "$ENVEJECIMIENTO" "- Tiempo Promedio de Espera   : 3.56 ticks"
expect_line "$ENVEJECIMIENTO" "- Tiempo Promedio de Retorno  : 6.00 ticks"
expect_line "$ENVEJECIMIENTO" "Tiempo de Espera   : 8 ticks"
expect_line "$ENVEJECIMIENTO" "Tiempo de Respuesta: 8 ticks"

finish "$PURO

$ENVEJECIMIENTO"

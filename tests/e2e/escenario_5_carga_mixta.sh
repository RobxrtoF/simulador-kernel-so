#!/usr/bin/env bash
# Carga mixta (issue #31), con workloads/carga_mixta.txt: llegadas escalonadas,
# prioridades distintas y un periodo ocioso (Respaldo llega en t=20, cuando la
# CPU ya terminó todo lo demás). Resultados calculados a mano:
#
#   Proceso      Ráfaga  Prioridad  Llegada
#   Editor          6        2         0
#   Compilador      3        1         2
#   Navegador       4        3         4
#   Reproductor     2        4         5
#   Respaldo        2        5        20
#
#   1) RR con quantum 2. Al agotar su quantum, el proceso vuelve al final de la
#      cola detrás de los que llegaron en ese mismo instante (t=2: Compilador
#      antes que Editor; t=4: Navegador antes que Compilador).
#      Espera: 7, 4, 7, 4, 0 -> 4.40 | Retorno: 13, 7, 11, 6, 2 -> 7.80
#      Respuesta: 0, 0, 2, 4, 0 -> 1.20 | CPU: 17 de 22 ticks -> 77.3%
#   2) SRTF. En t=2 Compilador (3) desaloja a Editor (le quedan 4). En t=7
#      Editor y Navegador empatan con 4 restantes: gana Editor por prioridad
#      (2 frente a 3).
#      Espera: 5, 0, 7, 0, 0 -> 2.40 | Retorno: 11, 3, 11, 2, 2 -> 5.80
#      Respuesta: 0, 0, 7, 0, 0 -> 1.40 | CPU: 17 de 22 ticks -> 77.3%
#   En ambos casos la CPU queda ociosa (IDLE) entre t=15 y t=20.
# Cada sesión es un proceso nuevo del simulador (22 ticks a 50 ms ~ 1,1 s).
set -u
BIN=$1
source "$(dirname "$0")/lib.sh"

RR=$(run_session "$BIN" <<'EOF'
set_sched RR 2
set_speed 50
load workloads/carga_mixta.txt
start
@sleep 2.5
gantt
stats
pcb 3
exit
EOF
)

SRTF=$(run_session "$BIN" <<'EOF'
set_sched SRTF
set_speed 50
load workloads/carga_mixta.txt
start
@sleep 2.5
gantt
stats
pcb 3
exit
EOF
)

echo "Escenario 5a: carga mixta con Round Robin (quantum 2)"
expect_text "$RR" "[OK]: 5 proceso(s) cargado(s) desde 'workloads/carga_mixta.txt'"
expect_line "$RR" "DIAGRAMA DE GANTT (0 - 22 ticks):"
expect_line "$RR" "[0-2s: Editor] -> [2-4s: Compilador] -> [4-6s: Editor] -> [6-8s: Navegador] -> [8-9s: Compilador] -> [9-11s: Reproductor] -> [11-13s: Editor] -> [13-15s: Navegador] -> [15-20s: IDLE] -> [20-22s: Respaldo]"
expect_line "$RR" "- Procesos Finalizados        : 5 / 5"
expect_line "$RR" "- Tiempo Promedio de Espera   : 4.40 ticks"
expect_line "$RR" "- Tiempo Promedio de Retorno  : 7.80 ticks"
expect_line "$RR" "- Tiempo Promedio de Respuesta: 1.20 ticks"
expect_line "$RR" "- Uso de CPU                  : 77.3%"
expect_line "$RR" "Tiempo de Espera   : 7 ticks"
expect_line "$RR" "Tiempo de Respuesta: 2 ticks"

echo
echo "Escenario 5b: carga mixta con SRTF"
expect_line "$SRTF" "[0-2s: Editor] -> [2-5s: Compilador] -> [5-7s: Reproductor] -> [7-11s: Editor] -> [11-15s: Navegador] -> [15-20s: IDLE] -> [20-22s: Respaldo]"
expect_line "$SRTF" "- Procesos Finalizados        : 5 / 5"
expect_line "$SRTF" "- Tiempo Promedio de Espera   : 2.40 ticks"
expect_line "$SRTF" "- Tiempo Promedio de Retorno  : 5.80 ticks"
expect_line "$SRTF" "- Tiempo Promedio de Respuesta: 1.40 ticks"
expect_line "$SRTF" "- Uso de CPU                  : 77.3%"
expect_line "$SRTF" "Tiempo de Espera   : 7 ticks"
expect_line "$SRTF" "Tiempo de Respuesta: 7 ticks"

finish "$RR

$SRTF"

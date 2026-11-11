#!/usr/bin/env bash
# Escenario 1 del enunciado: simulación manual e interactiva.
#   set_sched RR 2, crear P1/P2/P3, start, inspeccionar con ps, comandos mal
#   formados durante la ejecución, pause + pcb 1, cambio en caliente a SRTF,
#   start y verificación final con gantt y stats.
# Se usa 200 ms/tick (en vez de 1000) para que la prueba sea rápida; la lógica
# es idéntica porque el tiempo del sistema se mide en ticks.
set -u
BIN=$1
source "$(dirname "$0")/lib.sh"

OUTPUT=$(run_session "$BIN" <<'EOF'
set_sched RR 2
set_speed 200
create P1 5 2
create P2 3 1
create P3 2 3
start
@sleep 0.5
ps
create BadProcess
pcb xyz
set_sched FIFO
@sleep 0.2
pause
pcb 1
set_sched SRTF
start
@sleep 2.6
gantt
stats
exit
EOF
)
STATUS=$?

echo "Escenario 1: RR q=2 -> pausa -> SRTF en caliente"
expect_text "$OUTPUT" "[OK]: Política de planificación: RR (quantum = 2)."
expect_text "$OUTPUT" "[OK]: Proceso 'P3' creado con PID 3"
expect_text "$OUTPUT" "RUNNING"
expect_line "$OUTPUT" "[ERROR DE SINTAXIS]: Parámetros insuficientes."
expect_line "$OUTPUT" "Uso correcto: create <nombre> <burst_time> [prioridad]"
expect_line "$OUTPUT" "[ERROR DE TIPO]: El PID debe ser un número entero positivo."
expect_line "$OUTPUT" "[ERROR DE PARAMETRO]: Algoritmo no reconocido. Algoritmos válidos: RR, SRTF."
expect_text "$OUTPUT" "        CONTROL BLOCK DE PROCESO (PCB) - PID: 1"
expect_text "$OUTPUT" "[OK]: Política de planificación: SRTF (apropiativo)."
expect_text "$OUTPUT" "[0-2s: P1] -> [2-"
expect_line "$OUTPUT" "- Procesos Finalizados        : 3 / 3"
expect_line "$OUTPUT" "- Uso de CPU                  : 100.0%"
expect_line "$OUTPUT" "[OK]: Hilos detenidos y memoria liberada (3 PCB). Hasta luego."
reject_text "$OUTPUT" "Segmentation fault"
[[ $STATUS -eq 0 ]] || { echo "  [X]  código de salida $STATUS"; FAILURES=$((FAILURES + 1)); }

finish "$OUTPUT"

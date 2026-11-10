#!/usr/bin/env bash
# Escenario 2 del enunciado: carga por lotes con workload_test.txt y SRTF.
#   - En t=1 llega Job_B (BT=2) y desaloja de inmediato a Job_A (le quedan 3).
#   - La secuencia del Gantt debe ser exactamente:
#     [0-1s: Job_A] -> [1-3s: Job_B] -> [3-4s: Job_C] -> [4-7s: Job_A]
# Para observar el desalojo se pausa en t=2 (reloj de 500 ms) y luego se
# acelera a 50 ms/tick para terminar rápido.
set -u
BIN=$1
source "$(dirname "$0")/lib.sh"

OUTPUT=$(run_session "$BIN" <<'EOF'
set_sched SRTF
set_speed 500
load workloads/workload_test.txt
start
@sleep 0.75
pause
ps
pcb 1
set_speed 50
start
@sleep 0.8
gantt
stats
exit
EOF
)

echo "Escenario 2: lotes + SRTF"
expect_text "$OUTPUT" "[OK]: 3 proceso(s) cargado(s) desde 'workloads/workload_test.txt'"
expect_line "$OUTPUT" "[OK]: Reloj de CPU en pausa en t=2. Use 'start' para reanudar."
expect_text "$OUTPUT" "1       Job_A           READY         3s                2"
expect_text "$OUTPUT" "2       Job_B           RUNNING       1s                1"
expect_line "$OUTPUT" "Ráfaga Restante    : 3 ticks"
expect_line "$OUTPUT" "DIAGRAMA DE GANTT (0 - 7 ticks):"
expect_line "$OUTPUT" "[0-1s: Job_A] -> [1-3s: Job_B] -> [3-4s: Job_C] -> [4-7s: Job_A]"
expect_line "$OUTPUT" "- Procesos Finalizados        : 3 / 3"
expect_line "$OUTPUT" "- Tiempo Promedio de Espera   : 1.00 ticks"
expect_line "$OUTPUT" "- Tiempo Promedio de Retorno  : 3.33 ticks"
expect_line "$OUTPUT" "- Uso de CPU                  : 100.0%"

finish "$OUTPUT"

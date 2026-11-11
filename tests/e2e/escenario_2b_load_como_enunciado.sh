#!/usr/bin/env bash
# Escenario 2 escrito al pie de la letra: el enunciado pide 'load workload_test.txt'
# y en el repositorio la traza está en workloads/. load la busca allí, lo avisa
# con [INFO] y el Gantt sigue siendo el exigido. Las rutas explícitas no cambian.
set -u
BIN=$1
source "$(dirname "$0")/lib.sh"

OUTPUT=$(run_session "$BIN" <<'EOF2'
set_sched SRTF
set_speed 50
load workload_test.txt
start
@sleep 1
gantt
load no_existe.txt
exit
EOF2
)

echo "Escenario 2b: 'load workload_test.txt' como en el enunciado"
expect_line "$OUTPUT" "[INFO]: 'workload_test.txt' no está en el directorio actual; se usa 'workloads/workload_test.txt'."
expect_text "$OUTPUT" "[OK]: 3 proceso(s) cargado(s) desde 'workloads/workload_test.txt'"
expect_line "$OUTPUT" "[0-1s: Job_A] -> [1-3s: Job_B] -> [3-4s: Job_C] -> [4-7s: Job_A]"
expect_text "$OUTPUT" "[ERROR DE ARCHIVO]: No se encontró el archivo 'no_existe.txt'"
reject_text "$OUTPUT" "se usa 'workloads/no_existe.txt'"

finish "$OUTPUT"

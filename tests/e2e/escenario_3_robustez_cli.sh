#!/usr/bin/env bash
# Robustez de la CLI: una ráfaga de entradas inválidas MIENTRAS el motor
# ejecuta procesos. La shell debe informar cada error, seguir respondiendo y
# cerrar limpiamente con exit (código de salida 0).
set -u
BIN=$1
source "$(dirname "$0")/lib.sh"

# El guion se lee primero en una variable y se envía con <<<. Si el heredoc
# estuviera dentro de $(...), el bash 3.2 de macOS intentaría emparejar la
# comilla de 'load "comillas sin cerrar' y abortaría con un error de sintaxis.
IFS= read -r -d '' SCRIPT <<'EOF'
set_speed 50
create Largo 40 3
start
create
create SoloNombre
create X abc
create X 2.5
create X 0
create X 5 9
create X 5 2 7
pcb
pcb abc
pcb -1
pcb 999
set_sched
set_sched FIFO
set_sched RR 0
set_sched RR dos
set_sched SRTF 4
set_speed
set_speed rapido
set_speed 10
set_speed 99999999999999999999
ps extra
gantt ahora
stats todo
load
load no_existe.txt
load workloads/workload_errores.txt
load "comillas sin cerrar
block xyz
unblock 1
comando_inventado
PS
ps
exit
EOF

OUTPUT=$(run_session "$BIN" <<<"$SCRIPT")
STATUS=$?

echo "Escenario 3: robustez de la CLI con el motor en ejecución"
expect_text "$OUTPUT" "[ERROR DE SINTAXIS]: Parámetros insuficientes."
expect_text "$OUTPUT" "[ERROR DE TIPO]: El burst_time debe ser un número entero."
expect_text "$OUTPUT" "[ERROR DE PARAMETRO]: El burst_time debe ser un entero mayor que 0."
expect_text "$OUTPUT" "[ERROR DE PARAMETRO]: La prioridad debe estar entre 1 y 5."
expect_text "$OUTPUT" "[ERROR DE SINTAXIS]: Demasiados parámetros."
expect_text "$OUTPUT" "[ERROR DE PARAMETRO]: No existe un proceso con PID 999."
expect_text "$OUTPUT" "[ERROR DE PARAMETRO]: El quantum debe ser un entero mayor que 0."
expect_text "$OUTPUT" "[ERROR DE SINTAXIS]: SRTF no utiliza quantum."
expect_text "$OUTPUT" "[ERROR DE PARAMETRO]: ms_por_tick debe estar entre 50 y 5000 ms."
expect_text "$OUTPUT" "está fuera de rango."
expect_text "$OUTPUT" "[ERROR DE SINTAXIS]: El comando 'ps' no recibe parámetros."
expect_text "$OUTPUT" "[ERROR DE ARCHIVO]: No se encontró el archivo 'no_existe.txt'"
expect_text "$OUTPUT" "[ERROR DE FORMATO]: El archivo 'workloads/workload_errores.txt' tiene 6 error(es)"
expect_text "$OUTPUT" "[ERROR DE SINTAXIS]: Comillas sin cerrar."
expect_text "$OUTPUT" "[ERROR DE PARAMETRO]: El proceso PID 1 no está bloqueado"
expect_text "$OUTPUT" "[ERROR]: Comando no reconocido: 'comando_inventado'."
expect_text "$OUTPUT" "¿Quiso decir 'ps'?"
expect_text "$OUTPUT" "1       Largo"
expect_line "$OUTPUT" "[OK]: Hilos detenidos y memoria liberada (1 PCB). Hasta luego."
reject_text "$OUTPUT" "Segmentation fault"
reject_text "$OUTPUT" "terminate called"
[[ $STATUS -eq 0 ]] || { echo "  [X]  código de salida $STATUS"; FAILURES=$((FAILURES + 1)); }

finish "$OUTPUT"

#!/usr/bin/env bash
# Ciclo de vida completo (NEW -> READY -> RUNNING -> BLOCKED -> READY ->
# TERMINATED) y cierre limpio cuando la entrada termina sin `exit`.
set -u
BIN=$1
source "$(dirname "$0")/lib.sh"

OUTPUT=$(run_session "$BIN" <<'EOF'
set_sched RR 2
set_speed 400
create P1 4 3
create P2 2 3
start
@sleep 0.2
pause
block 1
ps
set_speed 50
start
@sleep 0.4
unblock 1
@sleep 0.8
pcb 1
gantt
EOF
)
STATUS=$?

echo "Escenario 4: bloqueo/desbloqueo y fin de la entrada"
expect_text "$OUTPUT" "[OK]: Proceso PID 1 bloqueado (BLOCKED)."
expect_text "$OUTPUT" "1       P1              BLOCKED       3s                3"
expect_text "$OUTPUT" "[OK]: Proceso PID 1 desbloqueado: vuelve a la cola de LISTOS (READY)."
expect_line "$OUTPUT" "Estado Actual      : TERMINATED"
expect_text "$OUTPUT" "[0-1s: P1] -> [1-3s: P2]"
expect_text "$OUTPUT" "[INFO]: Fin de la entrada. Cerrando la simulación..."
expect_line "$OUTPUT" "[OK]: Hilos detenidos y memoria liberada (2 PCB). Hasta luego."
[[ $STATUS -eq 0 ]] || { echo "  [X]  código de salida $STATUS"; FAILURES=$((FAILURES + 1)); }

finish "$OUTPUT"

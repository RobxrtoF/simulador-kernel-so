#!/usr/bin/env bash
# Entradas extremas contra el ejecutable real, como si alguien intentara romperlo:
#   1) Escenario 2 con la traza COPIADA DEL PDF del enunciado (trae U+200B, un
#      carácter invisible, al final de cada línea) y comandos con espacios duros.
#   2) Basura binaria, números gigantes, líneas de 200 000 caracteres.
#   3) Ctrl+C (SIGINT) con el motor en marcha: la shell avisa y sigue viva.
set -u
BIN=$1
source "$(dirname "$0")/lib.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# 1) Traza tal cual sale al copiar del PDF.
printf '# NOMBRE, BURST_TIME, PRIORIDAD, ARRIVAL_TIME\xe2\x80\x8b\nJob_A, 4, 2, 0\xe2\x80\x8b\nJob_B, 2, 1, 1\xe2\x80\x8b\nJob_C, 1, 3, 3\n' >"$TMP/workload_pdf.txt"
GUION=$(printf 'set_sched SRTF\nset_speed 50\nload %s\nstart\n@sleep 1\ngantt\ncreate\xc2\xa0Copiado\xc2\xa03\xe2\x80\x8b\nexit\n' "$TMP/workload_pdf.txt")
PDF=$(run_session "$BIN" <<<"$GUION")

# 2) Basura y valores extremos (generados, sin comillas sueltas en un heredoc).
python3 - "$TMP/basura.txt" <<'PY'
import random, sys
random.seed(11)
lines = ["create P1 99999999999999999999", "create P1 0x10", "pcb 99999999999999", "set_speed -1",
         "set_sched RR -5", "load /dev/zero", "load .", "x" * 200000, "create " + "A" * 100000 + " 5"]
lines += [bytes(random.randint(1, 255) for _ in range(random.randint(1, 60))).decode("latin-1") for _ in range(150)]
with open(sys.argv[1], "w", encoding="latin-1") as f:
    for line in lines:
        f.write(line.replace("\n", " ") + "\n")
    f.write("create Sobreviviente 2\nps\nexit\n")
PY
BASURA=$("$BIN" <"$TMP/basura.txt" 2>&1)
ESTADO_BASURA=$?

# 3) Ctrl+C con el motor en marcha (la entrada llega por una tubería con nombre).
mkfifo "$TMP/entrada"
"$BIN" <"$TMP/entrada" >"$TMP/sigint.txt" 2>&1 &
PID=$!
exec 3>"$TMP/entrada"
sleep 0.5
printf 'create Largo 40 2\nset_speed 50\nstart\n' >&3
sleep 0.5
kill -INT "$PID"
sleep 0.3
kill -INT "$PID"
sleep 0.3
printf 'ps\nexit\n' >&3
exec 3>&-
wait "$PID"
ESTADO_SIGINT=$?
SIGINT=$(cat "$TMP/sigint.txt")

echo "Escenario 7: entradas extremas"
expect_text "$PDF" "[OK]: 3 proceso(s) cargado(s)"
expect_line "$PDF" "[0-1s: Job_A] -> [1-3s: Job_B] -> [3-4s: Job_C] -> [4-7s: Job_A]"
expect_text "$PDF" "[OK]: Proceso 'Copiado' creado con PID 4"
expect_text "$BASURA" "[OK]: Proceso 'Sobreviviente' creado con PID 1"
expect_line "$BASURA" "[OK]: Hilos detenidos y memoria liberada (1 PCB). Hasta luego."
reject_text "$BASURA" "Segmentation fault"
[[ $ESTADO_BASURA -eq 0 ]] || { echo "  [X]  código de salida con basura: $ESTADO_BASURA"; FAILURES=$((FAILURES + 1)); }
expect_text "$SIGINT" "[INFO]: Ctrl+C no cierra la simulación. Escriba 'exit' para salir."
expect_text "$SIGINT" "1      Largo"
expect_line "$SIGINT" "[OK]: Hilos detenidos y memoria liberada (1 PCB). Hasta luego."
[[ $ESTADO_SIGINT -eq 0 ]] || { echo "  [X]  código de salida tras Ctrl+C: $ESTADO_SIGINT"; FAILURES=$((FAILURES + 1)); }

finish "$PDF

$SIGINT"

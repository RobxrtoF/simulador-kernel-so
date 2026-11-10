#!/usr/bin/env bash
# Ejecuta todos los escenarios e2e contra el ejecutable real.
# Uso: tests/e2e/run_all.sh [ruta_al_simulador]
set -u

BIN=${1:-./simulador}
DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$DIR/../.." && pwd)
OUT="$DIR/out"
mkdir -p "$OUT"

if [[ ! -x $BIN ]]; then
    echo "No se encontró el ejecutable '$BIN'. Compile primero con: make"
    exit 1
fi
BIN=$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")

failed=0
total=0
for scenario in "$DIR"/escenario_*.sh; do
    name=$(basename "$scenario" .sh)
    total=$((total + 1))
    if (cd "$ROOT" && bash "$scenario" "$BIN") >"$OUT/$name.log" 2>&1; then
        echo "[  OK  ] $name"
    else
        echo "[ FALLA] $name  (detalle en tests/e2e/out/$name.log)"
        failed=$((failed + 1))
    fi
done

echo
echo "$((total - failed))/$total escenarios superados"
[[ $failed -eq 0 ]]

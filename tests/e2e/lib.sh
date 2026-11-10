# Utilidades compartidas por los escenarios e2e (se incluye con `source`).

# Envía al simulador las líneas de un guion leído por stdin, con pausas reales.
# Las líneas "@sleep <segundos>" no se envían: solo esperan ese tiempo.
# Uso: SALIDA=$(run_session ./simulador <<'EOF' ... EOF)
run_session() {
    local bin=$1
    {
        # Espera a que el simulador arranque antes del primer comando, para que
        # las pausas del guion midan tiempo de simulación aunque el arranque sea
        # lento (por ejemplo, bajo ThreadSanitizer en la CI).
        sleep 0.5
        while IFS= read -r line; do
            if [[ $line == @sleep* ]]; then
                sleep "${line#@sleep }"
            else
                printf '%s\n' "$line"
            fi
        done
    } | "$bin" 2>&1
}

FAILURES=0

# Verifica que la salida contenga una línea EXACTAMENTE igual a la esperada.
expect_line() {
    if grep -Fxq -- "$2" <<<"$1"; then
        echo "  [ok] línea exacta: $2"
    else
        echo "  [X]  falta la línea exacta: $2"
        FAILURES=$((FAILURES + 1))
    fi
}

# Verifica que la salida contenga un texto.
expect_text() {
    if grep -Fq -- "$2" <<<"$1"; then
        echo "  [ok] contiene: $2"
    else
        echo "  [X]  no contiene: $2"
        FAILURES=$((FAILURES + 1))
    fi
}

# Verifica que la salida NO contenga un texto.
reject_text() {
    if grep -Fq -- "$2" <<<"$1"; then
        echo "  [X]  no debería contener: $2"
        FAILURES=$((FAILURES + 1))
    else
        echo "  [ok] no contiene: $2"
    fi
}

# Termina el escenario con éxito solo si no hubo fallos.
finish() {
    echo
    echo "----- transcripción -----"
    echo "$1"
    [[ $FAILURES -eq 0 ]]
}

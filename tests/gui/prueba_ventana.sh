#!/usr/bin/env bash
# Prueba de la interfaz gráfica contra el ejecutable real (compilado con la GUI).
# Abre la ventana con el comando `gui`, deja correr la simulación, guarda una
# captura y verifica que la shell sigue respondiendo y que `exit` cierra todo.
#
# Uso: tests/gui/prueba_ventana.sh [ruta_al_simulador] [captura.png]
# En un servidor sin pantalla (la CI) se ejecuta dentro de una pantalla virtual:
#   xvfb-run -a -s "-screen 0 1400x900x24" tests/gui/prueba_ventana.sh build-gui/simulador
set -u
BIN=${1:-./simulador}
CAPTURA=${2:-tests/gui/out/captura.png}
mkdir -p "$(dirname "$CAPTURA")"
rm -f "$CAPTURA"

SALIDA=$({
    sleep 0.5
    printf 'set_speed 100\nload workloads/carga_mixta.txt\nstart\ngui\n'
    sleep 5
    printf 'gui\nps\n'      # con la ventana abierta: no se abre otra y la shell responde
    sleep 1.5
    printf 'exit\n'
} | SIM_GUI_SCREENSHOT="$CAPTURA" SIM_GUI_SCREENSHOT_DELAY=3 "$BIN" 2>&1)
ESTADO=$?

FALLOS=0
verificar() {  # verificar <descripción> <condición...>
    local descripcion=$1
    shift
    if "$@"; then
        echo "  [ok] $descripcion"
    else
        echo "  [X]  $descripcion"
        FALLOS=$((FALLOS + 1))
    fi
}
contiene() { grep -Fq -- "$1" <<<"$SALIDA"; }
captura_valida() { [[ -s $CAPTURA ]] && [[ $(wc -c <"$CAPTURA") -gt 20000 ]]; }

echo "Interfaz gráfica: ventana, captura y cierre"
verificar "el comando gui abre la ventana" contiene "[OK]: Abriendo la interfaz gráfica."
verificar "un segundo gui no abre otra ventana" contiene "[INFO]: La interfaz gráfica ya está abierta."
verificar "la shell responde con la ventana abierta (ps)" contiene "Cola de LISTOS:"
verificar "la ventana dibujó y guardó la captura ($CAPTURA)" captura_valida
verificar "exit cierra la ventana y los hilos" contiene "[OK]: Hilos detenidos y memoria liberada (5 PCB). Hasta luego."
verificar "código de salida 0" test "$ESTADO" -eq 0

if [[ $FALLOS -ne 0 ]]; then
    echo
    echo "----- transcripción -----"
    echo "$SALIDA"
    exit 1
fi

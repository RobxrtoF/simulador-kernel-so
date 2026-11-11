#!/usr/bin/env bash
# Regenera los diagramas y el informe técnico en PDF (pandoc + xelatex) a partir
# de docs/diagramas/*.dot (Graphviz), docs/diagramas/*.svg y docs/informe_tecnico.md.
#
# Requisitos (Ubuntu/WSL):
#   sudo apt install graphviz librsvg2-bin pandoc texlive-xetex texlive-latex-extra \
#        texlive-lang-spanish fonts-crosextra-caladea fonts-dejavu
set -euo pipefail
cd "$(dirname "$0")"

for source in diagramas/*.dot; do
    dot -Tpng -Gdpi=170 "$source" -o "img/$(basename "${source%.dot}").png"
done
for source in diagramas/*.svg; do
    rsvg-convert -z 2 "$source" -o "img/$(basename "${source%.svg}").png"
done

# El anexo empieza en una página nueva (solo en el PDF; el Markdown queda limpio para GitHub).
temporal=$(mktemp --suffix=.md)
trap 'rm -f "$temporal"' EXIT
sed 's/^# Anexo A\./\\newpage\n\n# Anexo A./' informe_tecnico.md >"$temporal"

pandoc "$temporal" -o informe_tecnico.pdf \
    --pdf-engine=xelatex --resource-path=. -H estilo_informe.tex \
    -V geometry:margin=2.2cm -V fontsize=11pt \
    -V mainfont="Caladea" -V monofont="DejaVu Sans Mono" -V monofontoptions="Scale=0.82" \
    -V colorlinks=true -V linkcolor=NavyBlue -V urlcolor=NavyBlue

echo "Listo: docs/informe_tecnico.pdf y docs/img/*.png"

#!/usr/bin/env bash
# build.sh -- build docs/manual/octopus-manual.pdf from scratch, idempotently.
#
#   1. figures/svg/*.svg  -> figures/pdf/*.pdf   (headless Chrome + pdfcrop; skipped when
#                                                 up to date, or when Chrome is absent and
#                                                 the committed PDFs exist)
#   2. configuration/**  -> generated/config_keys.tex
#      Protocols_FSM/**  -> generated/fsm_*.tex
#      git HEAD          -> generated/version.tex
#   3. latexmk -pdf (pdflatex + biber), aux files under build/
#   4. copy build/octopus-manual.pdf next to the sources (that copy is committed)
#
# Usage: bash docs/manual/build.sh [--force-figures] [--clean]
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

PY=python3; command -v python3 >/dev/null 2>&1 || PY=python
FORCE=""; [[ "${1:-}" == "--force-figures" ]] && FORCE="--force"
if [[ "${1:-}" == "--clean" ]]; then rm -rf build; echo "build/ removed"; exit 0; fi

echo "== figures"
$PY tools/svg2pdf.py $FORCE
echo "== generated tables"
$PY tools/gen_config_tables.py
$PY tools/gen_fsm_tables.py
mkdir -p generated build
printf '%s' "$(git rev-parse --short HEAD 2>/dev/null || echo unversioned)" > generated/version.tex

echo "== latexmk"
latexmk -pdf -interaction=nonstopmode -halt-on-error -outdir=build octopus-manual.tex >build/latexmk.out 2>&1 \
  || { tail -40 build/latexmk.out; echo "build FAILED -- see build/octopus-manual.log"; exit 1; }
cp build/octopus-manual.pdf octopus-manual.pdf

echo "== checks"
grep -c "Warning.*undefined" build/octopus-manual.log | sed 's/^/undefined-reference warnings: /' || true
grep -iE "^\(!\)|LaTeX Error|Emergency stop" build/octopus-manual.log | head -5 || true
pages=$(grep -oE "Output written on .*\(([0-9]+) pages" build/octopus-manual.log | grep -oE "[0-9]+ pages" || true)
echo "octopus-manual.pdf: ${pages:-? pages}, $(du -h octopus-manual.pdf | cut -f1)"

#!/usr/bin/env bash
#
# on-create.sh -- install everything needed to build, analyze, visualize, and
# document Octopus in a GitHub Codespace. This runs once for a newly-created
# container (and during a prebuild); post-create work should not be put here.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

git config --global --add safe.directory /workspaces/OctopusSimulator
git update-index

echo "== system packages"
sudo apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
    build-essential \
    cmake \
    gdb \
    git \
    git-lfs \
    ninja-build \
    pkg-config \
    python3 \
    python3-dev \
    python3-pip \
    python3-venv \
    scons \
    xz-utils \
    chromium \
    latexmk \
    biber \
    texlive-extra-utils \
    texlive-bibtex-extra \
    texlive-fonts-recommended \
    texlive-latex-extra \
    texlive-latex-recommended \
    >/dev/null

echo "== python packages"
# Ubuntu 24.04 marks its system interpreter externally managed (PEP 668).
# Codespaces are disposable, and the repository's scripts intentionally use
# plain python3, so install the pinned project dependencies into that interpreter.
python3 -m pip install --quiet --break-system-packages -r requirements.txt \
    || python3 -m pip install --quiet -r requirements.txt

echo "== toolchain check"
for tool in g++ cmake make ninja python3 xz latexmk biber pdfcrop scons git-lfs; do
    command -v "$tool" >/dev/null || {
        echo "on-create: '$tool' is missing after installation" >&2
        exit 1
    }
done
if ! command -v chromium >/dev/null && ! command -v chromium-browser >/dev/null \
    && ! command -v google-chrome >/dev/null; then
    echo "on-create: no Chromium-compatible browser found; SVG figure rendering will be skipped"
fi
echo "toolchain: $(cmake --version | head -1), $(g++ --version | head -1)"

echo "== benchmark traces (EEMBC only)"
# SPLASH archives are large and are not needed by the tutorial exercises.
# Set BMS_SPARSE or run get_benchmarks.sh manually to fetch another suite.
BMS_SPARSE="${BMS_SPARSE:-eembc-traces}" ./get_benchmarks.sh

echo "on-create: done"

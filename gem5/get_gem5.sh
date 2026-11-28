#!/usr/bin/env bash
# get_gem5.sh -- gem5 for this repository, from scratch, outside the dev container
# (which already has it as `gem5`): the release the container's gem5 is built from,
# the patches in gem5/patches/, and a build with this repository as an EXTRAS module.
#
#   bash gem5/get_gem5.sh [dir]        default dir: ../gem5, beside this repository
#   export GEM5_ROOT=<dir>             the tutorial scripts then use $GEM5_ROOT/build/ARM/gem5.opt
#
# Needs git, scons and gem5's build dependencies (see gem5's documentation, "Building
# gem5"), and Octopus built first (build/libOctopus.so), which gem5 links. The build
# takes a while: about an hour on 8 cores. BUILD=0 stops after the checkout and the
# patches; JOBS=<n> sets the parallelism; GEM5_URL overrides where gem5 comes from.
#
# ATP=1 also builds Arm's ATP-Engine 3.2 into gem5 (needed by fs_arm.py --atp): it is
# cloned beside gem5 (ATP_DIR, default ../ATP-Engine), patched with
# gem5/patches/atp-engine/*.patch (gem5 v25 build fixes, and looping streams that
# keep running: auto_reset, deferred start on checkpoint restore, stream reset of
# send-status gated profiles) and added to EXTRAS. ATP needs protobuf
# (apt: libprotobuf-dev protobuf-compiler).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIR="${1:-$(dirname "$ROOT")/gem5}"
TAG=v25.1.0.1
GEM5_URL="${GEM5_URL:-https://github.com/gem5/gem5.git}"
JOBS="${JOBS:-$(nproc)}"
ATP="${ATP:-0}"
ATP_DIR="${ATP_DIR:-$(dirname "$DIR")/ATP-Engine}"
ATP_URL="${ATP_URL:-https://github.com/ARM-software/ATP-Engine.git}"
ATP_REV=e411a12   # ATP Engine 3.2 release

if [ ! -d "$DIR/.git" ]; then
    echo "== cloning gem5 $TAG into $DIR"
    git clone --depth 1 --branch "$TAG" "$GEM5_URL" "$DIR"
fi
cd "$DIR"

for p in "$ROOT"/gem5/patches/*.patch; do
    if git apply --reverse --check "$p" 2>/dev/null; then
        echo "== already applied: $(basename "$p")"
    else
        echo "== applying $(basename "$p")"
        git apply "$p"
    fi
done

EXTRAS="$ROOT"
if [ "$ATP" = 1 ]; then
    if [ ! -d "$ATP_DIR/.git" ]; then
        echo "== cloning ATP-Engine $ATP_REV into $ATP_DIR"
        git clone "$ATP_URL" "$ATP_DIR"
        git -C "$ATP_DIR" checkout -q "$ATP_REV"
    fi
    for p in "$ROOT"/gem5/patches/atp-engine/*.patch; do
        if git -C "$ATP_DIR" apply --reverse --check "$p" 2>/dev/null; then
            echo "== already applied to ATP-Engine: $(basename "$p")"
        else
            echo "== applying to ATP-Engine: $(basename "$p")"
            git -C "$ATP_DIR" apply "$p"
        fi
    done
    EXTRAS="$ROOT:$ATP_DIR"
fi

[ "${BUILD:-1}" = 0 ] && { echo "== checkout ready (BUILD=0): $DIR"; exit 0; }

[ -f "$ROOT/build/libOctopus.so" ] || {
    echo "build Octopus first: cmake -S $ROOT -B $ROOT/build -DCMAKE_BUILD_TYPE=Release && cmake --build $ROOT/build" >&2
    exit 1; }

echo "== building gem5 with EXTRAS=$EXTRAS (-j$JOBS)"
scons EXTRAS="$EXTRAS" build/ARM/gem5.opt -j"$JOBS"
echo "== done: export GEM5_ROOT=$DIR"

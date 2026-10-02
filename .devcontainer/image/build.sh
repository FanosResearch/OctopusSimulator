#!/usr/bin/env bash
# build.sh -- build the dev container image from .devcontainer/image/Dockerfile.
#
#   bash .devcontainer/image/build.sh [tag]          default tag: test
#   docker push ghcr.io/fanosresearch/esweek:<tag>
#
# The Dockerfile copies binary inputs from ctx/ (not in git). This script fills
# ctx/gem5 and ctx/gem5-resources/ from an existing image, by default the published
# one, so a rebuild keeps the gem5 the reference results were made with, and
# ctx/fs/ (the full-system files) from a resources directory:
#
#   SOURCE_IMAGE=<image>     take gem5, the kernel and the bootloader from this image
#   GEM5_BIN=<path>          or take gem5 from here instead (e.g. a gem5.opt built by
#                            gem5/get_gem5.sh with this repository at
#                            /workspaces/OctopusSimulator, so its RUNPATH matches)
#   FS_RESOURCES=<dir>       the directory holding arm-ubuntu-22.04-trim.img and the
#                            checkpoints ov2_start_trim_<clip>; ctx/fs/ is refreshed
#                            from it (the image compressed). Without it, an existing
#                            ctx/fs/ is used as it is.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TAG="${1:-test}"
IMAGE="ghcr.io/fanosresearch/esweek:$TAG"
SOURCE_IMAGE="${SOURCE_IMAGE:-ghcr.io/fanosresearch/esweek:latest}"
CTX="$HERE/ctx"

rm -rf "${CTX:?}/gem5" "${CTX:?}/gem5-resources"
mkdir -p "$CTX/gem5-resources" "$CTX/fs"
cid=$(docker create "$SOURCE_IMAGE")
trap 'docker rm -f "$cid" >/dev/null' EXIT
if [ -n "${GEM5_BIN:-}" ]; then
    cp "$GEM5_BIN" "$CTX/gem5"
else
    docker cp "$cid:/usr/local/bin/gem5" "$CTX/gem5"
fi
for f in arm64-linux-kernel-5.15.180 boot_foundation.arm64-20220707; do
    docker cp "$cid:/opt/gem5-resources/$f" "$CTX/gem5-resources/$f"
done

if [ -n "${FS_RESOURCES:-}" ]; then
    for c in 0.5s 1s 3s; do
        rm -rf "${CTX:?}/fs/ov2_start_trim_$c"
        cp -a "$FS_RESOURCES/ov2_start_trim_$c" "$CTX/fs/"
    done
    img="$FS_RESOURCES/arm-ubuntu-22.04-trim.img"
    zst="$CTX/fs/arm-ubuntu-22.04-trim.img.zst"
    [ "$zst" -nt "$img" ] || zstd -T0 -10 --long=27 -q -f "$img" -o "$zst"
fi
[ -f "$CTX/fs/arm-ubuntu-22.04-trim.img.zst" ] || { echo "no ctx/fs/: set FS_RESOURCES" >&2; exit 1; }

docker build -t "$IMAGE" "$HERE"
echo "built $IMAGE; push with: docker push $IMAGE"

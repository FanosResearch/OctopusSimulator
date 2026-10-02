#!/usr/bin/env bash
# get_disk_image.sh -- unpack the full-system disk image the dev container ships
# compressed, once, before a full-system run (gem5/configs/fs_arm.py):
#
#   bash gem5/get_disk_image.sh
#
# The image is 9 GB unpacked, so it goes to /tmp, the codespace's large scratch
# disk, and /opt/gem5-resources/arm-ubuntu-22.04-trim.img links to it, where
# fs_arm.py looks. /tmp does not survive a stop: run it again after a restart (it
# returns at once when the image is still there). zstd verifies the image's
# checksum as it unpacks; --sparse keeps its empty blocks off the disk.
#
#   RESOURCES=<dir>   the resources directory (default /opt/gem5-resources)
#   UNPACK_DIR=<dir>  where the image is unpacked (default /tmp/gem5-resources)
set -euo pipefail

RESOURCES="${RESOURCES:-/opt/gem5-resources}"
UNPACK_DIR="${UNPACK_DIR:-/tmp/gem5-resources}"
IMG=arm-ubuntu-22.04-trim.img
LINK="$RESOURCES/$IMG"
ZST="$RESOURCES/$IMG.zst"

if [ -f "$LINK" ]; then             # follows the link: unpacked and still there
    echo "disk image ready: $LINK"
    exit 0
fi
[ -f "$ZST" ] || { echo "no $ZST: not the dev container? set RESOURCES" >&2; exit 1; }

mkdir -p "$UNPACK_DIR"
echo "== unpacking $IMG into $UNPACK_DIR (about a minute)"
zstd -d --sparse --long=27 -q -f "$ZST" -o "$UNPACK_DIR/$IMG.part"
mv "$UNPACK_DIR/$IMG.part" "$UNPACK_DIR/$IMG"
ln -sfn "$UNPACK_DIR/$IMG" "$LINK"
echo "disk image ready: $LINK -> $UNPACK_DIR/$IMG"

#!/usr/bin/env bash
# Build the simulator and the unit tests with AddressSanitizer + UBSan under WSL (S0-ASAN).
# Produces build_asan/Octopus_Simulator and build_asan/test/network/unit/Octopus_tests.
#   bash test/network/asan.sh            # from Git Bash on Windows: runs the build inside WSL
#   bash test/network/asan.sh --native   # already on Linux
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; ROOT="$(cd "$HERE/../.." && pwd)"
if [ "${1:-}" != "--native" ] && command -v wsl.exe > /dev/null 2>&1; then
  LINUX_ROOT="/mnt/$(echo "$ROOT" | sed 's#^/\([a-zA-Z]\)/#\L\1/#; s#^\([A-Za-z]\):#\L\1#')"
  exec wsl.exe -e bash -lc "cd '$LINUX_ROOT' && bash test/network/asan.sh --native"
fi
cd "$ROOT"
mkdir -p build_asan
cmake -S . -B build_asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" > build_asan/configure.log 2>&1
cmake --build build_asan -j"$(nproc)" >> build_asan/configure.log 2>&1
ls -la build_asan/Octopus_Simulator build_asan/test/network/unit/Octopus_tests

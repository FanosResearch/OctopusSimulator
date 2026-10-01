#!/bin/bash
# Standalone harnesses against build/libOctopus.so (no gem5). Usage: ./build.sh && ./l1 /tmp/octlog [overrides...]
# Each takes a workload_path directory (must contain newLogger/) and optional Octopus overrides, e.g.
#   ./reorder /tmp/octlog "cache_controller[*].m_data_handler.m_data_access_latency(i)=10"
#   ./stress  /tmp/octlog --seed=2 --cores=4 --lines=3 --inflight=4 --total=20000 "llc_controller.line_interlock(i)=0"
set -e
T=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p /tmp/octlog/newLogger
for src in "$T"/gem5/harness/*.cc; do
  g++ -O1 -std=c++17 -I"$T"/header $(find "$T"/header -mindepth 1 -type d | sed 's/^/-I/') "$src" -L"$T"/build -lOctopus -Wl,-rpath,"$T"/build -o "${src%.cc}"
done
echo "built: $(ls "$T"/gem5/harness | grep -v '\.' | tr '\n' ' ')"

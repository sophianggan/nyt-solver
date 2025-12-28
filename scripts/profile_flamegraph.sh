#!/usr/bin/env sh
set -eu

if [ "$#" -lt 1 ]; then
  echo "Usage: $0 <command> [args...]" >&2
  exit 2
fi

if ! command -v perf >/dev/null 2>&1; then
  echo "perf not found. Run this on Linux with perf installed." >&2
  exit 1
fi

if ! command -v flamegraph.pl >/dev/null 2>&1; then
  echo "flamegraph.pl not found. Install Brendan Gregg's FlameGraph and add it to PATH." >&2
  exit 1
fi

mkdir -p reports
perf record -F 99 -g -- "$@"
perf script | flamegraph.pl > reports/flamegraph.svg
echo "FlameGraph saved: reports/flamegraph.svg"

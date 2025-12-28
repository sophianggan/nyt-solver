#!/usr/bin/env sh
set -eu

count=30
while [ "$#" -gt 0 ]; do
  case "$1" in
    --count)
      count="$2"
      shift 2
      ;;
    *)
      echo "Unknown arg: $1" >&2
      exit 2
      ;;
  esac
done

solutions_file="data/wordle_solutions.txt"

echo "Baseline build (no SIMD/pool):"
cmake -S . -B build-baseline -DCMAKE_BUILD_TYPE=Release \
  -DALETHEIA_USE_HWY=0 -DALETHEIA_USE_WORD_POOL=0
cmake --build build-baseline --config Release
python3 scripts/wordle_benchmark.py --count "$count" --binary ./build-baseline/aletheia \
  --solutions-file "$solutions_file" \
  --json reports/wordle_benchmark_baseline.json \
  --plot reports/wordle_guess_hist_baseline.png

echo ""
echo "Optimized build (SIMD + pool):"
cmake -S . -B build-optimized -DCMAKE_BUILD_TYPE=Release \
  -DALETHEIA_USE_HWY=1 -DALETHEIA_USE_WORD_POOL=1
cmake --build build-optimized --config Release
python3 scripts/wordle_benchmark.py --count "$count" --binary ./build-optimized/aletheia \
  --solutions-file "$solutions_file" \
  --json reports/wordle_benchmark_optimized.json \
  --plot reports/wordle_guess_hist_optimized.png

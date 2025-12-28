# Benchmarks

## Wordle Benchmark Summary

Source: `reports/wordle_benchmark_optimized.json`

```
Run: python3 scripts/wordle_benchmark.py --count 30
```

Metrics (n=30):

| Metric | Value |
| --- | --- |
| Win rate | 96.7% |
| Average guesses | 3.79 |
| Max guesses | 5 |
| Average guess latency (ms) | 906.190 |
| P99 guess latency (ms) | 1674.790 |
| P99 total latency (ms) | 5024.371 |
| Average total bits | 13.859 |
| Hardest words | cupel, muvva, tamer, mappy |

## Full Verification Run

Source: `reports/wordle_benchmark_full.json` (latest 1,653 official dates)

| Metric | Value |
| --- | --- |
| Win rate | 99.1% |
| Average guesses | 3.91 |
| Max guesses | 6 |
| Average guess latency (ms) | 735.651 |
| P99 guess latency (ms) | 1359.975 |
| P99 total latency (ms) | 2893.597 |
| Average total bits | 13.859 |
| Hardest words (sample) | tocks, gyves, tabus, solus, dawds, yukes, eched, popes, tacos, weeke, oafos, murky |

## A/B Comparison (Baseline vs Optimized)

Run the baseline build without SIMD/pooling and compare to optimized:

```
./scripts/wordle_benchmark_ab.sh --count 30
```

| Metric | Baseline (no SIMD/pool) | Optimized (SIMD + pool) |
| --- | --- | --- |
| P50 guess latency (ms) | 734.450 | 827.519 |
| P90 guess latency (ms) | 1006.840 | 1115.534 |
| P99 guess latency (ms) | 1205.451 | 1674.790 |
| P99 total latency (ms) | 3616.354 | 5024.371 |
| Win rate | 96.7% | 96.7% |
| Average guesses | 3.79 | 3.79 |

Reports:

- `reports/wordle_benchmark_baseline.json`
- `reports/wordle_benchmark_optimized.json`

## Memory Pool Impact

The Wordle loop reuses candidate buffers and a fixed-block pool, removing
per-turn allocations. You can verify the allocation cost by running:

```
./build/aletheia --wordle-dict wordle.txt --interactive --profile
```

The "alloc" time should stay near zero compared to compute time, indicating
the hot loop is zero-allocation.

## Memory Leak Check

Use the script below to run the binary under `valgrind` (Linux) or `leaks`
(macOS):

```
./scripts/memcheck.sh ./build/aletheia --wordle-dict wordle.txt --interactive
```

If you need stricter leak settings, add flags inside `scripts/memcheck.sh`.

## Sanitizers

CI runs AddressSanitizer and ThreadSanitizer on Linux. You can also run
locally with:

```
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer"
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

## FlameGraph Profiling

On Linux, capture a flamegraph of the solver hot paths:

```
./scripts/profile_flamegraph.sh ./build/aletheia --wordle-dict wordle.txt --interactive
```

This writes `reports/flamegraph.svg`.

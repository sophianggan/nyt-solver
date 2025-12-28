# Aletheia

[![CI](https://github.com/sophianggan/nyt-solver/actions/workflows/ci.yml/badge.svg)](https://github.com/sophianggan/nyt-solver/actions/workflows/ci.yml)

High-performance NYT Wordle + Connections solver with SIMD acceleration,
fixed-block memory pooling, and PCA-based Connections clustering.

## Quickstart

Build:

```
cmake -S . -B build
cmake --build build
```

Wordle (interactive + auto feedback):

```
./build/aletheia --wordle-dict wordle.txt --interactive --wordle-target crane
```

Connections (demo puzzle):

```
./build/aletheia --connections-interactive --connections-demo --allow-fallback
```

## WASM Demo

Build the WebAssembly bundle (requires Emscripten):

```
./scripts/build_wasm.sh
```

Then open `web/index.html` in a local server (or deploy `web/` to GitHub Pages).

## Docs

- `THEORY.md` for Shannon Entropy + Cosine Similarity + PCA notes.
- `BENCHMARKS.md` for benchmark methodology and results.

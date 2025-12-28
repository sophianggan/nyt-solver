#!/usr/bin/env sh
set -eu

OUT_DIR="web"
EMCC="${EMCC:-emcc}"

if ! command -v "$EMCC" >/dev/null 2>&1; then
  echo "emcc not found. Install Emscripten and try again."
  exit 1
fi

if [ ! -d "build/_deps/eigen-src" ]; then
  echo "Eigen headers missing. Run: cmake -S . -B build"
  exit 1
fi

mkdir -p "$OUT_DIR"

"$EMCC" \
  -O3 \
  -std=c++20 \
  -s WASM=1 \
  -s MODULARIZE=1 \
  -s EXPORT_ES6=1 \
  -s ENVIRONMENT=web \
  -s ALLOW_MEMORY_GROWTH=1 \
  --bind \
  -I. \
  -Ibuild/_deps/eigen-src \
  web/wasm_api.cpp \
  Wordle.cpp \
  Connections.cpp \
  -o "$OUT_DIR/aletheia_wasm.js"

echo "WASM build complete: $OUT_DIR/aletheia_wasm.js"

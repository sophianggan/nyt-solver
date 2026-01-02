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

COMMON_FLAGS="-O3 -flto -std=c++20 -msimd128"
COMMON_FLAGS="$COMMON_FLAGS -s WASM=1"
COMMON_FLAGS="$COMMON_FLAGS -s MODULARIZE=1 -s EXPORT_ES6=1"

"$EMCC" \
  $COMMON_FLAGS \
  -s ENVIRONMENT=web \
  -s ALLOW_MEMORY_GROWTH=1 \
  --bind \
  -I. \
  -Ibuild/_deps/eigen-src \
  web/wasm_api.cpp \
  Wordle.cpp \
  Connections.cpp \
  -o "$OUT_DIR/aletheia_wasm.js"

"$EMCC" \
  $COMMON_FLAGS \
  -s ENVIRONMENT=web,worker \
  -s USE_PTHREADS=1 \
  -s PTHREAD_POOL_SIZE=4 \
  -s INITIAL_MEMORY=268435456 \
  -s ALLOW_MEMORY_GROWTH=0 \
  -pthread \
  --bind \
  -I. \
  -Ibuild/_deps/eigen-src \
  web/wasm_api.cpp \
  Wordle.cpp \
  Connections.cpp \
  -o "$OUT_DIR/aletheia_wasm_mt.js"

echo "WASM build complete: $OUT_DIR/aletheia_wasm.js + $OUT_DIR/aletheia_wasm_mt.js"

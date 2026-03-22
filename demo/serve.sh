#!/bin/bash
# Build WASM and serve the demo locally
# Usage: ./demo/serve.sh [port]

set -e

PORT=${1:-8080}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "=== Building WASM ==="
source ~/emsdk/emsdk_env.sh 2>/dev/null
cd "$PROJECT_DIR"
emcmake cmake -B build-wasm -DBUILD_WASM=ON -DCMAKE_BUILD_TYPE=Release 2>/dev/null
cmake --build build-wasm 2>&1 | tail -3

echo "=== Copying files to demo/ ==="
cp build-wasm/hevc-decode.js "$SCRIPT_DIR/"
cp build-wasm/hevc-decode.wasm "$SCRIPT_DIR/"
cp src/wasm/worker.js "$SCRIPT_DIR/"

echo "=== Serving on http://localhost:$PORT ==="
echo "Open http://localhost:$PORT in your browser"
cd "$SCRIPT_DIR"
python3 -m http.server "$PORT"

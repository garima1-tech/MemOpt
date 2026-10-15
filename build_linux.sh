#!/usr/bin/env bash
# Builds MemOpt on Linux / WSL2 and (with --test) runs the test suite.
#   ./scripts/build_linux.sh          build only
#   ./scripts/build_linux.sh --test   build + ctest
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
command -v cmake >/dev/null || { echo "ERROR: sudo apt install cmake g++ libpq-dev"; exit 1; }
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
echo "Build OK: build/memopt"
if [[ "${1:-}" == "--test" ]]; then
  (cd build && ctest --output-on-failure)
fi

#!/usr/bin/env bash
# Build and run the host-side parser unit tests.
# Usage: bash tests/run.sh   (from the repo root, or anywhere)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BIN="$HERE/test_parser"

g++ -std=c++17 -Wall -Wextra \
    -I"$HERE" \
    -I"$ROOT/src" \
    "$HERE/test_parser.cpp" \
    "$ROOT/src/ld2410.cpp" \
    -o "$BIN"

"$BIN"

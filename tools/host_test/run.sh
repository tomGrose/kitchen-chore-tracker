#!/usr/bin/env bash
# Build and run the engine tests on a PC. app_controller.cpp is excluded on
# purpose: it is the only file under src/app that needs Arduino and LVGL.
# Point ARDUINOJSON_DIR at a directory containing ArduinoJson.h (the
# single-header release).
set -euo pipefail
cd "$(dirname "$0")/../.."
: "${ARDUINOJSON_DIR:=/tmp/aj}"
SOURCES=$(ls src/app/*.cpp | grep -v app_controller)
OUT="${TMPDIR:-/tmp}/host_test"
g++ -std=c++17 -Wall -Wextra -I src -I "$ARDUINOJSON_DIR" \
    tools/host_test/test_engine.cpp $SOURCES -o "$OUT"
"$OUT"

#!/usr/bin/env bash
# Runs the two checks that can be done without Wokwi or an ESP32 toolchain:
#   1. logic tests  - compiles and runs the real decision logic (logic.h)
#   2. stub build   - compiles sketch.ino as C++ against Arduino API stubs
# Run from the project root:  bash tests/run_host_tests.sh
set -e
mkdir -p tests/logs

echo "== 1. logic tests =="
g++ -std=c++17 -Wall -Wextra -Wno-unused-function -o /tmp/host_logic_test tests/host_logic_test.cpp
/tmp/host_logic_test | tee tests/logs/host_logic_test.log

echo
echo "== 2. sketch stub build + smoke run =="
g++ -std=c++17 -Wall -Wno-unused-function -I tests/host_stubs -x c++ \
    sketch.ino tests/host_stubs/host_main.cpp -o /tmp/sketch_stub
/tmp/sketch_stub | tee tests/logs/sketch_stub_run.log

echo
echo "Host checks complete. Wokwi simulation still has to be run separately."

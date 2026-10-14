#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
c++ -std=c++17 -O2 -Wall -Wextra -Itests/include shape_lfo.cpp tests/host_test.cpp -o tests/host_test_bin
tests/host_test_bin

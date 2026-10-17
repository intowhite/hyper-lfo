#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
CXX="${CXX:-c++}"
"$CXX" -std=c++17 -O2 -Wall -Wextra -Itests/include shape_lfo.cpp tests/host_test.cpp -o tests/host_test_bin
./tests/host_test_bin
"$CXX" -std=c++17 -O2 -Wall -Wextra -Itests/include tests/math_test.cpp -o tests/math_test_bin
./tests/math_test_bin
"$CXX" -std=c++17 -O2 -Wall -Wextra -Itests/include tests/clock_test.cpp -o tests/clock_test_bin
./tests/clock_test_bin
"$CXX" -std=c++17 -O2 -Wall -Wextra -Itests/include tests/playmode_test.cpp -o tests/playmode_test_bin
./tests/playmode_test_bin
"$CXX" -std=c++17 -O2 -Wall -Wextra -Itests/include tests/gate_test.cpp -o tests/gate_test_bin
./tests/gate_test_bin
"$CXX" -std=c++17 -O2 -Wall -Wextra -Itests/include tests/bank_test.cpp -o tests/bank_test_bin
./tests/bank_test_bin
"$CXX" -std=c++17 -O2 -Wall -Wextra -Itests/include tests/segment_test.cpp -o tests/segment_test_bin
./tests/segment_test_bin
"$CXX" -std=c++17 -O2 -Wall -Wextra -Itests/include tests/quantize_test.cpp -o tests/quantize_test_bin
./tests/quantize_test_bin
"$CXX" -std=c++17 -O2 -Wall -Wextra -Itests/include tests/features_test.cpp -o tests/features_test_bin
./tests/features_test_bin
# The same segment/zoom tests with an 8-segment window (logic must not depend on the size).
"$CXX" -std=c++17 -O2 -Wall -Wextra -DSLFO_ZOOM_WINDOW=8 -Itests/include tests/segment_test.cpp -o tests/segment_test_zoom8_bin
./tests/segment_test_zoom8_bin

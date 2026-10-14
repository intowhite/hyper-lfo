#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
SDK="${1:-../distingNT_API}"
if [[ $# -eq 0 && -f "./distingNT_API/include/distingnt/api.h" ]]; then SDK="./distingNT_API"; fi
if [[ ! -f "$SDK/include/distingnt/api.h" ]]; then
  echo "Official distingNT_API missing; pass SDK folder as first argument" >&2; exit 1
fi
command -v arm-none-eabi-g++ >/dev/null || { echo "GNU ARM toolchain missing" >&2; exit 1; }
rm -f shape_lfo.o
arm-none-eabi-g++ -std=c++17 -O2 -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard \
  -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-unwind-tables -fno-asynchronous-unwind-tables \
  -ffunction-sections -fdata-sections -I"$SDK/include" -c shape_lfo.cpp -o shape_lfo.o
if command -v arm-none-eabi-nm >/dev/null; then
  echo 'Host imports:'
  arm-none-eabi-nm -u shape_lfo.o
  if arm-none-eabi-nm -u shape_lfo.o | grep -Eq '__aeabi_|\bmemcpy\b|\bmemset\b|\bmalloc\b|\bfree\b'; then
    echo 'Unexpected runtime helper; refusing to certify build' >&2; exit 1
  fi
fi
printf 'Built %s bytes: shape_lfo.o\n' "$(wc -c <shape_lfo.o)"

#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
flags=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude)
"${CC:-clang}" -std=c11 "${flags[@]}" -c src/gameboy_apu.c -o build/gameboy_apu.o
"${CXX:-clang++}" -std=c++17 "${flags[@]}" test/gameboy_test.cpp src/gameboy.cpp build/gameboy_apu.o -o build/gameboy-test
build/gameboy-test "$@"

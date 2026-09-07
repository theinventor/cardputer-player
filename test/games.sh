#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
read -r -a json_flags <<< "$(pkg-config --cflags --libs libcjson)"
flags=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude)
for core in blocks 2048; do
  "${CC:-clang}" -std=c11 "${flags[@]}" -c "src/game_$core.c" -o "build/game_$core.o"
done
"${CXX:-clang++}" -std=c++17 "${flags[@]}" test/games_test.cpp src/games.cpp src/game_store.cpp src/game_render.cpp \
  build/game_blocks.o build/game_2048.o "${json_flags[@]}" -o build/games-test
build/games-test
"${CXX:-clang++}" -std=c++17 "${flags[@]}" test/game_driver.cpp src/games.cpp src/game_store.cpp src/game_render.cpp \
  build/game_blocks.o build/game_2048.o "${json_flags[@]}" -o build/game-driver

#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/fixtures
for kind in cbr vbr mono; do
  options=(-b:a 192k -ar 44100 -ac 2)
  if [[ "$kind" == vbr ]]; then options=(-q:a 2 -ar 48000 -ac 2); fi
  if [[ "$kind" == mono ]]; then options=(-b:a 64k -ar 22050 -ac 1); fi
  ffmpeg -hide_banner -loglevel error -y -f lavfi \
    -i 'aevalsrc=0.2*sin(2*PI*(440+80*sin(t))*t)|0.15*sin(2*PI*660*t):d=12:s=48000' \
    "${options[@]}" -metadata title='Cardtunes test' -metadata artist='Troy Anderson' \
    "build/fixtures/$kind.mp3"
done
"${CXX:-clang++}" -std=c++17 -O1 -g -fsanitize=address,undefined \
  -fno-omit-frame-pointer -DMINIMP3_ONLY_MP3 -DMINIMP3_NO_SIMD -Iinclude \
  test/media_test.cpp src/media.cpp -o build/media-test
build/media-test build/fixtures/cbr.mp3 build/fixtures/vbr.mp3 build/fixtures/mono.mp3
read -r -a json_flags <<< "$(pkg-config --cflags --libs libcjson)"
"${CXX:-clang++}" -std=c++17 -O1 -g -fsanitize=address,undefined \
  -fno-omit-frame-pointer -DMINIMP3_ONLY_MP3 -DMINIMP3_NO_SIMD -Iinclude \
  test/playlists_test.cpp src/playlists.cpp src/media.cpp "${json_flags[@]}" -o build/playlists-test
build/playlists-test

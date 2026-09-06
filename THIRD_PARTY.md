# Third-Party Components

- `include/third_party/blocks/`: [olzhasar/sdl-tetris](https://github.com/olzhasar/sdl-tetris),
  commit `428d99d52a1de2448c7442d2655fae56aa6e9e96`, MIT (included LICENSE).
  Only the C rules engine is used, not SDL. Cardtunes moves bag/line bookkeeping
  into each game state, exposes the next shape for rendering, and bounds row access.
- `include/third_party/2048/`: [mevdschee/2048.c](https://github.com/mevdschee/2048.c),
  commit `afc8898691f54d43309497f4c32682fe90bb5f57`, MIT (included LICENSE).
  `CARDTUNES_CORE_ONLY` excludes terminal UI and main; the original move/merge
  and game-over routines are retained. Cardtunes handles random tile spawning.
- `include/third_party/cute_c2.h`: [RandyGaul/cute_headers](https://github.com/RandyGaul/cute_headers),
  commit `389aa9554f478c49d5db2715548f52b8d5286db7`, cute_c2 1.10.
  Used under its zlib license, retained at the end of the file. Breakout uses
  its circle/rectangle collision manifold; fixed-step motion and game rules are local.

- `include/third_party/minimp3.h`: lieff/minimp3, commit
  `ea99364f61c14656440e8d77e9c233ccf3124633`, CC0. The original notice is retained.
- `web/icons/*.svg`: Lucide Static 0.468.0. See `web/icons/LICENSE` for the ISC and
  retained Feather icon license notices.
- PlatformIO resolves M5Cardputer 1.1.1, M5Unified 0.2.21, M5GFX 0.2.28, IRremote
  4.7.1, and the Arduino ESP32 platform pinned in `platformio.ini`. These carry
  their own upstream licenses; their source is not vendored here. Generated
  firmware distributions must comply with those licenses.
- Test audio is synthesized locally by FFmpeg. Personal demo music and artwork
  are excluded from source control and are not licensed for redistribution here.

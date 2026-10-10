# Game Boy Preview Review

Status: software preview; no physical-device acceptance or flashing yet.

## Scope

- Pinned MIT Peanut-GB core and MiniGB APU; no ROM/BIOS/save included.
- Original Game Boy mode, custom color palettes, sound, held keyboard controls,
  pause/exit, cartridge saves, authenticated ROM upload, CLI commands.
- Existing MP3/library/playlists and three built-in games remain available.
- MP3 decoder buffers are now allocated lazily and released by the audio worker
  after its speaker buffers have drained, before the emulator allocates memory.
- Emulator and keyboard/network handling share the main loop. CPU work is bounded
  per tick. Cartridge ROM is paged from SD instead of allocating the whole ROM.

## Review Checks

- Header bounds and actual ROM size checked before Peanut-GB's lookup tables.
- Unsupported cartridge features rejected explicitly instead of mis-emulated.
- ROM upload path restricted to one `.gb` file beneath `/Games/`; existing files
  are protected. No new unauthenticated API is introduced.
- SRAM writes use temporary files, streamed verification, and backup rotation.
  Failed finalization leaves the prior generation recoverable. Corrupt/truncated
  existing saves do not silently reset progress.
- Game entry is rejected during scans/loading/another game. Music mutation and
  firmware/music/ROM uploads are rejected while emulation is active.
- Music handoff retains order/queue/playlist and position. Stopped playback is not
  automatically started. Allocation/start failure attempts to restore playback.
- Game Boy pause controls do not fall through to MP3 controls.

## Verification

- ASan/UBSan synthetic-ROM tests: header/size/path rejection, banked ROM reads,
  bounded execution, button mapping, pause, SRAM persistence, interrupted writes,
  corrupted temporary writes, failed rename, backup recovery, and SD read failure.
- Local user-provided Red ROM: SHA-1
  `ea9bcae617fdf159b045185467ae58b2e4a48b9a`, 1,800 frames with nonblank video
  and nonzero synthesized audio. No copyrighted bytes are test fixtures.
- ESP32-S3/8 MiB firmware build succeeds and fits the existing 3 MiB OTA slot.
- Existing native media, playlists, order, storage/scan, and games tests pass.
- Go CLI tests and vet pass, including authenticated ROM upload framing and
  Game Boy control routing. Spotify playlist conversion tests pass.
- Existing browser playlist regression passes against its mock API; this does
  not exercise the physical device or the new firmware endpoint.
- The actual RGB332 line renderer is bounds-tested at 240x135; a local rendered
  screenshot reaches Professor Oak's introduction without cropping the image.

## Remaining Hardware Gates

- Confirm no heap exhaustion with Wi-Fi plus the largest library/playlists.
- Measure real frame rate and sound quality; desktop execution is not proof of
  full-speed emulation on ESP32-S3 or reliable timing over a slow SD card.
- Verify keyboard holds/releases, pause, lock/sleep, and returning to MP3 repeatedly.
- Verify Pokemon's in-game save across power cycles and physical SD removal.
- Check authenticated ROM uploads, screenshots, OTA rejection, and device recovery.

Do not call this hardware-ready or merge it as a tested release until these gates
are exercised on the actual Cardputer. USB/power was unavailable during this build.

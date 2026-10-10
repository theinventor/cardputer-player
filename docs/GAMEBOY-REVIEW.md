# Game Boy Preview Review

Status: flashed and smoke-tested on a Cardputer ADV on 2026-10-10. Still a preview
pending long gameplay, final subjective audio acceptance, connected Wi-Fi stress,
and an actual Pokemon in-game Save/Continue test across a power cycle.

## Scope

- Pinned MIT Peanut-GB core and MiniGB APU; no ROM/BIOS/save included.
- Original Game Boy mode, custom color palettes, sound, held keyboard controls,
  pause/exit, cartridge saves, authenticated ROM upload, CLI commands.
- Existing MP3/library/playlists and three built-in games remain available.
- MP3 decoder buffers are now allocated lazily and released by the audio worker
  after its speaker buffers have drained, before the emulator allocates memory.
- Emulator and keyboard/network handling share the main loop. CPU work is bounded
  per tick. ROMs up to 1 MiB use a SHA-256-verified memory-mapped flash cache;
  larger ROMs fall back to SD paging. No whole-ROM RAM allocation is required.
- Audio consumption paces the emulator, with two-frame sound blocks and alternate
  LCD rendering. Only the game viewport is transferred during ordinary redraws.
- Cartridge RAM is split into 8 KiB allocations. MP3 PCM buffers are independently
  allocated so repeated game/music transitions tolerate heap fragmentation.
- A physical-USB installer and diagnostics cover networks unavailable to the
  Cardputer. ROM uploads are bounded, checked by full SD readback, and no-overwrite.

## Review Checks

- Header bounds and actual ROM size checked before Peanut-GB's lookup tables.
- Unsupported cartridge features rejected explicitly instead of mis-emulated.
- ROM upload path restricted to one `.gb` file beneath `/Games/`; existing files
  are protected. Network endpoints retain authentication; USB remains a trusted
  physical console, like the existing Wi-Fi configuration commands.
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
- ESP32-S3/8 MiB firmware build succeeds and fits the new 2 MiB OTA slot.
  USB installs the changed table; the original NVS and LittleFS addresses remain.
- Existing native media, playlists, order, storage/scan, and games tests pass.
- Go CLI tests and vet pass, including authenticated ROM upload framing and
  Game Boy control routing. Spotify playlist conversion tests pass.
- Existing browser playlist regression passes against its mock API; this does
  not exercise the physical device or the new firmware endpoint.
- The actual RGB332 line renderer is bounds-tested at 240x135; a local rendered
  screenshot reaches Professor Oak's introduction without cropping the image.
- Additional native tests cover all four cartridge-RAM banks, mapped ROM reads
  without SD access, USB offsets/hex/bounds, checksum corruption, abort, write
  failure, finalization failure, and refusing to overwrite an existing ROM.

## Physical Smoke Test

- Flashed via USB; existing NVS, LittleFS and all 805 indexed music tracks retained.
- Installed the user's 1,048,576-byte Red ROM to `/Games/pokemon-red.gb` over USB
  in 43.9 seconds; verified full SD readback before publishing the destination.
- Game starts, renders a nonblank correctly framed image, accepts physical keys,
  plays sound, and pauses on inactivity. User confirmed playability but reported
  choppy audio on the initial SD-paged build, motivating the flash-cache fix.
- Flash-cached build: roughly 59 frames/second across the intro; individual sampled
  windows around 56-60. About 50 KiB heap free, no emulation errors, zero SD ROM
  cache misses. Initial cache build took 6.3 seconds; repeat launch took 2 seconds.
  The `audio_underruns` diagnostic counts empty software queues, not actual I2S
  underruns; audible quality still requires user confirmation on the final build.
- Three consecutive playing-music -> game -> save/exit cycles restored the same
  song and position without allocation failures. Paused and stopped music states
  were also retained. This caught and fixed an MP3 allocation failure after an
  earlier successful transition.
- The saved open hotel SSID was not visible in the Cardputer's 2.4 GHz scan.
  Network connectivity was not claimed, and the laptop's Wi-Fi was never changed.
- Independent `codex review` completed. It found failed USB upload setup could
  leave music paused; setup now restores previously playing audio on failure.
  The review sandbox could not run compiler tests; tests were run separately in
  the working environment.

## Remaining Hardware Gates

- Confirm no heap exhaustion with Wi-Fi plus the largest library/playlists.
- Confirm final subjective sound quality and longer gameplay, including battles.
- Exercise lock/unlock and the slower SD fallback with larger ROMs.
- Verify Pokemon's in-game save across power cycles and physical SD removal.
- Check authenticated ROM uploads, screenshots, OTA rejection, and device recovery.

Keep the PR as a preview until the remaining gates are exercised. Do not describe
host save-recovery tests as proof of Pokemon's in-game progress surviving power loss.

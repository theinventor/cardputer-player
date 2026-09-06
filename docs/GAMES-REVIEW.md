# Cardtunes 0.3.0 Review and Acceptance

Date: 2026-09-06. Base: `feature/saved-playlists`. PR: #3.

## Review

The requested `/review` workflow covered the first-party diff, pinned game
cores, enum consumers, persistence, audio handoff, device UI, CLI, and tests.
Independent testing, maintainability, security, performance, API-contract,
design, red-team, and adversarial reviewers were used. These were same-host
Codex agents, not cross-model reviews. Nested Codex CLI reviews were skipped.
The independent adversarial pass inspected test/fixture changes in summary
mode only; the structured testing review read their contents.

Findings addressed before OTA:

- Restoring SD games after a late insertion, retrying failed restoration, and
  refusing to overwrite existing games until restoration succeeds.
- String/escape-aware JSON nesting limits before parsing, canonical Blocks
  shape validation, and 2048 terminal-state validation with backup fallback.
- Reclaiming unattached cJSON nodes on allocation failure, with 400 injected
  allocation-failure positions checked for leaks.
- Redrawing static/paused games only when their visual state changes.
- Visible game-launch errors, preserving screen-lock pause, and clearing old
  text-entry state and pending G0 clicks when changing modes.
- Browser-preview keyboard focus, idempotent blur pause, complete touch-pad
  markup, and sequence acknowledgements in asynchronous visual tests.
- CLI tests through the real command dispatcher; normal backup-rotation fault
  tests; an automated hardware test of App/audio/playlist integration.

No known unresolved release-blocking findings. Residual coverage limits:
forced audio-worker/queue failures are not injected on hardware, physical
keyboard feel requires human evaluation, and abrupt power loss/FAT damage is
simulated in native storage tests rather than deliberately induced on the SD.
The firmware has no PSRAM; minimum free heap can become very low under weak
Wi-Fi/network load (also observed on 0.1.2). Save allocation failures are
handled, but long-duration stress testing remains useful.

## Verification

- `bash test/run.sh`: synthesized MP3 decoding/seek tests, media validation,
  playback order, saved playlists, and games, with Clang ASAN/UBSAN.
- Games: 60,000 seeded input steps; collision/lives/waves, four-line clear,
  2048 merging/win, pause/restart/exit, render bounds, save validation and recovery.
- Go race tests and vet passed. No GitHub CI checks are configured in this repo.
- Playwright: actual native game cores, motion/input/pause/exit, nonblank canvas,
  keyboard focus and three viewports; existing playlist web regression tests pass.
- Cardputer-Adv OTA reports **0.3.0** and retains all **12 tracks**.
- `node test/games-device.mjs`: three real game screens, input, pause, lock,
  saves, rejected music commands during games, same-track/position resume,
  selected-playlist preservation and queue preservation passed.
- Hardware test restores the prior music selection, playback state, queue,
  and volume; it creates and deletes only its own temporary QA playlist.

Reboot persistence passed separately with the explicit
`node test/games-reboot.mjs build/cardtunes-0.3.0.bin` test. It reflashes the
tested image, verifies a reboot, and compares saved game scores/bests and
the music library/settings. Restored scores were Blocks 1, Breakout 10, and
2048 0; post-reboot free heap was 51,256 bytes. Never run hardware scripts against an unattended
device without the owner's approval.

Screenshots and binaries are local build artifacts, not committed personal data.
Use the application `firmware.bin` for OTA, never `firmware.factory.bin`.

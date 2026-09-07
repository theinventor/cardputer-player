# 0.3.2 Scan Acceptance

Verified on a Cardputer-Adv with 803 indexed tracks, including the flash demo,
after the owner's 800 MP3 files were copied directly to microSD.

- Cancelled a live scan through keyboard input over the authenticated API.
  All 803 previous library entries remained available.
- A complete incremental scan indexed all 802 SD files with zero skips in
  147,055 ms. Status requests kept working (maximum observed response: 2,342 ms).
  Uptime stayed monotonic with no reset.
- Repeated scan requests, game launch, and playlist edits were rejected while
  scanning. A native test also covers upload/index mutation exclusion.
- Played an SD track and observed the playback position advancing; restored
  the previous track/position and volume afterward.
- Captured the actual device display and checked battery outline/terminal
  pixels. Visually checked scan counter, filename, cancel command, and header.
- Native ASan/UBSan tests cover 10,000 songs, hidden macOS sidecars, cancellation,
  write failure, rename rollback, and recovery of the previous index after reboot.
- Browser tests cover progress, cancellation, completion refresh, disabled
  controls, and responsive layouts. Go tests and vet pass.

Investigation: the original device reset reason was panic (4). Instrumented
blocking scans subsequently completed without reproducing that panic, but each
held the UI and HTTP server for several minutes. The original panic's precise
cause is not established. The new implementation removes the recursive,
whole-scan call chain and demonstrates responsive, reset-free rescanning.
Temporary crash instrumentation was removed from the release source.

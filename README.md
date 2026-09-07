# Cardtunes

A pocket MP3 player for the M5Stack Cardputer-Adv (Stamp-S3A).

An on-device MP3 library and player with Wi-Fi controls, browser uploads, and
firmware updates. No cloud account or Home Assistant is involved.

## Controls

| Key | Action |
| --- | --- |
| Space | Play / pause |
| Tab | Playing / Library / Playlists / Games / Settings |
| N / B | Next / previous track |
| [ / ] | Volume down / up |
| , / / | Seek backward / forward 10 seconds |
| S | Shuffle on / off |
| R | Cycle repeat off / all / one |
| V | Toggle spectrum display |
| F | Search the library; Enter finishes text entry |
| ; / . | Move up / down in Library, Playlists, or Settings |
| Enter | Play the selected track/list or edit the selected setting |
| Q | Add the selected library track to the queue |
| Backtick | Return to Playing |
| Fn + backtick | Escape from text entry |
| G0 button | One click: play/pause; two: next; three: previous |
| Hold G0 | Lock / unlock the keyboard and screen |

The first key after screen sleep wakes the screen **and** performs its action.
Explicitly locked keys stay disabled until G0 is held again.

## Games (0.3.0)

Three games are built into the same firmware: **Falling Blocks**, **Breakout**,
and **2048**. Tab to Games, select with `;` / `.`, then press Enter. Each game
resumes its last session. No reboot, download, emulator, or ROM is required.

| Game | Controls |
| --- | --- |
| Falling Blocks | A/D or comma/slash: move; W or semicolon: rotate; S or period: soft drop; Space/Enter: hard drop |
| Breakout | Hold A/D or comma/slash: paddle; Space/Enter: launch |
| 2048 | WASD or semicolon/comma/period/slash: slide in that direction |
| All games | P, Tab, or G0 click: pause menu; up/down then Enter: Resume, New game, or Exit to music |
| All games | Backtick or Fn+backtick: save and exit to Playing; hold G0: lock and pause |

Fn-arrow keys work too. 2048 ends when you reach the 2048 tile or have no moves.
Breakout starts with three lives and refills cleared brick boards. Games are
silent: music pauses on entry and resumes on exit **only if it was playing**.
Playlist selection, position, and queue are retained. Screen sleep pauses games;
after waking, use the pause menu to resume. Loading tracks must finish before entry.

Progress and best scores live in `/.cardtunes/games-v1.json`, with verified
temporary writes and a recoverable `.bak` generation. Saves happen on pause,
exit, game over, and at 30-second checkpoints. Sudden power loss can lose the
last checkpoint; a backup recovery can lose one additional generation. This
is not protection from SD hardware failure or arbitrary FAT corruption.
Without a mounted SD card games work, but progress is RAM-only. Save errors
appear in the Games menu and status API. Exit remains available after a save failure.
After a late SD insertion and rescan, existing card sessions take precedence
over RAM sessions of the same game; other RAM games and higher best scores remain.

Wi-Fi status, screenshots, and game commands remain available. Music changes,
playlist edits, rescans, and music/firmware uploads are rejected while a game
is open. Exit first. Games use the existing screen buffer and do not need PSRAM.
On-device Wi-Fi acceptance covers rendering, game input, SD saves, and returning
to music with the same playlist and queue. Physical keyboard feel and extended
stress testing remain human acceptance checks; see [review notes](docs/GAMES-REVIEW.md).

### Game API

The same authenticated `POST /api/control` endpoint accepts:

- `action=game&value=blocks` (also `breakout` or `2048`)
- `action=game-key&value=left` (also right/up/down/primary/pause/exit)
- `action=game-exit`

`GET /api/status` adds `game`: `id` (`none` when closed), `name`, `paused`,
`over`, `score`, `best`, and `save_error`. Invalid commands return 400; uploads
during a game return 409. Remote keys are single presses, not held controls.
The existing `/api/input` route also accepts device keys. No credentials change.

## Saved Playlists

Firmware **0.3.1** supports **16 named playlists with 1,000 unique songs each**.
The whole-library limit remains **10,000 songs**. Existing playlists remain readable.
A song can belong to several playlists without duplicating its MP3 file.

- **On the Cardputer:** press Tab to reach Playlists, select a list with `;` / `.`,
  and press Enter. Choose All music to return to whole-library playback.
- **In the browser:** use New playlist (+) beside the playlist selector. Choose
  All music, then use each song's list/music icon to add it to a saved list.
  Select a list to view it; the play button beside the selector starts that list.
  Browsing a list does not interrupt playback. Use the pencil to rename, arrows
  to reorder, or X to remove entries/delete the list. Deleting a list never
  deletes MP3s; deleting the active list stops playback and selects All music.
- **Shuffle and repeat** stay inside the active playlist. Repeat off ends at the
  last song; Repeat all loops this list; Repeat one repeats the current song.
  Explicitly playing a song from All music/the device's Library exits playlist mode.
- **Persistence:** lists live at `/.cardtunes/playlist-N.jsonl` on the microSD card.
  Entries use absolute song paths, not changing library IDs. Names, membership,
  and order survive reboot, rescan, and normal firmware updates. The active list
  is remembered in NVS along with the existing paused track/position resume.
  Writes use a verified temporary file and a recoverable backup. Keep external
  backups too; this cannot protect against card failure or arbitrary FAT corruption.
  Version 2 stores a JSON header line (`version`, `name`, `count`), followed by one
  JSON-encoded absolute path per line. The player keeps offsets in RAM and reads
  paths in 512-byte chunks, rather than allocating the entire playlist. Legacy
  version-1 `.json` files migrate when edited; retain a backup before downgrading
  firmware because older releases cannot read the new format.
- **Missing songs:** unavailable paths remain visible in the browser and are
  skipped during playback. An empty/all-missing list cannot start; the current
  playback selection is left unchanged. Removing the currently playing entry
  stops playback. A missing/corrupt active playlist does not fall back to unrelated music.

Names are 1-63 UTF-8 bytes, with no surrounding whitespace/control characters;
duplicate names (ASCII case-insensitive) and duplicate entries are rejected.
The serialized file is limited to 400,000 bytes. Standard M3U import/export is not included.

### Temporary Queue

The separate Up next queue still holds up to 64 entries and clears on reboot,
rescan, playlist switches, or membership/order edits to the active playlist.
In Library, `Q` appends a song; in the browser's All music view, `+` appends it.
Only songs inside the active playlist can be queued while in playlist mode.
When the queue empties, playback returns to the active list, not the whole library.
Shuffle does not change manually queued order. Repeat one takes precedence over
automatic queue advancement until changed or manually skipped.

## Music and Wi-Fi

Copy your MP3 files onto a FAT32 microSD card, optionally inside album folders,
then choose Rescan music in Settings. Folder `cover.jpg` artwork is supported.
Firmware **0.3.2** scans one directory entry at a time: the keyboard, display,
and Wi-Fi API keep running, with a scanned-track count and elapsed time on the
device and web page. Large cards can take minutes. Enter, Esc, or the web Cancel
scan button cancels without replacing the previous library. Playback is stopped
during scanning; successful scans restore the saved song and position paused.
Uploads, playlist edits, and games are blocked until the scan finishes or is
cancelled. Failed reads/writes keep the previous index and report an error.
The top-right battery icon shows remaining charge beside Wi-Fi, with a low-battery
color at 20% or less; the percentage is the board library's voltage-based estimate.
Alternatively, connect through the web interface and use Add files / Add folder.
Uploads pause playback, retain folder names, and do not overwrite existing files.
The keyboard also pauses while a file is transferring. Leave the player powered
on until the upload completes. Incomplete uploads are not added to the library.
If a card is inserted while the player is running, choose Rescan music to mount
it. Firmware 0.1.1 restores the last selected track and saved position on startup,
paused; press Space to continue. It does not force playback of the flash demo.

Set Network and Password in Settings, enable Wi-Fi, and choose Connect. The device
remembers these settings across power cycles. Settings also displays its IP and
its generated device access key. API requests use `Authorization: Bearer KEY`.
The browser remembers its key until Forget device access is clicked.

The device serves HTTP on port 80 and advertises `cardtunes.local`. Keep it on a
trusted LAN: HTTP is not encrypted, and the access key also permits firmware
updates. Do not expose port 80 to the public internet. The optional CLI proxy
below binds only to a Tailscale IPv4 address; anyone allowed to reach that proxy
by your tailnet ACLs can control the player. The proxy keeps the real key out of
the browser. The Cardputer itself does not run Tailscale.

## Build and Flash

Requirements: PlatformIO, Node.js, and Go 1.23 or later. Dependency versions are
pinned in `platformio.ini`. The target is the **8 MB Cardputer-Adv**, not the
original Cardputer. Flashing replaces the existing firmware.

```sh
bash tools/build.sh
bash tools/build.sh -t upload --upload-port /dev/cu.usbmodem11101
cd cli
go build -o ../build/cardtunes ./cmd/cardtunes
```

Adjust the serial port for your machine. The partition table reserves two 3 MB
application slots and a LittleFS area; ordinary firmware updates preserve NVS
settings and the filesystem. Do not use `firmware.factory.bin` for OTA.

## CLI

```sh
build/cardtunes pair DEVICE_ADDRESS DEVICE_ACCESS_KEY
build/cardtunes status
build/cardtunes list Redbone
build/cardtunes play 0
build/cardtunes pause
build/cardtunes volume 20
build/cardtunes playlist
build/cardtunes playlist create "Road trip"
build/cardtunes playlist add 1 2
build/cardtunes playlist add 1 7
build/cardtunes playlist show 1
build/cardtunes playlist move 1 1 0
build/cardtunes playlist play 1
build/cardtunes playlist play all
build/cardtunes game breakout
build/cardtunes game-key primary
build/cardtunes game-key pause
build/cardtunes game-exit
build/cardtunes upload /path/to/Albums
build/cardtunes screen build/display.bmp
build/cardtunes firmware .pio/build/cardputer-adv/firmware.bin
build/cardtunes serve YOUR_TAILSCALE_IP:8174
```

Pairing stores credentials in a mode-0600 file in the OS user config directory.
`cardtunes help` lists commands; `CARDTUNES_HOST` and `CARDTUNES_TOKEN` override
saved settings. Run the proxy while this computer can reach the Cardputer.
Use the ID returned by `playlist create`; it is not always 1. Track IDs come from
`list`. Remove/move commands take zero-based **playlist positions**, not track IDs.
`play ID` respects the current scope; `play-library ID` explicitly leaves it.

### Playlist API

All routes require the existing `Authorization: Bearer DEVICE_ACCESS_KEY` header.

| Request | Result / form fields |
| --- | --- |
| `GET /api/playlists` | List summaries and active playlist ID (0 means All music) |
| `GET /api/playlists?id=1&offset=0&limit=32` | Ordered entries, zero-based positions, missing flags, available count, next offset (-1 when finished) |
| `POST /api/playlists` | `action=create&value=NAME` returns the new `id` |
| `POST /api/playlists` | `action=rename&id=ID&value=NAME` |
| `POST /api/playlists` | `action=add&id=ID&value=TRACK_ID` |
| `POST /api/playlists` | `action=remove&id=ID&value=POSITION` |
| `POST /api/playlists` | `action=move&id=ID&value=FROM&to=TO` |
| `POST /api/playlists` | `action=delete&id=ID` |
| `POST /api/control` | `action=playlist&value=ID` (or `all`) starts the scope |

Status includes `active_playlist_id`, `playlist_name`, and `playlist_tracks`.
Add `track=TRACK_ID` to the playlist control to start a particular member atomically,
without briefly playing the first song. Out-of-scope tracks are rejected before switching.
Playlist pages are capped at 16 entries to bound RAM use with long paths. Requests
for the former 32-entry limit are accepted but return at most 16; follow
`next_offset`. There is no new cloud service or credential. Status also reports
`playlist_track_limit` and `library_track_limit`.

`POST /api/control` with `action=rescan` returns **202 when the scan starts**, not
when it finishes. Poll `GET /api/status`: `scan` contains `active`, `scanned`,
`skipped`, `elapsed_ms`, `path`, `succeeded`, and `error`. Check `succeeded` after
`active` becomes false. `action=cancel-scan` cancels the current scan. The CLI
exposes these as `cardtunes rescan`, `cardtunes status`, and `cardtunes cancel-scan`.
The published track count and track IDs stay unchanged until a successful scan.

USB serial accepts newline-delimited JSON commands, including `{"cmd":"status"}`
and `{"cmd":"info"}`. **Info includes the access key**: never publish its output.
There is no continuous serial status logging, so a closed serial monitor cannot
block the UI. Status includes keyboard press counts and maximum main-loop gap.
Firmware 0.1.2 also reports the ESP32 `reset_reason` code to distinguish software
updates (3), power-on resets (1), panic resets (4), task watchdog resets (6), and
brownouts (9). Multipart writes yield to the scheduler between chunks.

## Tests and Current Limits

```sh
bash test/run.sh
cd cli
go test ./...
go vet ./...
```

Native tests decode generated CBR/VBR/mono MP3s, compare seek PCM samples, exercise
malformed metadata, validate paths and playback ordering, and check key rollover.
They run with address/undefined-behavior sanitizers. FFmpeg, Clang, pkg-config, and
the host cJSON development library are required (macOS: `brew install cjson pkg-config`).
The firmware uses cJSON already supplied by ESP-IDF, with no new runtime dependency.
Playlist tests cover persistence, write failures, backup recovery, validation,
capacity limits, queue isolation, shuffle, repeat, and preserving saved order.
Go tests cover authenticated playlist commands, pagination, errors, and validation.

The 1,000-track tests cover long paths, edits at position 999, rejection of a
1,001st entry, migration, reboot, and recovery without whole-file reads. For an
explicit on-device capacity test, build `bash tools/build.sh -e cardputer-playlist-test`.
This non-production image installs a disposable 1,000-entry playlist in unused
slot 16 (999 missing long paths plus the flash demo); it never overwrites an
occupied slot. Run `node test/large-playlist-device.mjs`, then install the normal
`cardputer-adv` firmware and delete the fixture. Normal builds do not install it.

### Bulk Transfer Timing

A September 2026 test on the actual Cardputer uploaded a 4,717,484-byte MP3 in
31.52 seconds, approximately **150 kB/s** including indexing. At that rate a
1.98 GB collection takes about **3 hours 40 minutes**; allow **4-5 hours** for
Wi-Fi variability and per-file overhead. This is a measured reference, not a
guarantee. Use a microSD reader for a multi-gigabyte batch. Let downloads finish
before copying, and safely eject the card before reinserting it into the player.

Game tests use the actual firmware C/C++ cores, cover collisions, lives, line
clears, merges, pause/exit/restart, save corruption/recovery, and fuzz 60,000
input steps with render bounds and sanitizer checks. To run the native-engine
browser preview and responsive pixel/input checks (no Cardputer contacted):

```sh
bash test/games.sh
PLAYWRIGHT_MODULE=/path/to/playwright/index.mjs node test/games_web_test.mjs
PREVIEW_HOST=YOUR_TAILSCALE_IP node test/games-preview.mjs
```

The preview uses port 8176 and is a shared, development-only simulation with
no device access or music. It exercises the real game logic and drawing layout,
but browser fonts are an approximation of the device's bitmap font.

With Playwright installed, `node test/web_test.mjs` tests the actual web UI against
a **mock API**, including CRUD, ordering, missing songs, write errors, pagination,
and desktop/390px/320px screenshots. `PLAYWRIGHT_MODULE` can point to an existing
Playwright installation's `index.mjs`. Screenshots stay under ignored `build/`.
`PREVIEW_HOST=YOUR_TAILSCALE_IP node test/web-preview.mjs` starts a mock browser
preview on port 8175; its songs/state are fixtures, reset on restart, and do not
control any hardware or play audio. It uses no real device credentials.

**0.2.0 has been built and tested locally, not flashed or validated on hardware.**
Cardputer key layout, SD persistence across an actual reboot, and heap headroom
under maximum-size playlists must be checked when the device is reachable again.
`node test/device.mjs` uses the paired device for a live control and screen-sleep
regression test; it briefly changes playback and volume, then leaves music playing.
`node test/sd-device.mjs '/Music/Artist/Track.mp3'` verifies playback, seeking,
pause, and rescan with an existing card track at least 65 seconds long. An optional
second argument, `.pio/build/cardputer-adv/firmware.bin`, also installs that firmware
over Wi-Fi and verifies the same SD track and saved position survive the reboot.
This test leaves the selected track playing and does not erase or format the card.

On the development Cardputer, USB flashing, internal-flash MP3 playback, Wi-Fi
status/control, display capture, and OTA updates have been verified. Physical
keys were confirmed responsive by the operator. With a card inserted, Wi-Fi MP3
upload into album folders, playback from microSD, pause/seek, rescan, and saved
position recovery after a firmware update have also passed on firmware 0.1.1.
On 0.1.2, a ten-file MP3 collection totaling about 63 MB was verified in the SD
library. The timed uploads of 4-8 MB files took 25-53 seconds each on the test
network, averaging about 162 KiB/s. Transfer speed depends on signal and card.

An optional personal demo lives at LittleFS `/demo.mp3` and `/demo.jpg` and appears
as `/@demo.mp3` in the library. **Music and artwork are not included in this repo**.
The flash demo remains available alongside SD music. MP3 only; no Bluetooth audio,
streaming services, gapless playback, or signed firmware updates yet.

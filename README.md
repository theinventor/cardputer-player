# Cardtunes

A pocket MP3 player for the M5Stack Cardputer-Adv (Stamp-S3A).

An on-device MP3 library and player with Wi-Fi controls, browser uploads, and
firmware updates. No cloud account or Home Assistant is involved.

## Controls

| Key | Action |
| --- | --- |
| Space | Play / pause |
| Tab | Playing / Library / Settings |
| N / B | Next / previous track |
| [ / ] | Volume down / up |
| , / / | Seek backward / forward 10 seconds |
| S | Shuffle on / off |
| R | Cycle repeat off / all / one |
| V | Toggle spectrum display |
| F | Search the library; Enter finishes text entry |
| ; / . | Move up / down in Library or Settings |
| Enter | Play the selected track or edit the selected setting |
| Q | Add the selected library track to the queue |
| Backtick | Return to Playing |
| Fn + backtick | Escape from text entry |
| G0 button | One click: play/pause; two: next; three: previous |
| Hold G0 | Lock / unlock the keyboard and screen |

The first key after screen sleep wakes the screen **and** performs its action.
Explicitly locked keys stay disabled until G0 is held again.

## Music and Wi-Fi

Copy your MP3 files onto a FAT32 microSD card, optionally inside album folders,
then choose Rescan music in Settings. Folder `cover.jpg` artwork is supported.
Alternatively, connect through the web interface and use Add files / Add folder.
Uploads pause playback, retain folder names, and do not overwrite existing files.

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
build/cardtunes upload /path/to/Albums
build/cardtunes screen build/display.bmp
build/cardtunes firmware .pio/build/cardputer-adv/firmware.bin
build/cardtunes serve YOUR_TAILSCALE_IP:8174
```

Pairing stores credentials in a mode-0600 file in the OS user config directory.
`cardtunes help` lists commands; `CARDTUNES_HOST` and `CARDTUNES_TOKEN` override
saved settings. Run the proxy while this computer can reach the Cardputer.

USB serial accepts newline-delimited JSON commands, including `{"cmd":"status"}`
and `{"cmd":"info"}`. **Info includes the access key**: never publish its output.
There is no continuous serial status logging, so a closed serial monitor cannot
block the UI. Status includes keyboard press counts and maximum main-loop gap.

## Tests and Current Limits

```sh
bash test/run.sh
cd cli
go test ./...
go vet ./...
```

Native tests decode generated CBR/VBR/mono MP3s, compare seek PCM samples, exercise
malformed metadata, validate paths and playback ordering, and check key rollover.
They run with address/undefined-behavior sanitizers. FFmpeg and Clang are required.
`node test/device.mjs` uses the paired device for a live control and screen-sleep
regression test; it briefly changes playback and volume, then leaves music playing.

On the development Cardputer, USB flashing, internal-flash MP3 playback, Wi-Fi
status/control, display capture, and two OTA updates have been verified. Physical
keys were confirmed responsive by the operator. MicroSD upload/playback still
needs verification with a card inserted; the no-card upload rejection is explicit.

An optional personal demo lives at LittleFS `/demo.mp3` and `/demo.jpg` and appears
as `/@demo.mp3` in the library. **Music and artwork are not included in this repo**.
The current demo build starts track zero on boot. MP3 only; no Bluetooth audio,
streaming services, gapless playback, or signed firmware updates yet.

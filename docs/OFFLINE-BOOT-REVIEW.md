# Offline boot recovery (0.4.1-dev)

## Diagnosis

An ADV running 0.4.0-dev repeatedly reached `Cardtunes ready` with 805 tracks,
then aborted in `xQueueSemaphoreTake` before entering the main loop. The captured
ELF SHA256 matched the local 0.4.0 build. Its decoded stack was:

```
setup -> ct::Web::begin -> WebServer::begin -> NetworkServer::begin
      -> lwip_socket -> netconn_new_with_proto_and_callback
      -> tcpip_send_msg_wait_sem -> sys_mutex_lock -> xQueueSemaphoreTake
```

`App::begin` initializes Wi-Fi only when enabled and configured. `Web::begin`
previously opened a socket unconditionally, even without an initialized lwIP
stack. This crashes a cold boot with Wi-Fi disabled or no saved SSID.
The later core-dump errors are consequences of the panic, not evidence of an
SD-card or Pokemon-save failure.

## Fix

Register routes during setup without opening sockets. Service the HTTP listener
only once Wi-Fi is enabled and connected. Close it on disconnect and recreate
it on reconnect; retain route registrations. Leave OTA reboot scheduling outside
the connection guard. Do not force Wi-Fi on or change saved credentials. Check
the underlying listener after `begin()` (the Arduino method returns void); if
socket creation or binding fails, close partial resources and retry after one
second rather than treating it as started.

## Verification

`bash test/run.sh` exercises the actual listener lifecycle with a fake server
that asserts if sockets are opened before the network is ready. Cases cover cold
offline boot, delayed connection, one-time startup, repeated disconnect/offline
ticks, reconnect cycles, an already-connected first tick, failed socket startup,
bounded retries, timer rollover, and reconnect during retry. The full native
media, playlists, storage, games, and Game Boy test suite also passes.

The normal ADV firmware build passes. Hardware regression checklist:

- Flash only the active application slot, not a factory erase or SD image.
- Boot with the existing Wi-Fi-off setting; verify status responds and uptime
  increases rather than resetting every few seconds.
- Confirm the same 805-track SD library remains available.
- Verify music playback and Game Boy startup without changing the game save.
- Reset again with Wi-Fi disabled and verify another stable boot.
- Verify enabled-but-unconnected boot remains usable.
- On a reachable Wi-Fi network, verify HTTP starts after connection and returns
  after disconnect/reconnect. Do not change the laptop's Wi-Fi to perform this.

Hardware results must be recorded separately; native mocks do not prove radio
connectivity or game-save survival.

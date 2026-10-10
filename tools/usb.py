#!/usr/bin/env python3
"""Physical USB diagnostics and ROM installation; requires pyserial."""
import argparse
import json
import pathlib
import sys
import time

import serial


def request(port, command, timeout=15):
    port.write((json.dumps(command, separators=(",", ":")) + "\n").encode())
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            value = json.loads(port.readline())
        except (ValueError, UnicodeDecodeError):
            continue
        if not isinstance(value, dict):
            continue
        if value.get("ok") is False:
            raise RuntimeError(value.get("error", "Device command failed"))
        return value
    raise TimeoutError("No USB response")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/cu.usbmodem1101")
    parser.add_argument("command", choices=["status", "wifi-scan", "control", "rom", "screen"])
    parser.add_argument("args", nargs="*")
    args = parser.parse_args()
    with serial.Serial(args.port, 115200, timeout=.25, write_timeout=5) as port:
        port.write(b"\n")
        time.sleep(.1)
        port.reset_input_buffer()
        if args.command == "rom":
            if len(args.args) not in (1, 2):
                parser.error("rom FILE.gb [/Games/NAME.gb]")
            source = pathlib.Path(args.args[0])
            if not 32768 <= source.stat().st_size <= 2 * 1024 * 1024:
                parser.error("Expected a 32 KiB to 2 MiB ROM")
            data = source.read_bytes()
            hash_value = 2166136261
            for byte in data:
                hash_value = ((hash_value ^ byte) * 16777619) & 0xffffffff
            destination = args.args[1] if len(args.args) == 2 else "/Games/pokemon-red.gb"
            request(port, {"cmd": "rom-begin", "path": destination, "size": str(len(data)), "hash": str(hash_value)})
            started = time.monotonic()
            try:
                # Keep each JSON line within the default 256-byte USB RX queue.
                for offset in range(0, len(data), 64):
                    chunk = data[offset:offset + 64]
                    result = request(port, {"cmd": "rom-data", "offset": str(offset), "data": chunk.hex()})
                    if result.get("received") != offset + len(chunk):
                        raise RuntimeError("Unexpected upload acknowledgement")
                    if offset % 65536 == 0:
                        print(f"{offset}/{len(data)} bytes", flush=True)
                request(port, {"cmd": "rom-finish"}, timeout=30)
            except Exception:
                try:
                    request(port, {"cmd": "rom-abort"})
                except Exception:
                    pass
                raise
            print(f"Installed and readback-verified {destination}: {len(data)} bytes in {time.monotonic() - started:.1f}s")
        elif args.command == "screen":
            if len(args.args) != 1:
                parser.error("screen FILE.ppm")
            pixels = bytearray()
            for row in range(135):
                result = request(port, {"cmd": "screen-row", "row": str(row)})
                data = bytes.fromhex(result["pixels"])
                if result.get("row") != row or len(data) != 240:
                    raise RuntimeError("Invalid screenshot row")
                for value in data:
                    pixels.extend(((value >> 5) * 255 // 7, ((value >> 2) & 7) * 255 // 7, (value & 3) * 255 // 3))
            pathlib.Path(args.args[0]).write_bytes(b"P6\n240 135\n255\n" + pixels)
            print(f"Saved {args.args[0]}")
        else:
            if args.command == "control":
                if len(args.args) not in (1, 2):
                    parser.error("control COMMAND [VALUE]")
                command = {"cmd": args.args[0], "value": args.args[1] if len(args.args) == 2 else ""}
            else:
                command = {"cmd": args.command}
            result = request(port, command)
            result.pop("token", None)
            print(json.dumps(result, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, TimeoutError) as error:
        sys.exit(str(error))

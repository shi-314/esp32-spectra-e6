#!/usr/bin/env python3
"""Copies a local directory to the microSD card of a device running the sd-upload firmware.

Usage: upload.py <serial port> <local dir> <card dir>
Example: upload.py /dev/cu.usbserial-10 tools/moon/images /moon
"""
import sys
import time
import zlib
from pathlib import Path

import serial

BAUD = 460800  # Must match the firmware
CHUNK_SIZE = 4096  # Must match the firmware


def read_line(port, timeout=10):
    # The port keeps one short read timeout: changing it reconfigures the port, which glitches the
    # CH340 bridge on this board
    deadline = time.time() + timeout
    buffer = b""
    while time.time() < deadline:
        buffer += port.readline()
        if buffer.endswith(b"\n"):
            return buffer.decode(errors="replace").strip()
    raise TimeoutError("no response from device")


def expect(port, prefix, timeout=10):
    while True:
        line = read_line(port, timeout)
        if line.startswith(prefix):
            return line
        if line.startswith("ERR"):
            raise RuntimeError(line)
        # Anything else is boot noise from the ROM or the SD library


def wait_until_ready(port):
    deadline = time.time() + 15
    while time.time() < deadline:
        port.write(b"PING\n")
        try:
            line = read_line(port, timeout=1)
        except TimeoutError:
            continue
        # The boot log arrives at a different baud rate, so READY can follow a line of noise
        if line.endswith("READY"):
            return
        if line.startswith("ERR"):
            raise RuntimeError(line)
    raise TimeoutError("device did not report READY; is the sd-upload firmware flashed?")


def put(port, local, remote):
    data = local.read_bytes()
    port.write(f"PUT {remote} {len(data)}\n".encode())
    expect(port, "GO")
    for offset in range(0, len(data), CHUNK_SIZE):
        port.write(data[offset : offset + CHUNK_SIZE])
        expect(port, "ACK")
    done = expect(port, "DONE")
    if int(done.split()[1], 16) != zlib.crc32(data):
        raise RuntimeError(f"checksum mismatch for {remote}")


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    port_name, local_dir, card_dir = sys.argv[1], Path(sys.argv[2]), sys.argv[3].rstrip("/")
    files = sorted(p for p in local_dir.iterdir() if p.is_file())

    with serial.Serial(port_name, BAUD, timeout=0.2) as port:
        # Opening the port asserts RTS, which the board's auto-reset circuit turns into holding the chip
        # in reset. Pulse it once for a clean boot, then release it.
        port.dtr = False
        port.rts = True
        time.sleep(0.1)
        port.rts = False
        wait_until_ready(port)
        start = time.time()
        for index, local in enumerate(files, 1):
            put(port, local, f"{card_dir}/{local.name}")
            print(f"[{index}/{len(files)}] {card_dir}/{local.name}")

        port.write(f"LS {card_dir}\n".encode())
        listed = []
        while (line := read_line(port)) != "END":
            if line.startswith("F "):
                listed.append(line)
        port.write(b"INFO\n")
        info = expect(port, "INFO").split()
        print(f"Uploaded {len(files)} files in {time.time() - start:.0f}s; {len(listed)} files now in {card_dir}, "
              f"{info[2]} of {info[1]} MB used on the card")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Boot the real firmware in Espressif's ESP32 emulator and stream telemetry into it.

    tools/qemu_smoke.py <merged-flash.bin> [seconds]

Build the image with FlashMode=dio (the emulator does not support QIO), e.g.
    arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=min_spiffs,FlashMode=dio \
        --build-path build ship_hud
and pass build/ship_hud.ino.merged.bin. QEMU_XTENSA points at qemu-system-xtensa.

Passes when the sketch reaches "HUD ready", decodes the demo flight streamed
over the emulated UART (its "Telemetry: N frames" status lines), and never
resets or panics.
There is no display or touch in the emulator, so this checks boot, memory,
display/DMA initialisation and the main loop, not the picture.
"""
import math
import os
import socket
import struct
import subprocess
import sys
import time

QEMU = os.environ.get("QEMU_XTENSA", "qemu-system-xtensa")
PORT = 5555


# ---- telemetry (same wire format as the sender, see lib/hud_proto) ----------

def crc8(data: bytes) -> int:
    """CRC-8, polynomial 0x31, init 0, MSB first (lib/hud_proto/crc8.h)."""
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x31) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def cobs(data: bytes) -> bytes:
    out, block = bytearray(), bytearray()
    for b in data:
        if b == 0:
            out += bytes([len(block) + 1]) + block
            block = bytearray()
        else:
            block.append(b)
            if len(block) == 254:
                out += bytes([255]) + block
                block = bytearray()
    out += bytes([len(block) + 1]) + block
    return bytes(out)


def frame(msg_type: int, payload: bytes) -> bytes:
    raw = bytes([msg_type]) + payload
    return b"\x00" + cobs(raw + bytes([crc8(raw)])) + b"\x00"


def demo_cycle(t: float) -> bytes:
    bank = 25 * math.sin(t * 2 * math.pi / 24)
    pitch = 4 + 6 * math.sin(t * 2 * math.pi / 40)
    hdg = (270 + 40 * (1 - math.cos(t * 2 * math.pi / 24))) % 360
    return b"".join([
        frame(0x02, struct.pack("<hhh", round(pitch * 10), round(bank * 10), round(hdg * 10))),
        frame(0x03, struct.pack("<BHBBBB", 0, 2380, 88, 47, 187, 158)),
        frame(0x04, struct.pack("<HihH", 1124, 4520, int(pitch * 110), 1187)),
        frame(0x05, struct.pack("<hhh", 3, round(100 / math.cos(math.radians(bank))), -6)),
        frame(0x06, struct.pack("<H", 1 if 10 < t % 30 < 12 else 0)),
        frame(0x07, struct.pack("<iihHh", 474502000, 85618000, 2700, 124, 2680)),
        frame(0x08, struct.pack("<BBbb", 25, 2, 12, -4)),
        frame(0x09, struct.pack("<Hih", 0x07, 6000, 2700)),
    ])


# ---- run ------------------------------------------------------------------------

def main() -> int:
    image = sys.argv[1]
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 60
    qemu = subprocess.Popen(
        [QEMU, "-display", "none", "-monitor", "none", "-machine", "esp32", "-m", "4M",
         "-drive", f"file={image},if=mtd,format=raw",
         "-global", "driver=timer.esp32.timg,property=wdt_disable,value=true",
         "-serial", f"tcp:127.0.0.1:{PORT},server=on,wait=on"],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    sock = None
    for _ in range(100):
        try:
            sock = socket.create_connection(("127.0.0.1", PORT))
            break
        except OSError:
            time.sleep(0.1)
    if sock is None:
        print("could not connect to the emulator:", qemu.stderr.read().decode(errors="replace"))
        return 1
    sock.settimeout(0.05)

    log, start, ready_at, sent, next_send = bytearray(), time.time(), None, 0, 0.0
    while time.time() - start < seconds:
        try:
            chunk = sock.recv(4096)
            if not chunk:
                break
            log += chunk
        except socket.timeout:
            pass
        if ready_at is None and b"HUD ready" in log:
            ready_at = time.time()
        if ready_at is not None and time.time() >= next_send:
            data = demo_cycle(time.time() - ready_at)
            sock.sendall(data)
            sent += len(data)
            next_send = time.time() + 0.05   # 20 Hz like the sender
    qemu.terminate()
    qemu.wait()

    text = log.decode("latin1")
    print(text)
    import re
    decoded = sum(int(n) for n in re.findall(r"Telemetry: (\d+) frames", text))
    resets = text.count("rst:") - 1
    panics = [m for m in ("Guru Meditation", "abort()", "assert failed", "Backtrace:") if m in text]
    print(f"---- {'reached' if ready_at else 'did NOT reach'} 'HUD ready'; streamed {sent} bytes; "
          f"firmware decoded {decoded} frames; resets: {resets}; panics: {panics or 'none'}")
    ok = ready_at is not None and resets == 0 and not panics and decoded > 0
    print("qemu smoke test:", "PASSED" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Boot the real firmware in Espressif's ESP32 emulator and stream telemetry into it.

    tools/qemu_smoke.py <merged-flash.bin> [seconds]

Build the image with FlashMode=dio (the emulator does not support QIO), e.g.
    arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=min_spiffs,FlashMode=dio \
        --build-path build ship_hud
and pass build/ship_hud.ino.merged.bin. QEMU_XTENSA points at qemu-system-xtensa.

Checks, in order:
  1. the sketch reaches "HUD ready" without resetting or panicking
  2. it decodes the demo flight streamed over the emulated UART
     (its "Telemetry: N frames" status lines)
  3. the USB configuration commands the installer uses answer correctly
     (HUDCFG INFO / WIFI-OFF / an invalid one)
  4. HUDCFG WIFI stores the network in flash: the emulator has no radio, so
     starting WiFi asserts and resets, and after that reset the firmware must
     read the same network back from flash
The image is copied first because the emulator writes to it.
There is no display or touch in the emulator, so this checks boot, memory,
display/DMA initialisation and the main loop, not the picture.
"""
import binascii
import math
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
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

class Emulator:
    def __init__(self, image: str):
        self.qemu = subprocess.Popen(
            [QEMU, "-display", "none", "-monitor", "none", "-machine", "esp32", "-m", "4M",
             "-drive", f"file={image},if=mtd,format=raw",
             "-global", "driver=timer.esp32.timg,property=wdt_disable,value=true",
             "-serial", f"tcp:127.0.0.1:{PORT},server=on,wait=on"],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        self.sock = None
        for _ in range(100):
            try:
                self.sock = socket.create_connection(("127.0.0.1", PORT))
                break
            except OSError:
                time.sleep(0.1)
        if self.sock is None:
            raise RuntimeError("could not connect to the emulator: "
                               + self.qemu.stderr.read().decode(errors="replace"))
        self.sock.settimeout(0.05)
        self.log = bytearray()

    def text(self) -> str:
        return self.log.decode("latin1")

    def pump(self, seconds: float, until: str = "", start: int = 0, telemetry_from: float = -1) -> bool:
        """Read output for up to `seconds`; stop early once `until` appears after `start`.
        With telemetry_from >= 0, stream the demo flight at 20 Hz meanwhile."""
        end, next_send = time.time() + seconds, 0.0
        while time.time() < end:
            try:
                chunk = self.sock.recv(4096)
                if chunk:
                    self.log += chunk
            except socket.timeout:
                pass
            if until and until in self.text()[start:]:
                return True
            if telemetry_from >= 0 and time.time() >= next_send:
                self.sock.sendall(demo_cycle(time.time() - telemetry_from))
                next_send = time.time() + 0.05
        return bool(until) and until in self.text()[start:]

    def command(self, line: str, expect: str, seconds: float = 30) -> bool:
        mark = len(self.text())
        self.sock.sendall(line.encode() + b"\n")
        return self.pump(seconds, expect, mark)

    def close(self):
        self.qemu.terminate()
        self.qemu.wait()


def main() -> int:
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 45
    work = tempfile.mkdtemp()
    image = os.path.join(work, "flash.bin")
    shutil.copy(sys.argv[1], image)
    emu = Emulator(image)
    results = []

    def check(ok: bool, name: str):
        results.append((ok, name))

    try:
        booted = emu.pump(120, "HUD ready")
        check(booted, "boots to 'HUD ready'")
        if booted:
            emu.pump(seconds, telemetry_from=time.time())
            # The emulator runs slower than real time: let it work off the
            # queued telemetry before talking to it
            emu.pump(15)
            decoded = sum(int(n) for n in re.findall(r"Telemetry: (\d+) frames", emu.text()))
            check(decoded > 0, f"decodes streamed telemetry ({decoded} frames)")
            check(emu.command("HUDCFG INFO", "wifi=off"), "HUDCFG INFO reports firmware and WiFi off")
            check(emu.command("HUDCFG NOPE", "HUDCFG ERR"), "an unknown command is rejected")
            check(emu.command("HUDCFG WIFI-OFF", "HUDCFG OK"), "HUDCFG WIFI-OFF is acknowledged")
            before_wifi = emu.text()
            check("rst:" not in before_wifi.split("HUD ready", 1)[1]
                  and not any(m in before_wifi for m in ("Guru Meditation", "abort()", "assert failed")),
                  "no reset or panic before WiFi is started")

            ssid = "CYD Test Net"
            hexed = binascii.hexlify(ssid.encode()).decode()
            mark = len(emu.text())
            check(emu.command(f"HUDCFG WIFI {hexed} {binascii.hexlify(b'pw 1234').decode()}", "HUDCFG OK"),
                  "HUDCFG WIFI is acknowledged")
            # No radio in the emulator: the WiFi start asserts and the chip resets
            reset = emu.pump(60, "rst:", mark)
            stored = reset and emu.pump(120, f"Connecting to WiFi '{ssid}'", emu.text().index("rst:", mark))
            check(stored, "the stored network is read back from flash after a reset")
    finally:
        emu.close()
        shutil.rmtree(work, ignore_errors=True)

    print(emu.text())
    print("----")
    for ok, name in results:
        print(("ok   " if ok else "FAIL ") + name)
    ok = bool(results) and all(r[0] for r in results)
    print("qemu smoke test:", "PASSED" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

# MSFS 2024 HUD

A flight data HUD for the **ESP32-2432S024C** (Cheap Yellow Display), built with LVGL. Receives binary telemetry over USB serial from Microsoft Flight Simulator 2024 via SimConnect.

Supports 4 switchable display screens:
- **MSFS Gyroscope** — Artificial horizon with pitch, roll, and heading
- **MSFS Engine Gauges** — RPM arc, throttle bar, oil temp/pressure, fuel flow
- **MSFS Flight Data** — Airspeed, altitude, vertical speed, ground speed
- **MSFS G-Force Meter** — Vertical/lateral/longitudinal G with peak tracking

Tap the touchscreen to cycle through screens.

## Hardware

- **Board**: ESP32-2432S024C (2.4" 320x240 ILI9341, capacitive touch CST816S)
- **Touch**: CST816S on I2C (SDA=33, SCL=32)
- **Display**: ILI9341 on HSPI

## Protocol

Binary frames over 115200 baud USB serial using COBS framing with CRC8 error checking.

### Wire format

```
[0x00] [COBS-encoded: msg_type | payload... | CRC8] [0x00]
```

### Message types

| ID | Name | Direction | Payload |
|----|------|-----------|---------|
| `0x02` | Attitude | PC -> ESP32 | 6 bytes: pitch, roll, heading (int16 LE, tenths of degrees) |
| `0x03` | Engine | PC -> ESP32 | 6 bytes: rpm(u16), throttle, fuel_flow, oil_temp, oil_press |
| `0x04` | FlightData | PC -> ESP32 | 10 bytes: airspeed(u16), altitude(i32), vspeed(i16), gs(u16) |
| `0x05` | GForce | PC -> ESP32 | 6 bytes: gx, gy, gz (int16 LE, hundredths of G) |

The protocol is defined in:
- **`lib/hud_proto/`** — C headers (used by the ESP32 sketch)
- **`msfs-sender/msfs_sender/protocol.py`** — Python implementation (used by the sender)

## Project Structure

```
├── lib/
│   ├── hud_proto/            # C protocol headers
│   │   ├── frame_decoder.h   # Stream-fed COBS frame decoder
│   │   ├── messages.h        # Packed C structs
│   │   ├── cobs.h            # COBS decode
│   │   └── crc8.h            # CRC8/MAXIM
│   └── hud_widgets/          # C++ LVGL widget library
│       ├── GyroHorizon.h     # Artificial horizon (gyroscope)
│       ├── EngineGauges.h    # RPM, throttle, oil, fuel flow
│       ├── FlightData.h      # Airspeed, altitude, vspeed
│       ├── GForceMeter.h     # G-force arcs with peak tracking
│       └── ColorScale.h      # Threshold-based color mapping
├── msfs-sender/              # Python MSFS 2024 sender
│   ├── msfs_sender/
│   │   ├── __main__.py       # CLI entry point
│   │   ├── protocol.py       # COBS + CRC8 framing
│   │   └── simconnect_source.py  # SimConnect wrapper
│   └── tests/
│       └── test_protocol.py  # Protocol unit tests
└── ship_hud/
    └── ship_hud.ino          # Main ESP32 sketch (4-screen cycling)
```

## Quick Start

### ESP32

See [docs/SETUP.md](docs/SETUP.md) for full Arduino IDE and PlatformIO setup instructions.

### Sender — MSFS 2024 (Python)

See [docs/MSFS_PLUGIN.md](docs/MSFS_PLUGIN.md) for full setup and installation instructions.

**Pre-built .exe** — Download `msfs-gyro-sender.exe` from [GitHub Actions](../../actions) and run `msfs-gyro-sender.exe COM6`. No Python needed.

**From source:**

```sh
cd msfs-sender
pip install -r requirements.txt
python -m msfs_sender COM6           # default: 20Hz, 115200 baud
python -m msfs_sender COM6 --hz 30   # faster updates
```

## License

GNU General Public License v3.0. See [LICENSE](LICENSE).

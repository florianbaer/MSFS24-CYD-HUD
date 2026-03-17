# MSFS 2024 Flight Data Sender

A companion app that reads flight data from Microsoft Flight Simulator 2024 via the native SimConnect C# API and sends it to the ESP32 display over USB serial or WiFi UDP. Supports 7 HUD screens.

## Installation (Pre-built .exe)

1. Download `msfs-hud-sender.exe` from the latest [GitHub Actions build](../../actions)
2. Connect your ESP32 display via USB
3. Start MSFS 2024 and load into a flight
4. Run:

```
msfs-hud-sender.exe COM6
```

That's it. The exe is a standalone Windows executable — no runtime dependencies needed.

### Options

```
msfs-hud-sender.exe COM6 --baud 115200 --hz 30
msfs-hud-sender.exe 192.168.1.50 --udp --port 4242
```

| Flag | Default | Description |
|------|---------|-------------|
| `--baud` | 115200 | Serial baud rate |
| `--hz` | 20 | Send rate in Hz |
| `--udp` | — | Use UDP transport instead of serial |
| `--port` | 4242 | UDP port (with --udp) |

## Auto-start with MSFS

MSFS can automatically launch companion apps on startup via an `exe.xml` file.

**File location:**
- **Steam**: `%APPDATA%\Microsoft Flight Simulator 2024\exe.xml`
- **MS Store**: `%LOCALAPPDATA%\Packages\Microsoft.FlightSimulator_8wekyb3d8bbwe\LocalCache\exe.xml`

If the file doesn't exist, create it. If it already exists, just add the `<Launch.Addon>` block inside the existing `<SimBase.Document>`.

```xml
<?xml version="1.0" encoding="windows-1252"?>
<SimBase.Document Type="Launch" version="1,0">
  <Descr>Launch</Descr>
  <Filename>exe.xml</Filename>
  <Disabled>False</Disabled>
  <Launch.ManualLoad>False</Launch.ManualLoad>
  <Launch.Addon>
    <Name>ESP32 HUD Display</Name>
    <Disabled>False</Disabled>
    <ManualLoad>False</ManualLoad>
    <Path>C:\Your\Path\msfs-hud-sender.exe</Path>
    <CommandLine>COM6</CommandLine>
  </Launch.Addon>
</SimBase.Document>
```

Replace `C:\Your\Path\` with the actual folder where you saved the exe, and `COM6` with your ESP32 serial port.

## Building from Source

Requires **Windows 10/11**, **.NET 9+ SDK**, and **MSFS 2024 SDK** (for SimConnect DLL).

```sh
# Set MSFS_SDK environment variable to your SDK installation path
set MSFS_SDK=C:\MSFS SDK

cd msfs-sender
dotnet build
dotnet run --project MsfsHudSender -- COM6
```

### Building the .exe

```sh
cd msfs-sender
dotnet publish MsfsHudSender/MsfsHudSender.csproj -c Release -r win-x64 --self-contained -p:PublishSingleFile=true -o publish
# Output: publish/msfs-hud-sender.exe
```

## Display Screens

The ESP32 display has 7 screens cycled by touch tap:

| # | Screen | Data shown |
|---|--------|------------|
| 0 | **MSFS Gyroscope** | Artificial horizon with pitch, roll, heading |
| 1 | **MSFS Engine Gauges** | RPM arc, throttle bar, oil temp/pressure, fuel flow |
| 2 | **MSFS Flight Data** | Airspeed, altitude, vertical speed, ground speed |
| 3 | **MSFS G-Force Meter** | Vertical/lateral/longitudinal G, peak tracking |
| 4 | **MSFS Navigation** | Lat/lon, heading bug, waypoint distance/bearing |
| 5 | **MSFS Config** | Flaps, gear status, elevator/rudder trim |
| 6 | **MSFS Autopilot** | AP master, mode annunciators, target alt/hdg |

**Tap the touchscreen** to cycle to the next screen. All screens receive data simultaneously.

Alert warnings (stall, overspeed, gear unsafe, engine fire) overlay on all screens with blinking text and a heartbeat LED pattern.

## How It Works

1. The sender connects to MSFS via the native SimConnect C# API
2. Each loop reads all flight data (attitude, engine, flight, G-force, alerts, nav, config, autopilot)
3. Values are packed into binary protocol messages and framed with COBS encoding + CRC8 checksum
4. All frames are sent over USB serial (or WiFi UDP) to the ESP32 at the configured Hz rate
5. The ESP32 dispatches each message to the appropriate widget

## Protocol

Eight message types are sent every cycle. Wire format: `[0x00] [COBS-encoded: msg_type | payload | CRC8] [0x00]`

| ID | Name | Payload | Size |
|----|------|---------|------|
| 0x02 | Attitude | pitch(i16), roll(i16), heading(i16) | 6B |
| 0x03 | Engine | engine_idx(u8), rpm(u16), throttle(u8), fuel_flow(u8), oil_temp(u8), oil_press(u8) | 7B |
| 0x04 | FlightData | airspeed(u16), altitude(i32), vspeed(i16), ground_speed(u16) | 10B |
| 0x05 | GForce | gx(i16), gy(i16), gz(i16) | 6B |
| 0x06 | Alerts | flags(u16) bitfield | 2B |
| 0x07 | NavData | lat(i32), lon(i32), hdg_bug(i16), wp_dist(u16), wp_bearing(i16) | 14B |
| 0x08 | Config | flaps_pct(u8), gear_state(u8), elev_trim(i8), rudder_trim(i8) | 4B |
| 0x09 | Autopilot | mode_flags(u16), target_alt(i32), target_hdg(i16) | 8B |

All multi-byte fields are little-endian.

## Troubleshooting

**"SimConnect connection failed"**
-> Make sure MSFS 2024 is running and you're in a flight (not the main menu).

**Serial port not found**
-> Check the port name in Device Manager. On Windows it's typically `COM3`-`COM9`.

**No data on display**
-> Verify baud rates match (default 115200). The display shows "NO DATA" if no messages arrive for 2 seconds. Tap to cycle through all 7 screens.

**WiFi not connecting**
-> Copy `wifi_config.h.example` to `wifi_config.h`, fill in your credentials, and reflash the ESP32 firmware.

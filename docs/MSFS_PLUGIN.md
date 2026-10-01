# MSFS 2024 Flight Data Sender

A companion app that reads flight data from Microsoft Flight Simulator 2024 via the native SimConnect C# API and sends it to the ESP32 display over USB serial or WiFi UDP. Supports 7 HUD screens.

> **Using the Windows installer?** `Install.cmd` in the repository root builds the sender, installs it to `%LOCALAPPDATA%\MsfsCydHud\app`, registers it in `exe.xml` and adds Start-menu shortcuts. This page covers doing it by hand.

## Requirements

- **Windows 10/11** with MSFS 2024
- **.NET 10 SDK**
- **MSFS 2024 SDK** — it provides the SimConnect libraries. They cannot be downloaded separately, which is why there is no pre-built download: an exe built without the SDK (for example by this repository's CI) starts, but can only report that SimConnect is missing.

## Building

```sh
:: Point MSFS_SDK at your SDK installation (the folder that contains "SimConnect SDK")
set MSFS_SDK=C:\MSFS 2024 SDK

cd msfs-sender
dotnet publish MsfsHudSender/MsfsHudSender.csproj -c Release -r win-x64 --self-contained -p:PublishSingleFile=true -o publish
```

This produces `publish/msfs-hud-sender.exe` with `SimConnect.dll` next to it; keep the two files together. If the build prints *"MSFS SDK not found"*, `MSFS_SDK` does not point at the SDK.

For development, `dotnet run --project MsfsHudSender -- COM6` works as well.

## Running

1. Connect your ESP32 display via USB
2. Run:

```
msfs-hud-sender.exe            :: same as "auto": finds the display by its USB chip
msfs-hud-sender.exe COM6       :: or a fixed port
```

`auto` (the default) looks for the USB-serial chips used on these boards (CH340, CH9102, CP210x) and waits until one is plugged in, so the COM number may change between USB sockets without breaking auto-start.

To check the display without the simulator, send a synthetic flight:

```
msfs-hud-sender.exe --demo
```

The sender can be started before or after MSFS: it waits until the simulator answers, starts streaming, and exits when MSFS quits. Data only becomes meaningful once you are in a flight.

### Options

```
msfs-hud-sender.exe COM6 --baud 115200 --hz 30
msfs-hud-sender.exe 192.168.1.50 --udp --port 4242
```

| Flag | Default | Description |
|------|---------|-------------|
| `--baud` | 115200 | Serial baud rate |
| `--hz` | 20 | Send rate in Hz (1-60) |
| `--udp` | — | Use UDP transport instead of serial; the target is the display's IP address or host name |
| `--demo` | — | Send a synthetic flight instead of MSFS data (does not need MSFS or the SDK) |
| `--port` | 4242 | UDP port (with --udp) |

## Auto-start with MSFS

MSFS can automatically launch companion apps on startup via an `exe.xml` file.

**File location:**
- **Steam**: `%APPDATA%\Microsoft Flight Simulator 2024\exe.xml`
- **MS Store**: `%LOCALAPPDATA%\Packages\Microsoft.Limitless_8wekyb3d8bbwe\LocalCache\exe.xml`

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
    <CommandLine>auto</CommandLine>
  </Launch.Addon>
</SimBase.Document>
```

Replace `C:\Your\Path\` with the actual folder where you saved the exe. `auto` finds the display on USB; use `COM6` for a fixed port or `192.168.1.50 --udp` for WiFi.

## Display Screens

The ESP32 display has 7 screens cycled by touch tap:

| # | Screen | Data shown |
|---|--------|------------|
| 0 | **MSFS Gyroscope** | Artificial horizon with pitch, roll, heading |
| 1 | **MSFS Engine Gauges** | RPM arc, throttle bar, oil temp/pressure, fuel flow |
| 2 | **MSFS Flight Data** | Airspeed, altitude, vertical speed, ground speed |
| 3 | **MSFS G-Force Meter** | Vertical/lateral/longitudinal G, peak tracking |
| 4 | **MSFS Navigation** | Compass card with heading bug and waypoint bearing pointer, lat/lon, waypoint distance/bearing |
| 5 | **MSFS Config** | Flaps, gear status, elevator/rudder trim |
| 6 | **MSFS Autopilot** | AP master, mode annunciators, target alt/hdg |

**Tap the touchscreen** to cycle to the next screen. All screens receive data simultaneously.

Alert warnings (stall, overspeed, gear unsafe, engine fire) overlay on all screens with blinking text and a heartbeat LED pattern.

## How It Works

1. The sender connects to MSFS via the managed SimConnect API
2. Each loop reads all flight data (attitude, engine, flight, G-force, alerts, nav, config, autopilot) and converts it to the wire units — e.g. SimConnect reports pitch positive nose-down, the wire format is positive nose-up; vertical G is the load factor (1.0 in level flight); heading is magnetic
3. Values are packed into binary protocol messages and framed with COBS encoding + CRC8 checksum
4. All frames are sent over USB serial (or WiFi UDP) to the ESP32 at the configured Hz rate
5. The ESP32 dispatches each message to the appropriate widget

## Protocol

Eight message types are sent every cycle (Autopilot only for aircraft that have one; Engine once per engine, the display shows engine 1). Wire format: `[0x00] [COBS-encoded: msg_type | payload | CRC8] [0x00]`

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

**"Waiting for MSFS 2024..." never goes away**
-> MSFS is not running, or SimConnect is not reachable. The sender keeps retrying every 2 seconds.

**"This build was compiled without the MSFS SDK"**
-> The exe was built without `MSFS_SDK` set. Rebuild as described under [Building](#building).

**"Unable to load DLL 'SimConnect.dll'"**
-> `SimConnect.dll` must be in the same folder as `msfs-hud-sender.exe`.

**"Waiting for the display to be plugged in"** (with `auto`)
-> No CH340/CH9102/CP210x device is connected, or its driver is missing (Device Manager shows it with a warning sign). Pass the port name explicitly if your board uses a different USB chip.

**"Could not open serial port"**
-> The sender lists the ports it can see. Check the port name in Device Manager and close anything else that has the port open (Arduino serial monitor, `pio device monitor`).

**No data on display**
-> Verify baud rates match (default 115200). The display shows "NO DATA" if no messages arrive for 2 seconds. Tap to cycle through all 7 screens. While telemetry arrives, the display's serial log prints `Telemetry: N frames in 10 s` (about 1600 at 20 Hz); if that line is missing, nothing valid is reaching it.

**Display stays black, freezes, or shows garbage**
-> Check the serial log at boot: `Display: DMA double-buffered` or `single buffer`, and the free heap. If DMA is the problem on your board, build with `-DHUD_USE_DMA=0` (PlatformIO: add it to `build_flags`; arduino-cli: `--build-property "compiler.cpp.extra_flags=-DHUD_USE_DMA=0"`).

**WiFi not connecting**
-> Copy `wifi_config.h.example` to `wifi_config.h`, fill in your credentials, and reflash the ESP32 firmware. See [SETUP.md](SETUP.md#wifi-optional).

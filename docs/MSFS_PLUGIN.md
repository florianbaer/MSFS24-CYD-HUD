# MSFS 2024 Flight Data Sender

A companion app that reads flight data from Microsoft Flight Simulator 2020/2024 over SimConnect and sends it to the ESP32 display over USB serial or WiFi UDP. Supports 7 HUD screens.

> **Using the Windows installer?** `MsfsCydHud-Setup.exe` ships the sender ready-built, installs it to `%LOCALAPPDATA%\MsfsCydHud\app`, registers it in `exe.xml` and adds Start-menu shortcuts. This page covers doing it by hand.

## Requirements

- **Windows 10/11** with MSFS 2024 (or 2020)
- Nothing else for the ready-built `msfs-hud-sender.exe` (from Setup.exe or the CI artifacts)
- **.NET 10 SDK** only to build it yourself

The MSFS SDK is **not** needed: the sender has its own SimConnect client instead of Microsoft's DLLs. It talks to the simulator over the named pipe `\\.\pipe\Microsoft Flight Simulator\SimConnect` that MSFS opens for local clients (falling back to the TCP port MSFS registers), or over TCP to another PC with `--simconnect`.

## Building

```sh
cd msfs-sender
dotnet publish MsfsHudSender/MsfsHudSender.csproj -c Release -r win-x64 --self-contained -p:PublishSingleFile=true -p:EnableCompressionInSingleFile=true -o publish
```

This produces a single `publish/msfs-hud-sender.exe`. For development, `dotnet run --project MsfsHudSender -- COM6` works as well (the protocol and SimConnect tests run on any OS: `dotnet test`).

**Fallback to Microsoft's client:** with the MSFS SDK installed, `set MSFS_SDK=C:\MSFS 2024 SDK` and add `-p:UseSdkSimConnect=true` to build both clients; `--sdk-simconnect` then selects Microsoft's. Keep `SimConnect.dll` next to the exe in that case.

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
| `--demo` | — | Send a synthetic flight instead of MSFS data (does not need MSFS) |
| `--simconnect` | — | `host:port` of MSFS on another PC (SimConnect over TCP, enabled in that PC's `SimConnect.xml`) |
| `--sdk-simconnect` | — | Use Microsoft's SimConnect client instead of the built-in one (only in builds with `-p:UseSdkSimConnect=true`) |
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

1. The sender connects to MSFS with its built-in SimConnect client (named pipe, or TCP with `--simconnect`)
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

**"SimConnect rejected request ..."**
-> The simulator did not accept a simulation variable (for example on an aircraft without that system). The other values keep flowing; please open an issue with the line.

**"Waiting for the display to be plugged in"** (with `auto`)
-> No CH340/CH9102/CP210x device is connected, or its driver is missing (Device Manager shows it with a warning sign). Pass the port name explicitly if your board uses a different USB chip.

**"Could not open serial port"**
-> The sender lists the ports it can see. Check the port name in Device Manager and close anything else that has the port open (Arduino serial monitor, `pio device monitor`).

**No data on display**
-> Verify baud rates match (default 115200). The display shows "NO DATA" if no messages arrive for 2 seconds. Tap to cycle through all 7 screens. While telemetry arrives, the display's serial log prints `Telemetry: N frames in 10 s` (about 1600 at 20 Hz); if that line is missing, nothing valid is reaching it.

**Display stays black, freezes, or shows garbage**
-> Check the serial log at boot: `Display: DMA double-buffered` or `single buffer`, and the free heap. If DMA is the problem on your board, build with `-DHUD_USE_DMA=0` (PlatformIO: add it to `build_flags`; arduino-cli: `--build-property "compiler.cpp.extra_flags=-DHUD_USE_DMA=0"`).

**WiFi not connecting**
-> Run *Set up or reconfigure MSFS CYD HUD* from the Start menu and choose WiFi again (2.4 GHz networks only), or send the settings by hand. See [SETUP.md](SETUP.md#wifi-optional).

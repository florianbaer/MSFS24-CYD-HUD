# Setup Guide

> **On Windows, `MsfsCydHud-Setup.exe` does all of this for you** and flashes a ready-made firmware (see the [README](../README.md#windows-installer-recommended)). The steps below are for building the firmware yourself: PlatformIO, the Arduino IDE, other operating systems.

The display and LVGL settings live in [`config/`](../config) and are shared by both build systems:

| File | Purpose |
|------|---------|
| `config/lv_conf.h` | LVGL configuration (colour depth, heap size, the five font sizes the widgets use) |
| `config/User_Setup.h` | TFT_eSPI driver and pinout for the ESP32-2432S024C |

Tested versions: **lvgl 9.2.2**, **TFT_eSPI 2.5.43**, ESP32 Arduino core **3.3.12** (Arduino IDE / arduino-cli) and **2.0.17** (PlatformIO `espressif32@6.10.0`).

## PlatformIO

The repository contains a ready-to-use `platformio.ini`; libraries and configuration are picked up automatically.

```sh
pio run -t upload
pio device monitor
```

## Arduino IDE

### 1. Add ESP32 Board Support

In **File > Preferences**, add this URL to **Additional Board Manager URLs**:

```
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

Then go to **Tools > Board > Boards Manager**, search for **esp32** by Espressif Systems, and install version **3.3.12** (any 3.x should work).

### 2. Select Board

- **Tools > Board**: `ESP32 Dev Module`
- **Tools > Flash Size**: `4MB`
- **Tools > Partition Scheme**: `Minimal SPIFFS (1.9MB APP with OTA)` (the default 1.3 MB app partition is ~99% full with WiFi enabled)
- **Tools > Upload Speed**: `921600`
- **Tools > Port**: Select the COM port for your board

### 3. Install Libraries

Install via **Sketch > Include Library > Manage Libraries**:

| Library | Version |
|---------|---------|
| **lvgl** | 9.2.2 |
| **TFT_eSPI** | 2.5.43 |

### 4. Configure TFT_eSPI

Replace `User_Setup.h` in your TFT_eSPI library folder (`Arduino/libraries/TFT_eSPI/User_Setup.h`) with [`config/User_Setup.h`](../config/User_Setup.h).

### 5. Configure LVGL

Copy [`config/lv_conf.h`](../config/lv_conf.h) next to the lvgl library folder (`Arduino/libraries/lv_conf.h`, **not** inside `lvgl/`).

### 6. Upload

Open `ship_hud/ship_hud.ino` and upload.

> **Note:** The widget and protocol headers are duplicated in `ship_hud/` because Arduino IDE cannot resolve paths outside the sketch folder. The canonical sources are `lib/hud_widgets/` and `lib/hud_proto/`; run `tools/sync_headers.sh` after editing them.

### arduino-cli

The same steps on the command line (this is what CI runs):

```sh
URL=https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index --additional-urls "$URL"
arduino-cli core install esp32:esp32@3.3.12 --additional-urls "$URL"
arduino-cli lib install lvgl@9.2.2 TFT_eSPI@2.5.43

LIBS="$(arduino-cli config get directories.user)/libraries"
cp config/lv_conf.h "$LIBS/lv_conf.h"
cp config/User_Setup.h "$LIBS/TFT_eSPI/User_Setup.h"

FQBN=esp32:esp32:esp32:PartitionScheme=min_spiffs
arduino-cli compile --fqbn $FQBN ship_hud
arduino-cli upload  --fqbn $FQBN -p <PORT> ship_hud
```

## WiFi (optional)

USB serial works out of the box. To also receive telemetry over WiFi, the display needs your network (2.4 GHz). It keeps it in its flash memory, so this survives re-flashing with the same firmware build and never needs a rebuild:

- **With the installer:** choose WiFi in the guided setup. It sends the settings over USB and shows the display's IP address.
- **By hand:** send one line over the serial port (115200 baud), with the network name and password as the hex of their UTF-8 bytes (`-` for an open network):
  ```
  HUDCFG WIFI 486f6d65 70617373776f7264      (network "Home", password "password")
  HUDCFG WIFI-OFF                            (back to USB only)
  HUDCFG INFO                                (firmware version, WiFi state, IP address)
  ```
  The display answers `HUDCFG OK`, and prints `WiFi connected. Listening on <ip>:4242` once it has joined; that IP is what you pass to the sender (`msfs-hud-sender <ip> --udp`).
- **At build time:** copy `ship_hud/wifi_config.h.example` to `ship_hud/wifi_config.h` and fill it in (the file is gitignored). It is used when nothing is stored on the display.

The display keeps listening on USB serial as well, and reconnects by itself if the network drops.

> **Flash size:** the firmware with WiFi needs more than the default 1.3 MB app partition, which is why every build here (PlatformIO, CI, the installer) uses **Minimal SPIFFS (1.9MB APP with OTA)**. With the default scheme, build with `-DHUD_WIFI=0` for a USB-only firmware.

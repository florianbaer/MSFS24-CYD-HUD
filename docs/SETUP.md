# Setup Guide

> **On Windows, the installer does all of this for you:** double-click `Install.cmd` in the repository root (see the [README](../README.md#windows-installer-recommended)). The steps below are for PlatformIO, the Arduino IDE, other operating systems, or if you want to know what the installer does.

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

USB serial works out of the box. To also receive telemetry over WiFi:

1. Copy `ship_hud/wifi_config.h.example` to `ship_hud/wifi_config.h` and fill in your network name and password (the file is gitignored).
2. Rebuild and flash.
3. Open the serial monitor at 115200 baud: once connected the display prints its IP address, which is what you pass to the sender (`msfs-hud-sender <ip> --udp`).

The display keeps listening on USB serial as well, and reconnects by itself if the network drops.

> **Flash size:** with WiFi enabled the firmware fills about 99% of the default 1.3 MB app partition on core 3.x, which is why every build here (PlatformIO, CI, the installer) uses **Minimal SPIFFS (1.9MB APP with OTA)**.

// MSFS 2024 HUD Display for ESP32-2432S024C (Capacitive touch)
// Receives COBS-framed binary telemetry via USB serial (or WiFi UDP)
// Touch chip: CST816S on I2C
// 7 screens: Gyro, Engine, Flight Data, G-Force, Nav, Config, Autopilot
//
// Builds with arduino-esp32 3.x (Arduino IDE / arduino-cli) and 2.x (PlatformIO).

#include <lvgl.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>
#include "hud_widgets.h"
#include "hud_proto.h"

#define HUD_FW_VERSION "1.2.0"

// WiFi/UDP support. The network is normally set by the installer over USB and
// stored in flash (see config_command.h); a wifi_config.h next to the sketch
// still works and becomes the default when nothing is stored.
// Build with -DHUD_WIFI=0 for a USB-only firmware that fits the default
// 1.3 MB app partition.
#ifndef HUD_WIFI
#define HUD_WIFI 1
#endif
#if HUD_WIFI
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#define WIFI_ENABLED
#if __has_include("wifi_config.h")
#include "wifi_config.h"
#endif
#endif
#ifndef UDP_PORT
#define UDP_PORT 4242
#endif

#define TFT_HOR_RES   320
#define TFT_VER_RES   240
// Two draw buffers of 30 lines each. With DMA, LVGL renders into one buffer
// while the other is still streaming to the panel, so drawing and the SPI
// transfer overlap instead of taking turns: smoother motion, no tearing bands.
#define DRAW_BUF_LINES 30

// Set to 0 (e.g. -DHUD_USE_DMA=0) to push pixels with the CPU only: for a board
// whose display misbehaves with DMA, and for the ESP32 emulator, which does not
// emulate the display's SPI bus (see tools/qemu_smoke.py).
#ifndef HUD_USE_DMA
#define HUD_USE_DMA 1
#endif
#define DRAW_BUF_SIZE  (TFT_HOR_RES * DRAW_BUF_LINES * (LV_COLOR_DEPTH / 8))

#define BACKLIGHT_PIN 27
#define BACKLIGHT_PWM_HZ   5000
#define BACKLIGHT_BRIGHTNESS 255  // 0..255; lower it for night flying

TFT_eSPI tft = TFT_eSPI();
bool useDma = false;

// A 30-line DMA transfer takes ~3 ms at 55 MHz. One that is still running after
// this long never will (seen in the ESP32 emulator); TFT_eSPI's dmaWait() would
// then block forever, so wait with a limit and fall back to CPU pushes instead.
static const uint32_t DMA_TIMEOUT_MS = 100;

static bool waitForDma() {
  uint32_t start = millis();
  while (tft.dmaBusy()) {
    if (millis() - start > DMA_TIMEOUT_MS) return false;
  }
  return true;
}

static void disableDma(const char* why) {
  useDma = false;
  tft.endWrite();  // release the bus kept claimed for DMA
  Serial.printf("Display: DMA %s, switching to CPU transfers\n", why);
}

void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  if (useDma && !waitForDma()) disableDma("stalled");
  if (useDma) {
    // The bus stays claimed (startWrite in setup); the previous buffer is done,
    // hand this one to DMA and let LVGL carry on with the other.
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushPixelsDMA((uint16_t *)px_map, w * h);
  } else {
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)px_map, w * h, true);
    tft.endWrite();
  }
  lv_display_flush_ready(disp);
}

/// Push one black line by DMA and check it completes: some setups never finish
/// a DMA transfer, and finding out here keeps the first frame from hanging.
static bool dmaSelfTest(uint8_t* buf) {
  memset(buf, 0, TFT_HOR_RES * 2);
  tft.setAddrWindow(0, 0, TFT_HOR_RES, 1);
  tft.pushPixelsDMA((uint16_t *)buf, TFT_HOR_RES);
  return waitForDma();
}

static uint8_t* allocDrawBuf(bool dma) {
  uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | (dma ? MALLOC_CAP_DMA : 0);
  return (uint8_t*)heap_caps_aligned_alloc(4, DRAW_BUF_SIZE, caps);
}

static void backlightInit() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(BACKLIGHT_PIN, BACKLIGHT_PWM_HZ, 8);
  ledcWrite(BACKLIGHT_PIN, BACKLIGHT_BRIGHTNESS);
#else
  ledcSetup(0, BACKLIGHT_PWM_HZ, 8);
  ledcAttachPin(BACKLIGHT_PIN, 0);
  ledcWrite(0, BACKLIGHT_BRIGHTNESS);
#endif
}

// LVGL time base
static uint32_t lvgl_tick_cb() { return millis(); }

// CST816S I2C touch
#define TOUCH_SDA 33
#define TOUCH_SCL 32
#define TOUCH_INT 21
#define TOUCH_RST 25
#define CST816S_ADDR 0x15

void touchInit() {
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(10);
  digitalWrite(TOUCH_RST, HIGH);
  delay(50);
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  pinMode(TOUCH_INT, INPUT);
}

bool touchRead(uint16_t *x, uint16_t *y) {
  Wire.beginTransmission(CST816S_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission() != 0) return false;

  Wire.requestFrom((uint8_t)CST816S_ADDR, (uint8_t)5);
  if (Wire.available() < 5) return false;

  uint8_t touchPoints = Wire.read();
  uint8_t xHigh = Wire.read();
  uint8_t xLow  = Wire.read();
  uint8_t yHigh = Wire.read();
  uint8_t yLow  = Wire.read();

  if (touchPoints == 0) return false;

  *x = ((xHigh & 0x0F) << 8) | xLow;
  *y = ((yHigh & 0x0F) << 8) | yLow;
  return true;
}

// ---- Screen navigation ----
// Swipe left/right on the display, or press the BOOT button (next screen).
// Taps go to the controls on the screen (autopilot buttons, peak-G reset).

static const int NUM_SCREENS = 7;
lv_obj_t* screens[NUM_SCREENS] = {};
int currentScreen = 0;
int pendingScreenStep = 0;  // set by a swipe, applied in loop()
SwipeDetector swipe;
PageIndicator pageDots;

#define BOOT_BUTTON_PIN 0     // the "BOOT" button: free to use once the sketch runs
bool bootWasDown = false;
uint32_t bootChangedMs = 0;

void my_touchpad_read(lv_indev_t * indev, lv_indev_data_t * data) {
  uint16_t tx, ty;
  bool pressed = touchRead(&tx, &ty);
  // Panel is mounted in portrait; rotate into the landscape UI
  int step = feedTouch(swipe, indev, data, pressed, ty, TFT_VER_RES - 1 - tx);
  if (step != 0) pendingScreenStep = step;
}

// ---- MSFS widgets ----

GyroHorizon gyro;
EngineGauges engine;
FlightData flightData;
GForceMeter gforce;
NavDisplay nav;
AircraftConfig config;
AutopilotStatus autopilot;
AlertIndicator alert;

FrameDecoder decoder;

// ---- "NO DATA" timeout ----
uint32_t lastDataMs = 0;
// Frames decoded since the last status line (printed every 10 s while data flows)
uint32_t framesSinceReport = 0;
uint32_t lastReportMs = 0;
static const uint32_t REPORT_MS = 10000;
static const uint32_t NO_DATA_TIMEOUT_MS = 2000;
lv_obj_t* lblNoData = nullptr;

LineCollector cmdLine;

#ifdef WIFI_ENABLED
WiFiUDP udp;
bool udpListening = false;
bool wifiOn = false;
char wifiSsid[33] = {};
char wifiPass[64] = {};

// Credentials live in NVS ("hud" namespace), written by "HUDCFG WIFI ..."
static void loadWifiSettings() {
  Preferences prefs;
  prefs.begin("hud", true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();
#ifdef WIFI_SSID
  if (ssid.length() == 0) { ssid = WIFI_SSID; pass = WIFI_PASS; }
#endif
  strncpy(wifiSsid, ssid.c_str(), sizeof(wifiSsid) - 1);
  strncpy(wifiPass, pass.c_str(), sizeof(wifiPass) - 1);
}

static void saveWifiSettings(const char* ssid, const char* pass) {
  Preferences prefs;
  prefs.begin("hud", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
}

static void stopWifi() {
  if (udpListening) { udp.stop(); udpListening = false; }
  if (wifiOn) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiOn = false;
  }
}

// Non-blocking: loop() starts listening once the connection is up,
// and again after every reconnect.
static void startWifi() {
  stopWifi();
  if (wifiSsid[0] == '\0') return;
  Serial.printf("Connecting to WiFi '%s'...\n", wifiSsid);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(wifiSsid, wifiPass);
  wifiOn = true;
}
#endif

// ---- USB configuration commands (sent by the installer) ----

static void handleConfigLine(const char* line) {
  CfgRequest req = parseConfigLine(line);
  switch (req.cmd) {
    case CfgCmd::None:
      return;  // not a command: ignore
    case CfgCmd::Invalid:
      Serial.println("HUDCFG ERR invalid command");
      return;
    case CfgCmd::Info: {
#ifdef WIFI_ENABLED
      const char* state = !wifiOn ? "off" : (WiFi.status() == WL_CONNECTED ? "connected" : "connecting");
      String ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("-");
      Serial.printf("HUDCFG INFO fw=%s wifi=%s ip=%s port=%d\n", HUD_FW_VERSION, state, ip.c_str(), UDP_PORT);
#else
      Serial.printf("HUDCFG INFO fw=%s wifi=unsupported ip=- port=0\n", HUD_FW_VERSION);
#endif
      return;
    }
    case CfgCmd::Wifi:
#ifdef WIFI_ENABLED
      saveWifiSettings(req.ssid, req.pass);
      strncpy(wifiSsid, req.ssid, sizeof(wifiSsid) - 1);
      strncpy(wifiPass, req.pass, sizeof(wifiPass) - 1);
      Serial.println("HUDCFG OK");
      startWifi();
#else
      Serial.println("HUDCFG ERR wifi unsupported");
#endif
      return;
    case CfgCmd::WifiOff:
#ifdef WIFI_ENABLED
      saveWifiSettings("", "");
      wifiSsid[0] = wifiPass[0] = '\0';
      stopWifi();
#endif
      Serial.println("HUDCFG OK");
      return;
  }
}

void handleAttitude(const uint8_t* payload, int len) {
  if (len < (int)sizeof(AttitudeMsg)) return;
  AttitudeMsg msg;
  memcpy(&msg, payload, sizeof(AttitudeMsg));
  gyro.setValue(msg.pitch, msg.roll, msg.heading);
  nav.setHeading(msg.heading);
}

void handleEngine(const uint8_t* payload, int len) {
  if (len < (int)sizeof(EngineMsg)) return;
  EngineMsg msg;
  memcpy(&msg, payload, sizeof(EngineMsg));
  // For now, only display engine 0 on the gauges
  if (msg.engine_idx == 0) {
    engine.setValue(msg.rpm, msg.throttle, msg.fuel_flow, msg.oil_temp, msg.oil_press);
  }
}

void handleFlightData(const uint8_t* payload, int len) {
  if (len < (int)sizeof(FlightDataMsg)) return;
  FlightDataMsg msg;
  memcpy(&msg, payload, sizeof(FlightDataMsg));
  flightData.setValue(msg.airspeed, msg.altitude, msg.vspeed, msg.ground_speed);
}

void handleGForce(const uint8_t* payload, int len) {
  if (len < (int)sizeof(GForceMsg)) return;
  GForceMsg msg;
  memcpy(&msg, payload, sizeof(GForceMsg));
  gforce.setValue(msg.gforce_x, msg.gforce_y, msg.gforce_z);
}

void handleAlerts(const uint8_t* payload, int len) {
  if (len < (int)sizeof(AlertsMsg)) return;
  AlertsMsg msg;
  memcpy(&msg, payload, sizeof(AlertsMsg));
  alert.setFlags(msg.flags);
}

void handleNavData(const uint8_t* payload, int len) {
  if (len < (int)sizeof(NavDataMsg)) return;
  NavDataMsg msg;
  memcpy(&msg, payload, sizeof(NavDataMsg));
  nav.setValue(msg.lat, msg.lon, msg.hdg_bug, msg.wp_dist, msg.wp_bearing);
}

void handleConfig(const uint8_t* payload, int len) {
  if (len < (int)sizeof(ConfigMsg)) return;
  ConfigMsg msg;
  memcpy(&msg, payload, sizeof(ConfigMsg));
  config.setValue(msg.flaps_pct, msg.gear_state, msg.elev_trim, msg.rudder_trim);
}

void handleAutopilot(const uint8_t* payload, int len) {
  if (len < (int)sizeof(AutopilotMsg)) return;
  AutopilotMsg msg;
  memcpy(&msg, payload, sizeof(AutopilotMsg));
  autopilot.setValue(msg.mode_flags, msg.target_alt, msg.target_hdg);
}

void processFrame() {
  uint8_t payload[64];
  int len = decoder.payload(payload, sizeof(payload));
  lastDataMs = millis();
  framesSinceReport++;

  switch (decoder.msgType()) {
    case MSG_ATTITUDE:    handleAttitude(payload, len); break;
    case MSG_ENGINE:      handleEngine(payload, len); break;
    case MSG_FLIGHT_DATA: handleFlightData(payload, len); break;
    case MSG_GFORCE:      handleGForce(payload, len); break;
    case MSG_ALERTS:      handleAlerts(payload, len); break;
    case MSG_NAV_DATA:    handleNavData(payload, len); break;
    case MSG_CONFIG:      handleConfig(payload, len); break;
    case MSG_AUTOPILOT:   handleAutopilot(payload, len); break;
  }
  decoder.clear();
}

void showScreen(int step) {
  currentScreen = (currentScreen + step + NUM_SCREENS) % NUM_SCREENS;
  // Slide in from the side the finger moved towards
  lv_screen_load_anim(screens[currentScreen],
                      step > 0 ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT,
                      180, 0, false);
  pageDots.set(currentScreen);
}

// ---- Commands to the PC (display controls) ----

#ifdef WIFI_ENABLED
IPAddress udpPeer;         // the sender's address, learned from its telemetry
uint16_t udpPeerPort = 0;
uint32_t udpPeerSeenMs = 0;
#endif

// Sent over USB and, while a WiFi sender is active, back to it over UDP
void sendCommand(uint8_t command) {
  CommandMsg msg{command};
  uint8_t frame[8];
  size_t n = encodeFrame(MSG_COMMAND, (const uint8_t*)&msg, sizeof(msg), frame, sizeof(frame));
  if (n == 0) return;
  Serial.write(frame, n);
#ifdef WIFI_ENABLED
  if (udpListening && udpPeerPort != 0 && millis() - udpPeerSeenMs < 5000) {
    udp.beginPacket(udpPeer, udpPeerPort);
    udp.write(frame, n);
    udp.endPacket();
  }
#endif
}

static lv_obj_t* newScreen() {
  lv_obj_t* scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  // A tap cycles screens; never let a drag scroll the layout
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  return scr;
}

void setup() {
  // One telemetry burst must fit while a screen redraw blocks the loop
  Serial.setRxBufferSize(1024);
  Serial.begin(115200);
  Serial.println("Starting HUD...");

  touchInit();

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  backlightInit();  // after the clear, so the panel never flashes garbage

  lv_init();
  lv_tick_set_cb(lvgl_tick_cb);

  // Prefer two DMA buffers; fall back to a single CPU-pushed buffer if the
  // DMA-capable heap is short (e.g. with WiFi and a large canvas).
  uint8_t* buf1 = HUD_USE_DMA ? allocDrawBuf(true) : nullptr;
  uint8_t* buf2 = buf1 ? allocDrawBuf(true) : nullptr;
  if (buf1 && buf2 && tft.initDMA()) {
    useDma = true;
    tft.setSwapBytes(true);  // pushPixelsDMA swaps RGB565 to panel byte order
    tft.startWrite();        // the display owns this SPI bus: keep it claimed
    if (!dmaSelfTest(buf1)) disableDma("self-test failed");
  }
  if (!useDma) {
    if (buf2) { heap_caps_free(buf2); buf2 = nullptr; }
    if (!buf1) buf1 = allocDrawBuf(false);
  }
  Serial.printf("Display: %s, %d-line buffers\n", useDma ? "DMA double-buffered" : "single buffer", DRAW_BUF_LINES);

  lv_display_t * disp = lv_display_create(TFT_HOR_RES, TFT_VER_RES);
  lv_display_set_flush_cb(disp, my_disp_flush);
  lv_display_set_buffers(disp, buf1, buf2, DRAW_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t * indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, my_touchpad_read);

  // Gauges glide between telemetry samples (see Anim.h)
  hud::animate = true;

  // Screens: keep in sync with tools/screenshots/main.cpp
  for (int i = 0; i < NUM_SCREENS; i++) screens[i] = newScreen();

  // ---- Screen 0: MSFS Gyroscope ----
  GyroHorizonConfig gyroCfg;
  gyroCfg.cx = 160;
  gyroCfg.cy = 105;
  gyroCfg.radius = 90;
  gyroCfg.smooth = true;  // ease between telemetry samples (see Smoothing.h)
  gyro.create(screens[0], gyroCfg);

  engine.create(screens[1]);      // Screen 1: MSFS Engine Gauges
  flightData.create(screens[2]);  // Screen 2: MSFS Flight Data
  gforce.create(screens[3]);      // Screen 3: MSFS G-Force Meter
  nav.create(screens[4]);         // Screen 4: MSFS Navigation
  config.create(screens[5]);      // Screen 5: MSFS Aircraft Config
  autopilot.create(screens[6]);   // Screen 6: MSFS Autopilot
  autopilot.setCommandHandler(sendCommand);

  // ---- Alert overlay (on top of all screens) ----
  alert.create(lv_layer_top());

  // ---- "NO DATA" indicator on top layer ----
  lblNoData = lv_label_create(lv_layer_top());
  lv_obj_set_style_text_color(lblNoData, lv_color_make(255, 60, 60), 0);
  lv_obj_set_style_text_font(lblNoData, &lv_font_montserrat_14, 0);
  lv_label_set_text(lblNoData, "NO DATA");
  lv_obj_align(lblNoData, LV_ALIGN_BOTTOM_MID, 0, -5);
  // Start visible until first data arrives

  pageDots.create(lv_layer_top(), NUM_SCREENS);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  lv_screen_load(screens[0]);

#ifdef WIFI_ENABLED
  loadWifiSettings();
  startWifi();
#endif

  // Hardware watchdog: reset device if loop hangs for >5s
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t wdt_cfg = { .timeout_ms = 5000, .idle_core_mask = 0, .trigger_panic = true };
  // The 3.x core starts the task watchdog itself; init only succeeds if it did not
  if (esp_task_wdt_reconfigure(&wdt_cfg) != ESP_OK) esp_task_wdt_init(&wdt_cfg);
#else
  esp_task_wdt_init(5, true);
#endif
  esp_task_wdt_add(NULL);

  lastDataMs = millis();
  Serial.printf("Free heap: %u bytes (largest block %u)\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  Serial.printf("HUD ready (firmware %s). Swipe or press BOOT to change screens. Waiting for data...\n", HUD_FW_VERSION);
}

void loop() {
  esp_task_wdt_reset();
  lv_timer_handler();

  // Feed serial bytes to frame decoder
  // ... and to the line collector, which picks out configuration commands
  while (Serial.available()) {
    uint8_t c = Serial.read();
    decoder.feed(c);
    if (decoder.available()) processFrame();
    if (cmdLine.feed(c)) handleConfigLine(cmdLine.line());
  }

#ifdef WIFI_ENABLED
  if (wifiOn && WiFi.status() == WL_CONNECTED) {
    if (!udpListening) {
      udp.begin(UDP_PORT);
      udpListening = true;
      Serial.printf("WiFi connected. Listening on %s:%d (UDP)\n",
                    WiFi.localIP().toString().c_str(), UDP_PORT);
    }
    // Feed UDP bytes to frame decoder (the sender emits one frame per datagram)
    while (udp.parsePacket() > 0) {
      udpPeer = udp.remoteIP();
      udpPeerPort = udp.remotePort();
      udpPeerSeenMs = millis();
      uint8_t buf[72];
      int len = udp.read(buf, sizeof(buf));
      for (int i = 0; i < len; i++) {
        decoder.feed(buf[i]);
        if (decoder.available()) processFrame();
      }
    }
  } else if (udpListening) {
    udp.stop();
    udpListening = false;
    Serial.println("WiFi lost, reconnecting...");
  }
#endif

  // Animations: horizon and compass easing, alert heartbeat
  gyro.tick(millis());
  nav.tick(millis());
  alert.tick(millis());

  // Telemetry status on the serial log, for diagnosing a silent display
  if (millis() - lastReportMs >= REPORT_MS) {
    if (framesSinceReport) {
      Serial.printf("Telemetry: %u frames in %u s\n", (unsigned)framesSinceReport,
                    (unsigned)((millis() - lastReportMs) / 1000));
    }
    framesSinceReport = 0;
    lastReportMs = millis();
  }

  // "NO DATA" indicator
  if (millis() - lastDataMs > NO_DATA_TIMEOUT_MS) {
    lv_obj_remove_flag(lblNoData, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(lblNoData, LV_OBJ_FLAG_HIDDEN);
  }

  // Screen navigation: swipes (seen by the touch callback) and the BOOT button
  bool bootDown = digitalRead(BOOT_BUTTON_PIN) == LOW;
  if (bootDown != bootWasDown && millis() - bootChangedMs > 40) {  // debounce
    bootChangedMs = millis();
    bootWasDown = bootDown;
    if (bootDown) pendingScreenStep = 1;
  }
  if (pendingScreenStep != 0) {
    showScreen(pendingScreenStep);
    pendingScreenStep = 0;
  }

  yield();
}

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
#include "hud_widgets.h"
#include "hud_proto.h"

#if __has_include("wifi_config.h")
#include "wifi_config.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#define WIFI_ENABLED
#endif

#define TFT_HOR_RES   320
#define TFT_VER_RES   240
#define DRAW_BUF_SIZE (TFT_HOR_RES * TFT_VER_RES / 10 * (LV_COLOR_DEPTH / 8))

#define BACKLIGHT_PIN 27

TFT_eSPI tft = TFT_eSPI();

void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)px_map, w * h, true);
  tft.endWrite();
  lv_display_flush_ready(disp);
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

void my_touchpad_read(lv_indev_t * indev, lv_indev_data_t * data) {
  uint16_t tx, ty;
  if (touchRead(&tx, &ty)) {
    // Panel is mounted in portrait; rotate into the landscape UI
    data->point.x = ty;
    data->point.y = TFT_VER_RES - 1 - tx;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// ---- Mode switching ----

static const int NUM_SCREENS = 7;
lv_obj_t* screens[NUM_SCREENS] = {};
int currentScreen = 0;
bool touchWasPressed = false;
uint32_t lastToggleMs = 0;
static const uint32_t DEBOUNCE_MS = 300;

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
static const uint32_t NO_DATA_TIMEOUT_MS = 2000;
lv_obj_t* lblNoData = nullptr;

#ifdef WIFI_ENABLED
WiFiUDP udp;
bool udpListening = false;
#endif

void handleAttitude(const uint8_t* payload, int len) {
  if (len < (int)sizeof(AttitudeMsg)) return;
  AttitudeMsg msg;
  memcpy(&msg, payload, sizeof(AttitudeMsg));
  gyro.setValue(msg.pitch, msg.roll, msg.heading);
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

void toggleMode() {
  currentScreen = (currentScreen + 1) % NUM_SCREENS;
  if (screens[currentScreen]) {
    lv_screen_load(screens[currentScreen]);
  }
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

  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);
  touchInit();

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  lv_init();
  lv_tick_set_cb(lvgl_tick_cb);

  uint8_t* draw_buf = new uint8_t[DRAW_BUF_SIZE];
  lv_display_t * disp = lv_display_create(TFT_HOR_RES, TFT_VER_RES);
  lv_display_set_flush_cb(disp, my_disp_flush);
  lv_display_set_buffers(disp, draw_buf, NULL, DRAW_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t * indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, my_touchpad_read);

  // Screens: keep in sync with tools/screenshots/main.cpp
  for (int i = 0; i < NUM_SCREENS; i++) screens[i] = newScreen();

  // ---- Screen 0: MSFS Gyroscope ----
  GyroHorizonConfig gyroCfg;
  gyroCfg.cx = 160;
  gyroCfg.cy = 105;
  gyroCfg.radius = 90;
  gyro.create(screens[0], gyroCfg);

  engine.create(screens[1]);      // Screen 1: MSFS Engine Gauges
  flightData.create(screens[2]);  // Screen 2: MSFS Flight Data
  gforce.create(screens[3]);      // Screen 3: MSFS G-Force Meter
  nav.create(screens[4]);         // Screen 4: MSFS Navigation
  config.create(screens[5]);      // Screen 5: MSFS Aircraft Config
  autopilot.create(screens[6]);   // Screen 6: MSFS Autopilot

  // ---- Alert overlay (on top of all screens) ----
  alert.create(lv_layer_top());

  // ---- "NO DATA" indicator on top layer ----
  lblNoData = lv_label_create(lv_layer_top());
  lv_obj_set_style_text_color(lblNoData, lv_color_make(255, 60, 60), 0);
  lv_obj_set_style_text_font(lblNoData, &lv_font_montserrat_14, 0);
  lv_label_set_text(lblNoData, "NO DATA");
  lv_obj_align(lblNoData, LV_ALIGN_BOTTOM_MID, 0, -5);
  // Start visible until first data arrives

  lv_screen_load(screens[0]);

#ifdef WIFI_ENABLED
  // Non-blocking: loop() starts listening once the connection is up,
  // and again after every reconnect.
  Serial.printf("Connecting to WiFi '%s'...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
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
  Serial.println("HUD ready. Touch to cycle screens (7 modes). Waiting for data...");
}

void loop() {
  esp_task_wdt_reset();
  lv_timer_handler();

  // Feed serial bytes to frame decoder
  while (Serial.available()) {
    decoder.feed(Serial.read());
    if (decoder.available()) processFrame();
  }

#ifdef WIFI_ENABLED
  if (WiFi.status() == WL_CONNECTED) {
    if (!udpListening) {
      udp.begin(UDP_PORT);
      udpListening = true;
      Serial.printf("WiFi connected. Listening on %s:%d (UDP)\n",
                    WiFi.localIP().toString().c_str(), UDP_PORT);
    }
    // Feed UDP bytes to frame decoder (the sender emits one frame per datagram)
    while (udp.parsePacket() > 0) {
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

  // Alert heartbeat tick
  alert.tick(millis());

  // "NO DATA" indicator
  if (millis() - lastDataMs > NO_DATA_TIMEOUT_MS) {
    lv_obj_remove_flag(lblNoData, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(lblNoData, LV_OBJ_FLAG_HIDDEN);
  }

  // Touch to toggle mode (rising edge with debounce)
  uint16_t tx, ty;
  bool pressed = touchRead(&tx, &ty);
  if (pressed && !touchWasPressed && (millis() - lastToggleMs > DEBOUNCE_MS)) {
    toggleMode();
    lastToggleMs = millis();
  }
  touchWasPressed = pressed;

  yield();
}

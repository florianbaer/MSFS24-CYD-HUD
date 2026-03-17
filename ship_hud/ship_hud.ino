// MSFS 2024 HUD Display for ESP32-2432S024C (Capacitive touch)
// Receives COBS-framed binary telemetry via USB serial (or WiFi UDP)
// Touch chip: CST816S on I2C
// 7 screens: Gyro, Engine, Flight Data, G-Force, Nav, Config, Autopilot

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

// CST816S I2C touch
#define TOUCH_SDA 33
#define TOUCH_SCL 32
#define TOUCH_INT 21
#define TOUCH_RST 25
#define CST816S_ADDR 0x15

// Hardware timer for LVGL tick (1ms ISR — best practice for accurate animation timing)
static void IRAM_ATTR lvgl_tick_isr() { lv_tick_inc(1); }
hw_timer_t* lvgl_timer = nullptr;

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
    data->point.x = ty;
    data->point.y = TFT_VER_RES - tx;
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
bool wifiConnected = false;
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
    lv_scr_load(screens[currentScreen]);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting HUD...");

  pinMode(27, OUTPUT);
  digitalWrite(27, HIGH);
  touchInit();

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  lv_init();

  // Start 1ms hardware timer for LVGL ticks
  lvgl_timer = timerBegin(1000000); // 1MHz
  timerAttachInterrupt(lvgl_timer, lvgl_tick_isr);
  timerAlarm(lvgl_timer, 1000, true, 0); // 1ms interval, auto-reload

  uint8_t* draw_buf = new uint8_t[DRAW_BUF_SIZE];
  lv_display_t * disp = lv_display_create(TFT_HOR_RES, TFT_VER_RES);
  lv_display_set_flush_cb(disp, my_disp_flush);
  lv_display_set_buffers(disp, draw_buf, NULL, DRAW_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t * indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, my_touchpad_read);

  // ---- Screen 0: MSFS Gyroscope ----
  screens[0] = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screens[0], lv_color_black(), 0);
  GyroHorizonConfig gyroCfg;
  gyroCfg.cx = 160;
  gyroCfg.cy = 105;
  gyroCfg.radius = 90;
  gyro.create(screens[0], gyroCfg);

  // ---- Screen 1: MSFS Engine Gauges ----
  screens[1] = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screens[1], lv_color_black(), 0);
  engine.create(screens[1]);

  // ---- Screen 2: MSFS Flight Data ----
  screens[2] = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screens[2], lv_color_black(), 0);
  flightData.create(screens[2]);

  // ---- Screen 3: MSFS G-Force Meter ----
  screens[3] = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screens[3], lv_color_black(), 0);
  gforce.create(screens[3]);

  // ---- Screen 4: MSFS Navigation ----
  screens[4] = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screens[4], lv_color_black(), 0);
  nav.create(screens[4]);

  // ---- Screen 5: MSFS Aircraft Config ----
  screens[5] = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screens[5], lv_color_black(), 0);
  config.create(screens[5]);

  // ---- Screen 6: MSFS Autopilot ----
  screens[6] = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screens[6], lv_color_black(), 0);
  autopilot.create(screens[6]);

  // ---- Alert overlay (on top of all screens) ----
  alert.create(lv_layer_top());

  // ---- "NO DATA" indicator on top layer ----
  lblNoData = lv_label_create(lv_layer_top());
  lv_obj_set_style_text_color(lblNoData, lv_color_make(255, 60, 60), 0);
  lv_obj_set_style_text_font(lblNoData, &lv_font_montserrat_14, 0);
  lv_label_set_text(lblNoData, "NO DATA");
  lv_obj_align(lblNoData, LV_ALIGN_BOTTOM_MID, 0, -5);
  // Start visible until first data arrives

  lv_scr_load(screens[0]);

#ifdef WIFI_ENABLED
  Serial.printf("Connecting to WiFi '%s'...\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());
    udp.begin(UDP_PORT);
    Serial.printf("UDP listening on port %d\n", UDP_PORT);
    wifiConnected = true;
  } else {
    Serial.println("\nWiFi connection failed. Using serial only.");
  }
#endif

  // Hardware watchdog: reset device if loop hangs for >5s
  esp_task_wdt_config_t wdt_cfg = { .timeout_ms = 5000, .idle_core_mask = 0, .trigger_panic = true };
  esp_task_wdt_init(&wdt_cfg);
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
  if (wifiConnected) {
    // Feed UDP bytes to frame decoder
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
      uint8_t buf[72];
      int len = udp.read(buf, sizeof(buf));
      for (int i = 0; i < len; i++) {
        decoder.feed(buf[i]);
        if (decoder.available()) processFrame();
      }
    }
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

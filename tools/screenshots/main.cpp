// Renders every HUD screen to a PNG on a desktop machine.
//
// The widgets are the real headers from lib/hud_widgets, drawn by LVGL with the
// project's lv_conf.h into a 320x240 RGB565 framebuffer -- the same pixels the
// ESP32 pushes to the panel. Only the display driver and Arduino.h are stubbed.

#include <lvgl.h>
#include <zlib.h>

#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "hud_widgets.h"

static const int W = 320;
static const int H = 240;

static uint16_t framebuffer[W * H];
static uint32_t fakeMs = 0;

static uint32_t tickCb() { return fakeMs; }

static void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  const uint16_t* src = (const uint16_t*)px_map;
  for (int y = area->y1; y <= area->y2; y++) {
    for (int x = area->x1; x <= area->x2; x++) {
      framebuffer[y * W + x] = *src++;
    }
  }
  lv_display_flush_ready(disp);
}

// ---- PNG output ----

static void put32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}

static void writeChunk(FILE* f, const char* type, const std::vector<uint8_t>& data) {
  std::vector<uint8_t> out;
  put32(out, (uint32_t)data.size());
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  put32(out, (uint32_t)crc32(0, out.data() + 4, (uInt)(out.size() - 4)));
  fwrite(out.data(), 1, out.size(), f);
}

/// Write the framebuffer as an RGB PNG, enlarged `scale` times (nearest neighbour).
static bool writePng(const std::string& path, int scale) {
  const int ow = W * scale, oh = H * scale;
  std::vector<uint8_t> raw;
  raw.reserve((size_t)oh * (ow * 3 + 1));
  for (int y = 0; y < oh; y++) {
    raw.push_back(0); // filter: none
    for (int x = 0; x < ow; x++) {
      uint16_t p = framebuffer[(y / scale) * W + (x / scale)];
      uint8_t r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
      raw.push_back((r << 3) | (r >> 2));
      raw.push_back((g << 2) | (g >> 4));
      raw.push_back((b << 3) | (b >> 2));
    }
  }

  uLongf zlen = compressBound((uLong)raw.size());
  std::vector<uint8_t> z(zlen);
  if (compress2(z.data(), &zlen, raw.data(), (uLong)raw.size(), 9) != Z_OK) return false;
  z.resize(zlen);

  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return false;
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  fwrite(sig, 1, sizeof(sig), f);

  std::vector<uint8_t> ihdr;
  put32(ihdr, ow); put32(ihdr, oh);
  ihdr.push_back(8); // bit depth
  ihdr.push_back(2); // colour type: RGB
  ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
  writeChunk(f, "IHDR", ihdr);
  writeChunk(f, "IDAT", z);
  writeChunk(f, "IEND", {});
  fclose(f);
  return true;
}

// ---- Scene ----

static GyroHorizon gyro;
static EngineGauges engine;
static FlightData flightData;
static GForceMeter gforce;
static NavDisplay nav;
static AircraftConfig config;
static AutopilotStatus autopilot;
static AlertIndicator alert;

static PageIndicator pageDots;
static std::string outDir = ".";
static int failures = 0;

static lv_obj_t* newScreen() {
  lv_obj_t* scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  return scr;
}

static void shot(const char* name, lv_obj_t* screen, int index) {
  lv_screen_load(screen);
  pageDots.set(index);
  lv_refr_now(NULL);
  std::string path = outDir + "/" + name + ".png";
  if (writePng(path, 2)) {
    printf("wrote %s\n", path.c_str());
  } else {
    fprintf(stderr, "failed to write %s\n", path.c_str());
    failures++;
  }
}

int main(int argc, char** argv) {
  if (argc > 1) outDir = argv[1];

  lv_init();
  lv_tick_set_cb(tickCb);

  // Same partial-render setup as ship_hud.ino
  static uint8_t drawBuf[W * H / 10 * 2];
  lv_display_t* disp = lv_display_create(W, H);
  lv_display_set_flush_cb(disp, flushCb);
  lv_display_set_buffers(disp, drawBuf, NULL, sizeof(drawBuf), LV_DISPLAY_RENDER_MODE_PARTIAL);

  // Screens: keep in sync with setup() in ship_hud/ship_hud.ino
  lv_obj_t* screens[7];
  for (auto& s : screens) s = newScreen();

  GyroHorizonConfig gyroCfg;
  gyroCfg.cx = 160;
  gyroCfg.cy = 105;
  gyroCfg.radius = 90;
  gyro.create(screens[0], gyroCfg);
  engine.create(screens[1]);
  flightData.create(screens[2]);
  gforce.create(screens[3]);
  nav.create(screens[4]);
  config.create(screens[5]);
  autopilot.create(screens[6]);

  AlertIndicatorConfig alertCfg;
  alertCfg.ledPin = -1;
  alert.create(lv_layer_top(), alertCfg);
  pageDots.create(lv_layer_top(), 7);  // as in the sketch

  // Sample telemetry: a light single in a climbing left turn out of Zurich.
  // Values are in wire units, exactly as the sender would deliver them.
  gyro.setValue(65, 180, 2740);                    // pitch +6.5, roll 18 left, HDG 274
  nav.setHeading(2740);                            // the sketch forwards attitude heading too
  engine.setValue(2380, 88, 47, 187, 158);         // 9.2 GPH, 183 F, 62 PSI
  flightData.setValue(1124, 4520, 650, 1187);      // 112.4 KIAS, 4520 ft, +650 fpm
  gforce.setValue(3, 62, 4);                       // earlier push-over ...
  gforce.setValue(-5, 186, -9);                    // ... and pull-up, for the peaks
  gforce.setValue(4, 112, -6);
  nav.setValue(474502000, 85618000, 2700, 124, 2680);
  config.setValue(25, 2, 12, -4);
  autopilot.setValue(0x01 | 0x02 | 0x04, 6000, 2700);

  static const char* NAMES[7] = {
    "gyro", "engine", "flight-data", "g-force", "nav", "config", "autopilot",
  };
  for (int i = 0; i < 7; i++) shot(NAMES[i], screens[i], i);

  // Alert overlay: stall warning on top of the flight data screen
  alert.setFlags(0x0001);
  fakeMs += 1000;
  alert.tick(fakeMs);
  shot("alert", screens[2], 2);

  lv_mem_monitor_t mon;
  lv_mem_monitor(&mon);
  printf("LVGL heap: %u of %u bytes used (peak %u)\n",
         (unsigned)(mon.total_size - mon.free_size), (unsigned)mon.total_size,
         (unsigned)mon.max_used);

  return failures ? 1 : 0;
}

// Touch controls driven through LVGL with a scripted finger: the real widgets,
// the real swipe/tap logic (TouchInput.h), LVGL's own event handling.
//
//   make -C tools/screenshots touch-test

#include <lvgl.h>

#include <cstdio>
#include <vector>

#include "hud_widgets.h"

static int failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
      failures++;                                                       \
    }                                                                   \
  } while (0)

static uint32_t fakeMs = 0;
static uint32_t tickCb() { return fakeMs; }
static void flushCb(lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); }

// The scripted finger, read by LVGL through the same feedTouch() the sketch uses
static bool fingerDown = false;
static int fingerX = 0, fingerY = 0;
static SwipeDetector swipe;
static int screenStep = 0;
static void readCb(lv_indev_t* indev, lv_indev_data_t* data) {
  int step = feedTouch(swipe, indev, data, fingerDown, fingerX, fingerY);
  if (step) screenStep = step;
}

static std::vector<uint8_t> commands;
static void onCommand(uint8_t c) { commands.push_back(c); }

static void run(uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 5) {
    fakeMs += 5;
    lv_timer_handler();
  }
}

static void tap(int x, int y, uint32_t holdMs = 80) {
  fingerX = x; fingerY = y; fingerDown = true;
  run(holdMs);
  fingerDown = false;
  run(60);
}

static void drag(int x0, int y0, int x1, int y1) {
  fingerX = x0; fingerY = y0; fingerDown = true;
  run(40);
  for (int i = 1; i <= 10; i++) {
    fingerX = x0 + (x1 - x0) * i / 10;
    fingerY = y0 + (y1 - y0) * i / 10;
    run(20);
  }
  fingerDown = false;
  run(60);
}

static size_t count(uint8_t c) {
  size_t n = 0;
  for (auto x : commands) n += x == c;
  return n;
}

int main() {
  lv_init();
  lv_tick_set_cb(tickCb);
  static uint8_t buf[320 * 24 * 2];
  lv_display_t* disp = lv_display_create(320, 240);
  lv_display_set_flush_cb(disp, flushCb);
  lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, readCb);

  // ---- autopilot screen ----
  lv_obj_t* apScreen = lv_obj_create(NULL);
  lv_obj_remove_flag(apScreen, LV_OBJ_FLAG_SCROLLABLE);
  AutopilotStatus ap;
  ap.create(apScreen);
  ap.setCommandHandler(onCommand);
  ap.setValue(0x01, 6000, 2700);
  lv_screen_load(apScreen);
  run(50);

  tap(82, 40);                  // AP master tile
  CHECK(commands.size() == 1 && commands[0] == CMD_AP_MASTER);
  commands.clear();

  tap(12 + 0 * 60 + 28, 83);    // HDG
  tap(12 + 1 * 60 + 28, 83);    // ALT
  tap(12 + 2 * 60 + 28, 83);    // VS
  tap(12 + 3 * 60 + 28, 83);    // NAV
  tap(12 + 4 * 60 + 28, 83);    // APR
  CHECK((commands == std::vector<uint8_t>{CMD_AP_HDG, CMD_AP_ALT, CMD_AP_VS, CMD_AP_NAV, CMD_AP_APR}));
  commands.clear();

  tap(282, 138);                // ALT +
  tap(222, 138);                // ALT -
  tap(282, 191);                // HDG +
  tap(222, 191);                // HDG -
  CHECK((commands == std::vector<uint8_t>{CMD_ALT_INC, CMD_ALT_DEC, CMD_HDG_INC, CMD_HDG_DEC}));
  commands.clear();

  tap(222, 191, 1500);          // hold HDG - for 1.5 s: repeats
  CHECK(count(CMD_HDG_DEC) >= 8);
  CHECK(commands.size() == count(CMD_HDG_DEC));
  commands.clear();

  tap(60, 225);                 // empty space: nothing
  CHECK(commands.empty());

  // A swipe that starts on a tile changes the screen and does not press the tile
  screenStep = 0;
  drag(12 + 2 * 60 + 28, 83, 12 + 2 * 60 + 28 - 140, 90);
  CHECK(commands.empty());
  CHECK(screenStep == 1);       // finger moved left: next screen
  screenStep = 0;
  drag(40, 150, 220, 155);
  CHECK(screenStep == -1);      // finger moved right: previous screen
  screenStep = 0;
  drag(150, 60, 160, 200);      // mostly vertical: not a swipe
  CHECK(screenStep == 0);
  commands.clear();

  // ---- G-force screen: tap the peaks to reset them ----
  lv_obj_t* gScreen = lv_obj_create(NULL);
  lv_obj_remove_flag(gScreen, LV_OBJ_FLAG_SCROLLABLE);
  GForceMeter g;
  g.create(gScreen);
  g.setValue(0, 350, 0);        // pull 3.5 G ...
  g.setValue(0, -50, 0);        // ... then push -0.5 G
  g.setValue(0, 110, 0);        // now 1.1 G
  lv_screen_load(gScreen);
  run(50);
  CHECK(g.peakPositive() == 350 && g.peakNegative() == -50);
  tap(250, 120);
  CHECK(g.peakPositive() == 110 && g.peakNegative() == 110);

  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("touch controls: all tests passed\n");
  return 0;
}

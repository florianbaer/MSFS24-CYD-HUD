#pragma once
#include <lvgl.h>
#include <stdlib.h>

/// Tells swipes from taps.
///
/// A horizontal movement of at least MIN_DISTANCE pixels (and clearly more
/// horizontal than vertical) is a swipe; anything else is left to LVGL as a
/// tap on whatever control is under the finger.
class SwipeDetector {
public:
  static const int MIN_DISTANCE = 50;

  enum Direction { NONE = 0, LEFT = 1, RIGHT = 2 };

  /// Finger down or moving at (x, y).
  void touch(int x, int y) {
    if (!_down) {
      _down = true;
      _x0 = x;
      _y0 = y;
      _swiping = false;
    }
    int dx = x - _x0, dy = y - _y0;
    if (!_swiping && abs(dx) >= MIN_DISTANCE && abs(dx) > 2 * abs(dy)) {
      _swiping = true;
      _dir = dx < 0 ? LEFT : RIGHT;
    }
  }

  /// True from the moment the movement became a swipe until the finger lifts.
  bool swiping() const { return _swiping; }

  /// Finger lifted: the swipe direction, or NONE for a tap.
  Direction release() {
    Direction d = (_down && _swiping) ? _dir : NONE;
    _down = false;
    _swiping = false;
    return d;
  }

private:
  bool _down = false;
  bool _swiping = false;
  int _x0 = 0, _y0 = 0;
  Direction _dir = NONE;
};

/// Feeds one touch sample to LVGL and the swipe detector.
///
/// Call from the LVGL pointer read callback. Taps reach LVGL as usual; once a
/// movement turns into a swipe, LVGL is told to ignore the rest of that touch
/// so the control under the finger is not clicked. Returns the screen step
/// when a swipe ends: +1 for a swipe to the left (next screen), -1 for a
/// swipe to the right (previous screen), 0 otherwise.
inline int feedTouch(SwipeDetector& swipe, lv_indev_t* indev, lv_indev_data_t* data,
                     bool pressed, int x, int y) {
  if (pressed) {
    bool was = swipe.swiping();
    swipe.touch(x, y);
    if (!was && swipe.swiping()) lv_indev_wait_release(indev);
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
    return 0;
  }
  data->state = LV_INDEV_STATE_RELEASED;
  switch (swipe.release()) {
    case SwipeDetector::LEFT: return 1;
    case SwipeDetector::RIGHT: return -1;
    default: return 0;
  }
}

/// A row of dots showing which screen is shown, for swipe navigation.
class PageIndicator {
public:
  void create(lv_obj_t* parent, int count) {
    _count = count > MAX ? MAX : count;
    const int size = 5, gap = 4;
    int x = 320 - 6 - _count * (size + gap) + gap;
    for (int i = 0; i < _count; i++) {
      _dots[i] = lv_obj_create(parent);
      lv_obj_remove_style_all(_dots[i]);
      lv_obj_remove_flag(_dots[i], LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_size(_dots[i], size, size);
      lv_obj_set_pos(_dots[i], x + i * (size + gap), 5);
      lv_obj_set_style_radius(_dots[i], LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_opa(_dots[i], LV_OPA_COVER, 0);
    }
    set(0);
  }

  void set(int active) {
    for (int i = 0; i < _count; i++) {
      lv_obj_set_style_bg_color(_dots[i], i == active ? lv_color_white() : lv_color_make(70, 70, 70), 0);
    }
  }

private:
  static const int MAX = 10;
  lv_obj_t* _dots[MAX] = {};
  int _count = 0;
};

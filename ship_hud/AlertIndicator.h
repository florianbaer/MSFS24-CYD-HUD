#pragma once
#include <lvgl.h>
#include <Arduino.h>

struct AlertIndicatorConfig {
  int y = 0;             // offset from the top edge; horizontally centred
  uint32_t blinkMs = 500;
  int ledPin = 4;        // red LED pin (-1 to disable)
  uint8_t ledChannel = 0; // only used by arduino-esp32 2.x
};

/// Priority-based alert overlay. Shows highest-priority active alert.
/// Create on lv_layer_top() to overlay all screens.
class AlertIndicator {
public:
  AlertIndicator()
    : _lbl(nullptr), _flags(0), _prevFlags(0), _visible(false), _lastBlink(0),
      _seqActive(false), _beatIndex(0), _stepIndex(0), _stepTime(0) {}

  void create(lv_obj_t* parent, const AlertIndicatorConfig& cfg = {}) {
    _cfg = cfg;
    _lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(_lbl, &lv_font_montserrat_14, 0);
    lv_label_set_text(_lbl, "");
    // Opaque backdrop: the alert sits on top of the screen titles
    lv_obj_set_style_bg_color(_lbl, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(_lbl, 8, 0);
    lv_obj_set_style_pad_ver(_lbl, 2, 0);
    lv_obj_align(_lbl, LV_ALIGN_TOP_MID, 0, cfg.y);
    lv_obj_add_flag(_lbl, LV_OBJ_FLAG_HIDDEN);

    if (cfg.ledPin >= 0) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
      ledcAttach(cfg.ledPin, 5000, 8);
#else
      ledcSetup(cfg.ledChannel, 5000, 8);
      ledcAttachPin(cfg.ledPin, cfg.ledChannel);
#endif
      ledSet(255); // off (active low)
    }
  }

  /// Set alert flags bitfield. Highest-priority active alert is displayed.
  void setFlags(uint16_t flags) {
    _flags = flags;
    if (!flags) {
      lv_obj_add_flag(_lbl, LV_OBJ_FLAG_HIDDEN);
      _visible = false;
    } else {
      // Update symbol and color for highest-priority alert
      for (int i = 0; i < NUM_ALERTS; i++) {
        if (flags & PRIORITY[i].bit) {
          lv_label_set_text(_lbl, PRIORITY[i].text);
          lv_obj_set_style_text_color(_lbl, lv_color_make(PRIORITY[i].r, PRIORITY[i].g, PRIORITY[i].b), 0);
          break;
        }
      }
    }
  }

  void tick(uint32_t now) {
    // Trigger heartbeat whenever an alert that was not active before comes on
    if (_flags & ~_prevFlags) {
      _seqActive = true;
      _beatIndex = 0;
      _stepIndex = 0;
      _stepTime = now;
      ledSet(STEPS[0].duty);
    }
    _prevFlags = _flags;

    // Run heartbeat LED sequence
    if (_seqActive && _cfg.ledPin >= 0) {
      uint32_t elapsed = now - _stepTime;
      if (_stepIndex < NUM_STEPS) {
        if (elapsed >= STEPS[_stepIndex].ms) {
          _stepTime = now;
          _stepIndex++;
          if (_stepIndex < NUM_STEPS)
            ledSet(STEPS[_stepIndex].duty);
          else
            ledSet(255);
        }
      } else {
        if (elapsed >= 1000) {
          _beatIndex++;
          if (_beatIndex < 3) {
            _stepIndex = 0;
            _stepTime = now;
            ledSet(STEPS[0].duty);
          } else {
            _seqActive = false;
          }
        }
      }
    }

    // Blink warning text
    if (!_flags) return;
    if (now - _lastBlink >= _cfg.blinkMs) {
      _lastBlink = now;
      _visible = !_visible;
      if (_visible)
        lv_obj_remove_flag(_lbl, LV_OBJ_FLAG_HIDDEN);
      else
        lv_obj_add_flag(_lbl, LV_OBJ_FLAG_HIDDEN);
    }
  }

private:
  struct AlertDef {
    uint16_t bit;
    const char* text;
    uint8_t r, g, b;
  };

  // Priority order: highest first
  static constexpr AlertDef PRIORITY[] = {
    { 0x0010, LV_SYMBOL_WARNING " FIRE",   255,  40,  40 },  // engine_fire
    { 0x0001, LV_SYMBOL_WARNING " STALL",  255,  40,  40 },  // stall
    { 0x0002, LV_SYMBOL_WARNING " OVSPD",  255, 200,   0 },  // overspeed
    { 0x0004, LV_SYMBOL_WARNING " GEAR",   255, 200,   0 },  // gear_unsafe
    { 0x0008, LV_SYMBOL_WARNING " FUEL",   255, 160,   0 },  // low_fuel
    { 0x0020, "A/P DISC",                  255, 160,   0 },  // ap_disconnect
  };
  static const int NUM_ALERTS = 6;

  struct Step {
    uint8_t duty;
    uint32_t ms;
  };
  static constexpr Step STEPS[] = {
    { 25, 60},   // 90% — lub
    {230, 40},   // 10% — gap
    { 77, 60},   // 70% — dub
    {153, 50},   // 40% — fade
    {217, 50},   // 15% — fade
    {255, 50},   // off
  };
  static const int NUM_STEPS = 6;

  AlertIndicatorConfig _cfg;
  lv_obj_t* _lbl;
  uint16_t _flags;
  uint16_t _prevFlags;
  bool _visible;
  uint32_t _lastBlink;
  bool _seqActive;
  int _beatIndex;
  int _stepIndex;
  uint32_t _stepTime;

  void ledSet(uint8_t duty) {
    if (_cfg.ledPin < 0) return;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(_cfg.ledPin, duty);
#else
    ledcWrite(_cfg.ledChannel, duty);
#endif
  }
};

constexpr AlertIndicator::AlertDef AlertIndicator::PRIORITY[];
constexpr AlertIndicator::Step AlertIndicator::STEPS[];

#pragma once
#include <lvgl.h>
#include <stdio.h>
#include "hud_proto.h"

/// Aircraft configuration screen: flaps, gear, trim indicators.
class AircraftConfig {
public:
  AircraftConfig() = default;

  void create(lv_obj_t* parent) {
    // Title
    lv_obj_t* title = lv_label_create(parent);
    lv_obj_set_style_text_color(title, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_10, 0);
    lv_label_set_text(title, "MSFS CONFIG");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);

    // ---- Flaps bar (left side) ----
    lv_obj_t* flapTitle = lv_label_create(parent);
    lv_obj_set_style_text_color(flapTitle, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(flapTitle, &lv_font_montserrat_10, 0);
    lv_label_set_text(flapTitle, "FLAPS");
    lv_obj_set_pos(flapTitle, 20, 22);

    _barFlaps = lv_bar_create(parent);
    lv_obj_set_size(_barFlaps, 20, 150);
    lv_obj_set_pos(_barFlaps, 25, 36);
    lv_bar_set_range(_barFlaps, 0, 100);
    lv_bar_set_value(_barFlaps, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(_barFlaps, lv_color_make(40, 40, 40), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_barFlaps, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_barFlaps, lv_color_make(0, 200, 0), LV_PART_INDICATOR);
    lv_obj_set_style_radius(_barFlaps, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(_barFlaps, 4, LV_PART_INDICATOR);

    _lblFlaps = lv_label_create(parent);
    lv_obj_set_style_text_color(_lblFlaps, lv_color_make(0, 200, 0), 0);
    lv_obj_set_style_text_font(_lblFlaps, &lv_font_montserrat_14, 0);
    lv_label_set_text(_lblFlaps, "0%");
    lv_obj_set_pos(_lblFlaps, 15, 190);

    // ---- Gear indicator (center) ----
    lv_obj_t* gearTitle = lv_label_create(parent);
    lv_obj_set_style_text_color(gearTitle, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(gearTitle, &lv_font_montserrat_10, 0);
    lv_label_set_text(gearTitle, "GEAR");
    lv_obj_set_pos(gearTitle, 100, 22);

    _lblGear = lv_label_create(parent);
    lv_obj_set_style_text_font(_lblGear, &lv_font_montserrat_28, 0);
    lv_label_set_text(_lblGear, "UP");
    lv_obj_set_style_text_color(_lblGear, lv_color_make(100, 100, 100), 0);
    lv_obj_set_pos(_lblGear, 80, 50);

    // ---- Elevator trim (right side, vertical bar) ----
    lv_obj_t* eTrimTitle = lv_label_create(parent);
    lv_obj_set_style_text_color(eTrimTitle, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(eTrimTitle, &lv_font_montserrat_10, 0);
    lv_label_set_text(eTrimTitle, "ELEV TRIM");
    lv_obj_set_pos(eTrimTitle, 250, 22);

    _barElevTrim = lv_bar_create(parent);
    lv_obj_set_size(_barElevTrim, 16, 120);
    lv_obj_set_pos(_barElevTrim, 270, 36);
    lv_bar_set_range(_barElevTrim, -100, 100);
    lv_bar_set_mode(_barElevTrim, LV_BAR_MODE_SYMMETRICAL); // grow from neutral
    lv_bar_set_value(_barElevTrim, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(_barElevTrim, lv_color_make(40, 40, 40), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_barElevTrim, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_barElevTrim, lv_color_make(0, 180, 255), LV_PART_INDICATOR);
    lv_obj_set_style_radius(_barElevTrim, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(_barElevTrim, 3, LV_PART_INDICATOR);

    _lblElevTrim = lv_label_create(parent);
    lv_obj_set_style_text_color(_lblElevTrim, lv_color_make(0, 180, 255), 0);
    lv_obj_set_style_text_font(_lblElevTrim, &lv_font_montserrat_12, 0);
    lv_label_set_text(_lblElevTrim, "0");
    lv_obj_set_pos(_lblElevTrim, 268, 160);

    // ---- Rudder trim (bottom, horizontal bar) ----
    lv_obj_t* rTrimTitle = lv_label_create(parent);
    lv_obj_set_style_text_color(rTrimTitle, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(rTrimTitle, &lv_font_montserrat_10, 0);
    lv_label_set_text(rTrimTitle, "RUDDER TRIM");
    lv_obj_set_pos(rTrimTitle, 80, 115);

    _barRudderTrim = lv_bar_create(parent);
    lv_obj_set_size(_barRudderTrim, 140, 14);
    lv_obj_set_pos(_barRudderTrim, 70, 130);
    lv_bar_set_range(_barRudderTrim, -100, 100);
    lv_bar_set_mode(_barRudderTrim, LV_BAR_MODE_SYMMETRICAL); // grow from neutral
    lv_bar_set_value(_barRudderTrim, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(_barRudderTrim, lv_color_make(40, 40, 40), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_barRudderTrim, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_barRudderTrim, lv_color_make(200, 200, 0), LV_PART_INDICATOR);
    lv_obj_set_style_radius(_barRudderTrim, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(_barRudderTrim, 3, LV_PART_INDICATOR);

    _lblRudderTrim = lv_label_create(parent);
    lv_obj_set_style_text_color(_lblRudderTrim, lv_color_make(200, 200, 0), 0);
    lv_obj_set_style_text_font(_lblRudderTrim, &lv_font_montserrat_12, 0);
    lv_label_set_text(_lblRudderTrim, "0");
    lv_obj_set_pos(_lblRudderTrim, 216, 129);
  }

  void setValue(uint8_t flapsPct, uint8_t gearState, int8_t elevTrim, int8_t rudderTrim) {
    if (!_barFlaps) return;
    if (flapsPct == _prevFlaps && gearState == _prevGear
        && elevTrim == _prevElevTrim && rudderTrim == _prevRudderTrim) return;
    _prevFlaps = flapsPct; _prevGear = gearState;
    _prevElevTrim = elevTrim; _prevRudderTrim = rudderTrim;

    char buf[16];

    // Flaps
    lv_bar_set_value(_barFlaps, flapsPct, LV_ANIM_OFF);
    lv_color_t flapColor = flapsPct > 75 ? lv_color_make(255, 200, 0) :
                           flapsPct > 0  ? lv_color_make(0, 200, 0) :
                                           lv_color_make(100, 100, 100);
    lv_obj_set_style_bg_color(_barFlaps, flapColor, LV_PART_INDICATOR);
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)flapsPct);
    lv_label_set_text(_lblFlaps, buf);
    lv_obj_set_style_text_color(_lblFlaps, flapColor, 0);

    // Gear
    switch (gearState) {
      case 0:
        lv_label_set_text(_lblGear, "UP");
        lv_obj_set_style_text_color(_lblGear, lv_color_make(100, 100, 100), 0);
        break;
      case 1:
        lv_label_set_text(_lblGear, "TRANSIT");
        lv_obj_set_style_text_color(_lblGear, lv_color_make(255, 40, 40), 0);
        break;
      case 2:
        lv_label_set_text(_lblGear, "DOWN");
        lv_obj_set_style_text_color(_lblGear, lv_color_make(0, 200, 0), 0);
        break;
    }

    // Elevator trim
    lv_bar_set_value(_barElevTrim, elevTrim, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "%d", (int)elevTrim);
    lv_label_set_text(_lblElevTrim, buf);

    // Rudder trim
    lv_bar_set_value(_barRudderTrim, rudderTrim, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "%d", (int)rudderTrim);
    lv_label_set_text(_lblRudderTrim, buf);
  }

private:
  uint8_t _prevFlaps = 0xFF, _prevGear = 0xFF;
  int8_t _prevElevTrim = INT8_MIN, _prevRudderTrim = INT8_MIN;
  lv_obj_t* _barFlaps = nullptr;
  lv_obj_t* _lblFlaps = nullptr;
  lv_obj_t* _lblGear = nullptr;
  lv_obj_t* _barElevTrim = nullptr;
  lv_obj_t* _lblElevTrim = nullptr;
  lv_obj_t* _barRudderTrim = nullptr;
  lv_obj_t* _lblRudderTrim = nullptr;
};

#pragma once
#include <lvgl.h>
#include <stdio.h>
#include "hud_proto.h"

/// Autopilot status screen: master switch, mode annunciators, targets.
class AutopilotStatus {
public:
  AutopilotStatus() = default;

  void create(lv_obj_t* parent) {
    // Title
    lv_obj_t* title = lv_label_create(parent);
    lv_obj_set_style_text_color(title, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_10, 0);
    lv_label_set_text(title, "MSFS AUTOPILOT");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);

    // AP Master (large indicator)
    _lblMaster = lv_label_create(parent);
    lv_obj_set_style_text_font(_lblMaster, &lv_font_montserrat_28, 0);
    lv_label_set_text(_lblMaster, "AP OFF");
    lv_obj_set_style_text_color(_lblMaster, lv_color_make(100, 100, 100), 0);
    lv_obj_set_pos(_lblMaster, 15, 22);

    // Mode annunciators (row)
    static const char* MODE_NAMES[] = { "HDG", "ALT", "VS", "NAV", "APR" };
    for (int i = 0; i < 5; i++) {
      _lblModes[i] = lv_label_create(parent);
      lv_obj_set_style_text_font(_lblModes[i], &lv_font_montserrat_18, 0);
      lv_obj_set_style_text_color(_lblModes[i], lv_color_make(60, 60, 60), 0);
      lv_label_set_text(_lblModes[i], MODE_NAMES[i]);
      lv_obj_set_pos(_lblModes[i], 15 + i * 60, 65);
    }

    // Target altitude
    lv_obj_t* altTitle = lv_label_create(parent);
    lv_obj_set_style_text_color(altTitle, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(altTitle, &lv_font_montserrat_10, 0);
    lv_label_set_text(altTitle, "TARGET ALT");
    lv_obj_set_pos(altTitle, 15, 105);

    _lblTargetAlt = lv_label_create(parent);
    lv_obj_set_style_text_color(_lblTargetAlt, lv_color_make(0, 220, 0), 0);
    lv_obj_set_style_text_font(_lblTargetAlt, &lv_font_montserrat_28, 0);
    lv_label_set_text(_lblTargetAlt, "-----");
    lv_obj_set_pos(_lblTargetAlt, 15, 118);

    lv_obj_t* ftLabel = lv_label_create(parent);
    lv_obj_set_style_text_color(ftLabel, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(ftLabel, &lv_font_montserrat_10, 0);
    lv_label_set_text(ftLabel, "FT");
    lv_obj_set_pos(ftLabel, 112, 134);

    // Target heading
    lv_obj_t* hdgTitle = lv_label_create(parent);
    lv_obj_set_style_text_color(hdgTitle, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(hdgTitle, &lv_font_montserrat_10, 0);
    lv_label_set_text(hdgTitle, "TARGET HDG");
    lv_obj_set_pos(hdgTitle, 15, 160);

    _lblTargetHdg = lv_label_create(parent);
    lv_obj_set_style_text_color(_lblTargetHdg, lv_color_make(0, 180, 255), 0);
    lv_obj_set_style_text_font(_lblTargetHdg, &lv_font_montserrat_28, 0);
    lv_label_set_text(_lblTargetHdg, "---");
    lv_obj_set_pos(_lblTargetHdg, 15, 173);
  }

  void setValue(uint16_t modeFlags, int32_t targetAlt, int16_t targetHdg) {
    if (!_lblMaster) return;
    if (modeFlags == _prevFlags && targetAlt == _prevAlt && targetHdg == _prevHdg) return;
    _prevFlags = modeFlags; _prevAlt = targetAlt; _prevHdg = targetHdg;

    char buf[16];

    // AP Master
    bool master = modeFlags & 0x01;
    lv_label_set_text(_lblMaster, master ? "AP ON" : "AP OFF");
    lv_obj_set_style_text_color(_lblMaster,
      master ? lv_color_make(0, 220, 0) : lv_color_make(100, 100, 100), 0);

    // Mode annunciators: bits 1-5 map to HDG, ALT, VS, NAV, APR
    for (int i = 0; i < 5; i++) {
      bool active = modeFlags & (1 << (i + 1));
      lv_obj_set_style_text_color(_lblModes[i],
        active ? lv_color_make(0, 220, 0) : lv_color_make(60, 60, 60), 0);
    }

    // Target altitude
    if (modeFlags & 0x04) { // ALT lock active
      snprintf(buf, sizeof(buf), "%ld", (long)targetAlt);
      lv_label_set_text(_lblTargetAlt, buf);
    } else {
      lv_label_set_text(_lblTargetAlt, "-----");
    }

    // Target heading (tenths → degrees)
    if (modeFlags & 0x02) { // HDG lock active
      snprintf(buf, sizeof(buf), "%03d", targetHdg / 10);
      lv_label_set_text(_lblTargetHdg, buf);
    } else {
      lv_label_set_text(_lblTargetHdg, "---");
    }
  }

private:
  uint16_t _prevFlags = UINT16_MAX;
  int32_t _prevAlt = INT32_MIN;
  int16_t _prevHdg = INT16_MIN;
  lv_obj_t* _lblMaster = nullptr;
  lv_obj_t* _lblModes[5] = {};
  lv_obj_t* _lblTargetAlt = nullptr;
  lv_obj_t* _lblTargetHdg = nullptr;
};

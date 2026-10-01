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

    // AP Master: a large annunciator tile
    _tileMaster = makeTile(parent, 12, 20, 140, 40, &lv_font_montserrat_28, "AP OFF");
    _lblMaster = lv_obj_get_child(_tileMaster, 0);

    // Mode annunciators: a row of tiles that light up when engaged
    static const char* MODE_NAMES[] = { "HDG", "ALT", "VS", "NAV", "APR" };
    for (int i = 0; i < 5; i++) {
      _tileModes[i] = makeTile(parent, 12 + i * 60, 68, 56, 30, &lv_font_montserrat_18, MODE_NAMES[i]);
    }

    // Target altitude
    lv_obj_t* altTitle = lv_label_create(parent);
    lv_obj_set_style_text_color(altTitle, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(altTitle, &lv_font_montserrat_10, 0);
    lv_label_set_text(altTitle, "TARGET ALT");
    lv_obj_set_pos(altTitle, 15, 114);

    _lblTargetAlt = lv_label_create(parent);
    lv_obj_set_style_text_color(_lblTargetAlt, lv_color_make(0, 220, 0), 0);
    lv_obj_set_style_text_font(_lblTargetAlt, &lv_font_montserrat_28, 0);
    lv_label_set_text(_lblTargetAlt, "-----");
    lv_obj_set_pos(_lblTargetAlt, 15, 127);

    lv_obj_t* ftLabel = lv_label_create(parent);
    lv_obj_set_style_text_color(ftLabel, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(ftLabel, &lv_font_montserrat_10, 0);
    lv_label_set_text(ftLabel, "FT");
    lv_obj_set_pos(ftLabel, 112, 143);

    // Target heading
    lv_obj_t* hdgTitle = lv_label_create(parent);
    lv_obj_set_style_text_color(hdgTitle, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(hdgTitle, &lv_font_montserrat_10, 0);
    lv_label_set_text(hdgTitle, "TARGET HDG");
    lv_obj_set_pos(hdgTitle, 15, 167);

    _lblTargetHdg = lv_label_create(parent);
    lv_obj_set_style_text_color(_lblTargetHdg, lv_color_make(0, 180, 255), 0);
    lv_obj_set_style_text_font(_lblTargetHdg, &lv_font_montserrat_28, 0);
    lv_label_set_text(_lblTargetHdg, "---");
    lv_obj_set_pos(_lblTargetHdg, 15, 180);
  }

  void setValue(uint16_t modeFlags, int32_t targetAlt, int16_t targetHdg) {
    if (!_lblMaster) return;
    if (modeFlags == _prevFlags && targetAlt == _prevAlt && targetHdg == _prevHdg) return;
    _prevFlags = modeFlags; _prevAlt = targetAlt; _prevHdg = targetHdg;

    char buf[16];

    // AP Master
    bool master = modeFlags & 0x01;
    lv_label_set_text(_lblMaster, master ? "AP ON" : "AP OFF");
    setTile(_tileMaster, master);

    // Mode annunciators: bits 1-5 map to HDG, ALT, VS, NAV, APR
    for (int i = 0; i < 5; i++) {
      setTile(_tileModes[i], modeFlags & (1 << (i + 1)));
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
  /// Rounded annunciator tile with a centred label (child 0).
  static lv_obj_t* makeTile(lv_obj_t* parent, int x, int y, int w, int h,
                            const lv_font_t* font, const char* text) {
    lv_obj_t* tile = lv_obj_create(parent);
    lv_obj_remove_style_all(tile);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(tile, x, y);
    lv_obj_set_size(tile, w, h);
    lv_obj_set_style_radius(tile, 6, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tile, 2, 0);

    lv_obj_t* lbl = lv_label_create(tile);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    setTile(tile, false);
    return tile;
  }

  /// Engaged: lit green with dark text. Off: dark tile, dim outline and text.
  static void setTile(lv_obj_t* tile, bool on) {
    lv_obj_t* lbl = lv_obj_get_child(tile, 0);
    lv_obj_set_style_bg_color(tile, on ? lv_color_make(0, 200, 70) : lv_color_make(16, 18, 22), 0);
    lv_obj_set_style_border_color(tile, on ? lv_color_make(120, 255, 160) : lv_color_make(55, 60, 66), 0);
    lv_obj_set_style_text_color(lbl, on ? lv_color_make(0, 24, 8) : lv_color_make(85, 90, 96), 0);
    lv_obj_center(lbl);  // the text width changes ("AP ON" / "AP OFF")
  }

  lv_obj_t* _tileMaster = nullptr;
  lv_obj_t* _tileModes[5] = {};
  uint16_t _prevFlags = UINT16_MAX;
  int32_t _prevAlt = INT32_MIN;
  int16_t _prevHdg = INT16_MIN;
  lv_obj_t* _lblMaster = nullptr;
  lv_obj_t* _lblTargetAlt = nullptr;
  lv_obj_t* _lblTargetHdg = nullptr;
};

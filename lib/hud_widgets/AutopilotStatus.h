#pragma once
#include <lvgl.h>
#include <stdint.h>
#include <stdio.h>
#include "hud_proto.h"

/// Autopilot screen: master switch, mode annunciators and targets.
///
/// Every tile is also a button: tapping it asks the simulator to toggle that
/// mode, and the ± buttons move the heading bug and the altitude target
/// (holding them repeats). The display never changes a tile itself; it shows
/// what the simulator reports, so a tap shows up with the next telemetry
/// frame, about 50 ms later.
class AutopilotStatus {
public:
  /// Receives a HudCommand when a control is used (sends it to the PC).
  using CommandHandler = void (*)(uint8_t command);

  AutopilotStatus() = default;

  void setCommandHandler(CommandHandler handler) { _onCommand = handler; }

  void create(lv_obj_t* parent) {
    // Title
    lv_obj_t* title = lv_label_create(parent);
    lv_obj_set_style_text_color(title, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_10, 0);
    lv_label_set_text(title, "MSFS AUTOPILOT");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);

    // AP Master: a large annunciator tile
    _tileMaster = makeTile(parent, 12, 20, 140, 40, &lv_font_montserrat_28, "AP OFF", CMD_AP_MASTER);
    _lblMaster = lv_obj_get_child(_tileMaster, 0);

    lv_obj_t* hint = lv_label_create(parent);
    lv_obj_set_style_text_color(hint, lv_color_make(80, 80, 80), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_label_set_text(hint, "TAP TO\nENGAGE");
    lv_obj_set_pos(hint, 164, 27);

    // Mode annunciators: a row of tiles that light up when engaged
    static const char* MODE_NAMES[] = { "HDG", "ALT", "VS", "NAV", "APR" };
    static const uint8_t MODE_COMMANDS[] = { CMD_AP_HDG, CMD_AP_ALT, CMD_AP_VS, CMD_AP_NAV, CMD_AP_APR };
    for (int i = 0; i < 5; i++) {
      _tileModes[i] = makeTile(parent, 12 + i * 60, 68, 56, 30, &lv_font_montserrat_18,
                               MODE_NAMES[i], MODE_COMMANDS[i]);
    }

    // Target altitude with - / + (one autopilot step, usually 100 ft)
    lv_obj_t* altTitle = lv_label_create(parent);
    lv_obj_set_style_text_color(altTitle, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(altTitle, &lv_font_montserrat_10, 0);
    lv_label_set_text(altTitle, "TARGET ALT");
    lv_obj_set_pos(altTitle, 15, 114);

    _lblTargetAlt = lv_label_create(parent);
    lv_obj_set_style_text_font(_lblTargetAlt, &lv_font_montserrat_28, 0);
    lv_label_set_text(_lblTargetAlt, "-----");
    lv_obj_set_pos(_lblTargetAlt, 15, 127);

    lv_obj_t* ftLabel = lv_label_create(parent);
    lv_obj_set_style_text_color(ftLabel, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(ftLabel, &lv_font_montserrat_10, 0);
    lv_label_set_text(ftLabel, "FT");
    lv_obj_set_pos(ftLabel, 128, 143);

    makeStepButton(parent, 196, 118, LV_SYMBOL_MINUS, CMD_ALT_DEC);
    makeStepButton(parent, 256, 118, LV_SYMBOL_PLUS, CMD_ALT_INC);

    // Target heading (heading bug) with - / + (1°)
    lv_obj_t* hdgTitle = lv_label_create(parent);
    lv_obj_set_style_text_color(hdgTitle, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(hdgTitle, &lv_font_montserrat_10, 0);
    lv_label_set_text(hdgTitle, "TARGET HDG");
    lv_obj_set_pos(hdgTitle, 15, 167);

    _lblTargetHdg = lv_label_create(parent);
    lv_obj_set_style_text_font(_lblTargetHdg, &lv_font_montserrat_28, 0);
    lv_label_set_text(_lblTargetHdg, "---");
    lv_obj_set_pos(_lblTargetHdg, 15, 180);

    makeStepButton(parent, 196, 171, LV_SYMBOL_MINUS, CMD_HDG_DEC);
    makeStepButton(parent, 256, 171, LV_SYMBOL_PLUS, CMD_HDG_INC);

    showTargets(0);
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

    // Targets are always shown so they can be set before engaging the mode
    snprintf(buf, sizeof(buf), "%ld", (long)targetAlt);
    lv_label_set_text(_lblTargetAlt, buf);
    int hdg = ((targetHdg / 10) % 360 + 360) % 360;
    snprintf(buf, sizeof(buf), "%03d", hdg);
    lv_label_set_text(_lblTargetHdg, buf);
    showTargets(modeFlags);
  }

private:
  CommandHandler _onCommand = nullptr;

  /// Bright when the matching hold is engaged, dim otherwise.
  void showTargets(uint16_t modeFlags) {
    lv_obj_set_style_text_color(_lblTargetAlt,
      (modeFlags & 0x04) ? lv_color_make(0, 220, 0) : lv_color_make(110, 120, 110), 0);
    lv_obj_set_style_text_color(_lblTargetHdg,
      (modeFlags & 0x02) ? lv_color_make(0, 180, 255) : lv_color_make(110, 120, 130), 0);
  }

  static void onControl(lv_event_t* e) {
    auto* self = (AutopilotStatus*)lv_event_get_user_data(e);
    auto* target = (lv_obj_t*)lv_event_get_current_target(e);
    uint8_t command = (uint8_t)(uintptr_t)lv_obj_get_user_data(target);
    if (self && self->_onCommand) self->_onCommand(command);
  }

  /// Rounded annunciator tile with a centred label (child 0); tapping it sends `command`.
  lv_obj_t* makeTile(lv_obj_t* parent, int x, int y, int w, int h,
                     const lv_font_t* font, const char* text, uint8_t command) {
    lv_obj_t* tile = lv_obj_create(parent);
    lv_obj_remove_style_all(tile);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(tile, x, y);
    lv_obj_set_size(tile, w, h);
    lv_obj_set_style_radius(tile, 6, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tile, 2, 0);
    // Immediate feedback while the finger is down (the state follows from the sim)
    lv_obj_set_style_border_color(tile, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(tile, 3, LV_STATE_PRESSED);
    lv_obj_set_user_data(tile, (void*)(uintptr_t)command);
    lv_obj_add_event_cb(tile, onControl, LV_EVENT_CLICKED, this);

    lv_obj_t* lbl = lv_label_create(tile);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    setTile(tile, false);
    return tile;
  }

  /// - / + button; holding it repeats the step.
  lv_obj_t* makeStepButton(lv_obj_t* parent, int x, int y, const char* symbol, uint8_t command) {
    lv_obj_t* btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, 52, 40);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_make(30, 34, 40), 0);
    lv_obj_set_style_bg_color(btn, lv_color_make(70, 80, 95), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(btn, lv_color_make(70, 76, 84), 0);
    lv_obj_set_user_data(btn, (void*)(uintptr_t)command);
    // CLICKED for a tap, LONG_PRESSED_REPEAT about 10x per second while held
    lv_obj_add_event_cb(btn, onControl, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_add_event_cb(btn, onControl, LV_EVENT_LONG_PRESSED_REPEAT, this);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl, lv_color_make(220, 220, 220), 0);
    lv_label_set_text(lbl, symbol);
    lv_obj_center(lbl);
    return btn;
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

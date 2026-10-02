#pragma once
#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "hud_proto.h"
#include "Anim.h"

/// Airbus-style ECAM engine/warning display (the upper ECAM, "E/WD"):
/// N1 and EGT dials with digital readouts, N2 and fuel flow, fuel on board,
/// slats/flaps, and the memo area with warnings (left) and memos (right).
/// Shows engines 1 and 2; values come from MSG_ECAM_ENGINE/MSG_ECAM_STATUS,
/// warnings from MSG_ALERTS.
class EcamDisplay {
public:
  void create(lv_obj_t* parent) {
    // ---- engine dials ----
    for (int e = 0; e < 2; e++) {
      int cx = engX(e);
      makeDial(parent, _n1[e], cx, 48, 31, 0, 1100, 1000, /*labels*/ true);
      _n1[e].value = makeValueBox(parent, cx - 7, 50, 46, 20, &lv_font_montserrat_18);
      makeDial(parent, _egt[e], cx, 116, 22, 0, 1000, 950, false);
      _egt[e].value = makeValueBox(parent, cx - 3, 114, 36, 16, &lv_font_montserrat_12);
      _n2[e] = makeLabel(parent, cx - 28, 140, &lv_font_montserrat_18, GREEN, "0.0");
      _ff[e] = makeLabel(parent, cx - 28, 164, &lv_font_montserrat_14, GREEN, "0");
      lv_obj_set_width(_n2[e], 56);
      lv_obj_set_width(_ff[e], 56);
      lv_obj_set_style_text_align(_n2[e], LV_TEXT_ALIGN_RIGHT, 0);
      lv_obj_set_style_text_align(_ff[e], LV_TEXT_ALIGN_RIGHT, 0);
    }
    // Parameter names between the engines, Airbus-style in cyan
    centerLabel(parent, 34, "N1", "%");
    centerLabel(parent, 104, "EGT", "\xC2\xB0" "C");
    centerLabel(parent, 141, "N2", "%");
    centerLabel(parent, 163, "FF", "KG/H");

    // ---- right column: fuel on board, slats/flaps ----
    makeLabel(parent, 206, 20, &lv_font_montserrat_12, CYAN, "FOB :");
    _fob = makeLabel(parent, 244, 16, &lv_font_montserrat_18, GREEN, "0");
    makeLabel(parent, 296, 22, &lv_font_montserrat_10, CYAN, "KG");

    makeLabel(parent, 210, 60, &lv_font_montserrat_12, CYAN, "S");
    makeLabel(parent, 298, 60, &lv_font_montserrat_12, CYAN, "F");
    // Wing reference: a short grey bar, slats left of it, flaps right of it
    static lv_point_precise_t wing[2] = {{WING_L, WING_Y}, {WING_R, WING_Y}};
    lv_obj_t* w = lv_line_create(parent);
    lv_line_set_points(w, wing, 2);
    lv_obj_set_style_line_color(w, lv_color_make(150, 150, 150), 0);
    lv_obj_set_style_line_width(w, 5, 0);
    _slat = makeSurface(parent, _slatPts);
    _flap = makeSurface(parent, _flapPts);
    makeLabel(parent, 226, 104, &lv_font_montserrat_10, CYAN, "FLAPS");
    _flapIdx = makeLabel(parent, 266, 98, &lv_font_montserrat_18, GREEN, "0");

    // ---- memo area ----
    lv_obj_t* sep = lv_obj_create(parent);
    lv_obj_remove_style_all(sep);
    lv_obj_set_size(sep, 316, 1);
    lv_obj_set_pos(sep, 2, 190);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sep, lv_color_make(150, 150, 150), 0);
    lv_obj_t* vsep = lv_obj_create(parent);
    lv_obj_remove_style_all(vsep);
    lv_obj_set_size(vsep, 1, 44);
    lv_obj_set_pos(vsep, 186, 194);
    lv_obj_set_style_bg_opa(vsep, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(vsep, lv_color_make(90, 90, 90), 0);
    for (int i = 0; i < MEMO_LINES; i++) {
      _warn[i] = makeLabel(parent, 6, 194 + i * 14, &lv_font_montserrat_12, RED, "");
      _memo[i] = makeLabel(parent, 194, 194 + i * 14, &lv_font_montserrat_12, GREEN, "");
    }

    setStatus(0, 0, 0, 0, 0);
    for (int e = 0; e < 2; e++) setEngine(e, 0, 0, 0, 0);
  }

  void setEngine(int idx, uint16_t n1, uint16_t n2, int16_t egt, uint16_t fuelFlow) {
    if (idx < 0 || idx > 1 || !_n2[idx]) return;
    setDial(_n1[idx], n1);
    setDial(_egt[idx], egt);
    char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u", n1 / 10, n1 % 10);
    setText(_n1[idx].value, buf);
    // Over the red line: readout turns red, like the real one
    lv_obj_set_style_text_color(_n1[idx].value, n1 > 1000 ? RED : GREEN, 0);
    snprintf(buf, sizeof(buf), "%d", (int)egt);
    setText(_egt[idx].value, buf);
    lv_obj_set_style_text_color(_egt[idx].value, egt > 950 ? RED : GREEN, 0);
    snprintf(buf, sizeof(buf), "%u.%u", n2 / 10, n2 % 10);
    setText(_n2[idx], buf);
    snprintf(buf, sizeof(buf), "%u", fuelFlow);
    setText(_ff[idx], buf);
  }

  void setStatus(uint32_t fobKg, uint8_t flapsIndex, uint8_t slatsPct, uint8_t flapsPct, uint16_t memo) {
    if (!_fob) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)fobKg);
    setText(_fob, buf);
    snprintf(buf, sizeof(buf), "%u", flapsIndex);
    setText(_flapIdx, buf);

    // Slats droop forward-down from the wing's left end, flaps aft-down from its right end
    surface(_slat, _slatPts, WING_L, -1, slatsPct);
    surface(_flap, _flapPts, WING_R, +1, flapsPct);

    if (memo != _memoFlags) {
      _memoFlags = memo;
      static const struct { uint16_t bit; const char* text; } MEMOS[] = {
        {MEMO_PARK_BRK, "PARK BRK"},       {MEMO_SPEED_BRK, "SPEED BRK"},
        {MEMO_SPLRS_ARMED, "GND SPLRS ARMED"}, {MEMO_SEAT_BELTS, "SEAT BELTS"},
        {MEMO_APU_AVAIL, "APU AVAIL"},     {MEMO_ENG_ANTI_ICE, "ENG A.ICE"},
        {MEMO_LDG_LT, "LDG LT"},
      };
      int line = 0;
      for (auto& m : MEMOS)
        if ((memo & m.bit) && line < MEMO_LINES) setText(_memo[line++], m.text);
      while (line < MEMO_LINES) setText(_memo[line++], "");
    }
  }

  /// Alert flags (MSG_ALERTS): shown in the warning column, red before amber.
  void setAlerts(uint16_t flags) {
    if (!_warn[0] || flags == _alertFlags) return;
    _alertFlags = flags;
    static const struct { uint16_t bit; const char* text; bool red; } WARNINGS[] = {
      {1 << 4, "ENG 1 FIRE", true},       {1 << 0, "STALL", true},
      {1 << 1, "OVERSPEED", true},        {1 << 5, "AP OFF", true},
      {1 << 2, "L/G GEAR NOT DOWN", false}, {1 << 3, "FUEL LO LEVEL", false},
    };
    int line = 0;
    for (auto& w : WARNINGS) {
      if ((flags & w.bit) && line < MEMO_LINES) {
        setText(_warn[line], w.text);
        lv_obj_set_style_text_color(_warn[line], w.red ? RED : AMBER, 0);
        line++;
      }
    }
    while (line < MEMO_LINES) setText(_warn[line++], "");
  }

private:
  struct Dial {
    lv_obj_t* needle = nullptr;
    lv_obj_t* value = nullptr;
    lv_point_precise_t pts[2] = {};
    int cx = 0, cy = 0, r = 0;
    int vmin = 0, vmax = 1;
    int shown = 0;
  };

  /// Centre x of engine e's column (a function: older toolchains need no out-of-class definition)
  static int engX(int e) { return e == 0 ? 46 : 152; }
  static const int MEMO_LINES = 3;
  static const int WING_L = 238, WING_R = 274, WING_Y = 74;
  // The dial scale runs clockwise from lower left (150°) over the top to 3 o'clock
  static const int START_DEG = 150, SWEEP_DEG = 210;
  static inline const lv_color_t GREEN = LV_COLOR_MAKE(0, 230, 70);
  static inline const lv_color_t CYAN = LV_COLOR_MAKE(0, 200, 230);
  static inline const lv_color_t RED = LV_COLOR_MAKE(255, 40, 40);
  static inline const lv_color_t AMBER = LV_COLOR_MAKE(255, 170, 0);

  Dial _n1[2], _egt[2];
  lv_obj_t* _n2[2] = {};
  lv_obj_t* _ff[2] = {};
  lv_obj_t* _fob = nullptr;
  lv_obj_t* _flapIdx = nullptr;
  lv_obj_t* _slat = nullptr;
  lv_obj_t* _flap = nullptr;
  lv_point_precise_t _slatPts[2] = {};
  lv_point_precise_t _flapPts[2] = {};
  lv_obj_t* _warn[MEMO_LINES] = {};
  lv_obj_t* _memo[MEMO_LINES] = {};
  uint16_t _memoFlags = 0xFFFF;
  uint16_t _alertFlags = 0xFFFF;

  static int angleFor(const Dial& d, int v) {
    if (v < d.vmin) v = d.vmin;
    if (v > d.vmax) v = d.vmax;
    return START_DEG + (int)((long)(v - d.vmin) * SWEEP_DEG / (d.vmax - d.vmin));
  }

  static lv_obj_t* arc(lv_obj_t* parent, int cx, int cy, int r, int fromDeg, int toDeg,
                       int width, lv_color_t color) {
    lv_obj_t* a = lv_arc_create(parent);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_remove_style(a, NULL, LV_PART_INDICATOR);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(a, 2 * r, 2 * r);
    lv_obj_set_pos(a, cx - r, cy - r);
    lv_arc_set_bg_angles(a, fromDeg % 360, toDeg % 360);
    lv_obj_set_style_arc_color(a, color, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, false, LV_PART_MAIN);
    return a;
  }

  void makeDial(lv_obj_t* parent, Dial& d, int cx, int cy, int r, int vmin, int vmax,
                int redFrom, bool labels) {
    d.cx = cx; d.cy = cy; d.r = r; d.vmin = vmin; d.vmax = vmax;
    arc(parent, cx, cy, r, START_DEG, START_DEG + SWEEP_DEG, 2, lv_color_make(210, 210, 210));
    // Red limit zone just outside the scale
    arc(parent, cx, cy, r + 3, angleFor(d, redFrom), START_DEG + SWEEP_DEG, 3, RED);
    if (labels) {  // "5" and "10" (x10 %) inside the scale, like the A320
      static const char* const TXT[] = {"5", "10"};
      static const int AT[] = {500, 1000};
      for (int i = 0; i < 2; i++) {
        float a = angleFor(d, AT[i]) * (float)(M_PI / 180.0);
        lv_obj_t* l = makeLabel(parent, 0, 0, &lv_font_montserrat_10, lv_color_make(200, 200, 200), TXT[i]);
        lv_obj_set_pos(l, cx + (int)lroundf((r - 10) * cosf(a)) - 4, cy + (int)lroundf((r - 10) * sinf(a)) - 6);
      }
    }
    d.needle = lv_line_create(parent);
    lv_obj_set_style_line_color(d.needle, GREEN, 0);
    lv_obj_set_style_line_width(d.needle, 3, 0);
    lv_obj_set_style_line_rounded(d.needle, true, 0);
    d.shown = vmin;
    drawNeedle(&d, vmin);
  }

  static void drawNeedle(void* var, int32_t v) {
    Dial* d = (Dial*)var;
    d->shown = v;
    float a = angleFor(*d, v) * (float)(M_PI / 180.0);
    d->pts[0].x = d->cx;
    d->pts[0].y = d->cy;
    d->pts[1].x = (lv_value_precise_t)lroundf(d->cx + (d->r - 3) * cosf(a));
    d->pts[1].y = (lv_value_precise_t)lroundf(d->cy + (d->r - 3) * sinf(a));
    lv_line_set_points(d->needle, d->pts, 2);
  }

  static void setDial(Dial& d, int v) { hud::valueTo(&d, drawNeedle, d.shown, v); }

  static lv_obj_t* makeValueBox(lv_obj_t* parent, int x, int y, int w, int h, const lv_font_t* font) {
    lv_obj_t* box = lv_label_create(parent);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, w, h);
    lv_obj_set_style_text_font(box, font, 0);
    lv_obj_set_style_text_color(box, GREEN, 0);
    lv_obj_set_style_text_align(box, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, lv_color_make(120, 120, 120), 0);
    lv_obj_set_style_pad_right(box, 3, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(box, lv_color_black(), 0);
    lv_label_set_text(box, "0");
    return box;
  }

  static lv_obj_t* makeLabel(lv_obj_t* parent, int x, int y, const lv_font_t* font,
                             lv_color_t color, const char* text) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, x, y);
    return l;
  }

  /// "N1" above "%" between the two engines
  static void centerLabel(lv_obj_t* parent, int y, const char* name, const char* unit) {
    lv_obj_t* n = makeLabel(parent, 0, y, &lv_font_montserrat_12, CYAN, name);
    lv_obj_set_width(n, 40);
    lv_obj_set_style_text_align(n, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_x(n, (engX(0) + engX(1)) / 2 - 20);
    lv_obj_t* u = makeLabel(parent, 0, y + 13, &lv_font_montserrat_10, CYAN, unit);
    lv_obj_set_width(u, 40);
    lv_obj_set_style_text_align(u, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_x(u, (engX(0) + engX(1)) / 2 - 20);
  }

  static lv_obj_t* makeSurface(lv_obj_t* parent, lv_point_precise_t* pts) {
    lv_obj_t* l = lv_line_create(parent);
    lv_obj_set_style_line_color(l, GREEN, 0);
    lv_obj_set_style_line_width(l, 4, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_line_set_points(l, pts, 2);
    return l;
  }

  /// Draws a slat (side -1) or flap (side +1) deflected by pct (0 = retracted).
  static void surface(lv_obj_t* line, lv_point_precise_t* pts, int x, int side, uint8_t pct) {
    float a = (pct / 100.0f) * 40.0f * (float)(M_PI / 180.0);  // up to 40° down
    const float len = 22;
    pts[0].x = x;
    pts[0].y = WING_Y;
    pts[1].x = (lv_value_precise_t)lroundf(x + side * len * cosf(a));
    pts[1].y = (lv_value_precise_t)lroundf(WING_Y + len * sinf(a));
    lv_line_set_points(line, pts, 2);
  }

  static void setText(lv_obj_t* label, const char* text) {
    if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
  }
};

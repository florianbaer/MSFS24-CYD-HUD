#pragma once
#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <new>
#include "Smoothing.h"

struct GyroHorizonConfig {
  int cx = 160;
  int cy = 105;
  int radius = 50;
  /// Ease toward new samples in tick() instead of jumping on every setValue().
  bool smooth = false;
};

/// Attitude indicator drawn into an RGB565 canvas.
///
/// The ball is rasterised per pixel with anti-aliased edges (horizon, pitch
/// ladder, bezel) and ordered dithering on the sky/ground gradients, so the
/// 16-bit panel shows neither jaggies nor colour bands. Overlays that do not
/// move with the horizon frame are drawn with LVGL's own anti-aliased primitives.
class GyroHorizon {
public:
  GyroHorizon() : _canvas(nullptr), _label(nullptr), _modeLabel(nullptr), _cbuf(nullptr) {}

  ~GyroHorizon() { destroy(); }

  void destroy() {
    if (_cbuf) { delete[] _cbuf; _cbuf = nullptr; }
    // LVGL objects are freed when their parent screen is deleted
    _canvas = nullptr;
    _label = nullptr;
    _modeLabel = nullptr;
  }

  void create(lv_obj_t* parent, const GyroHorizonConfig& cfg) {
    if (cfg.radius < 10 || cfg.radius > 120) return;

    destroy(); // clean up any previous allocation
    _cfg = cfg;
    int d = cfg.radius * 2;

    _cbuf = new (std::nothrow) uint8_t[d * d * 2];
    if (!_cbuf) {
      lv_obj_t* err = lv_label_create(parent);
      lv_obj_set_style_text_color(err, lv_color_make(255, 60, 60), 0);
      lv_label_set_text(err, "GYRO: NO MEM");
      lv_obj_center(err);
      return;
    }

    _canvas = lv_canvas_create(parent);
    if (!_canvas) { destroy(); return; }
    lv_canvas_set_buffer(_canvas, _cbuf, d, d, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(_canvas, cfg.cx - cfg.radius, cfg.cy - cfg.radius);
    lv_obj_remove_flag(_canvas, LV_OBJ_FLAG_CLICKABLE);

    _label = lv_label_create(parent);
    lv_obj_set_style_text_color(_label, lv_color_make(0, 200, 0), 0);
    lv_obj_set_style_text_font(_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(_label, "HDG 000");
    lv_obj_set_pos(_label, cfg.cx - 28, cfg.cy + cfg.radius + 4);

    _modeLabel = lv_label_create(parent);
    lv_obj_set_style_text_color(_modeLabel, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(_modeLabel, &lv_font_montserrat_10, 0);
    lv_label_set_text(_modeLabel, "MSFS GYRO");
    lv_obj_set_pos(_modeLabel, cfg.cx - 30, cfg.cy - cfg.radius - 13);

    buildGradients();
    _pitch.setTarget(0, true);
    _roll.setTarget(0, true);
    _heading.setTarget(0, true);
    render();
  }

  /// New attitude sample in wire units (tenths of a degree).
  void setValue(int16_t pitch, int16_t roll, int16_t heading) {
    if (!_canvas || !_cbuf) return;
    bool snap = !_cfg.smooth;
    _pitch.setTarget(pitch, snap);
    _roll.setTarget(roll, snap);
    _heading.setTarget(heading, snap);
    if (snap) render();
    else _dirty = true;
  }

  /// Call every loop iteration when smoothing is enabled. Redraws at most
  /// every FRAME_MS, and only while the gyro screen is the one being shown.
  void tick(uint32_t nowMs) {
    if (!_canvas || !_cfg.smooth) return;
    uint32_t dt = nowMs - _lastTickMs;
    if (dt < FRAME_MS) return;
    _lastTickMs = nowMs;
    if (dt > 200) dt = 200;  // after a stall, catch up in one go

    if (_dirty) {
      bool moved = _pitch.step(dt, TAU_MS);
      moved |= _roll.step(dt, TAU_MS);
      moved |= _heading.step(dt, TAU_MS);
      if (_pitch.settled() && _roll.settled() && _heading.settled()) _dirty = false;
      if (moved) _needsRender = true;
    }
    if (_needsRender && lv_obj_get_screen(_canvas) == lv_screen_active()) render();
  }

private:
  // ~40 fps for the canvas; the 180x180 canvas needs ~10 ms on the SPI bus
  static const uint32_t FRAME_MS = 25;
  // Time constant of the easing: small enough to feel immediate at 20 Hz input
  static constexpr float TAU_MS = 45.0f;

  struct Rgb { uint8_t r, g, b; };

  GyroHorizonConfig _cfg;
  lv_obj_t* _canvas;
  lv_obj_t* _label;
  lv_obj_t* _modeLabel;
  uint8_t* _cbuf;
  SmoothedValue _pitch;
  SmoothedValue _roll{3600.0f};
  SmoothedValue _heading{3600.0f};
  uint32_t _lastTickMs = 0;
  bool _dirty = false;        // targets not reached yet
  bool _needsRender = false;  // displayed values changed since the last render
  int _shownHdg = -1;

  // Sky/ground colour as a function of the distance from the horizon (px).
  // Index GRAD_HALF is the horizon itself; beyond the table the colour is flat.
  static const int GRAD_HALF = 96;
  Rgb _grad[2 * GRAD_HALF + 1];

  // Pixels per degree of pitch — at radius=90, full ±60° range fills the circle
  float pixPerDeg() const { return _cfg.radius / 60.0f; }

  void buildGradients() {
    // Sky: lighter near the horizon, deeper blue above. Ground: warm brown near
    // the horizon, darker below. Gentle enough to read as a lit instrument.
    const Rgb skyHorizon = {0x3C, 0x8C, 0xE6}, skyTop = {0x10, 0x3C, 0x96};
    const Rgb gndHorizon = {0x8E, 0x56, 0x22}, gndBottom = {0x4A, 0x26, 0x0C};
    for (int i = -GRAD_HALF; i <= GRAD_HALF; i++) {
      float t = fabsf((float)i) / GRAD_HALF;
      t = t * (2.0f - t);  // ease-out: most of the change happens near the horizon
      _grad[i + GRAD_HALF] = i < 0 ? mix(skyHorizon, skyTop, t) : mix(gndHorizon, gndBottom, t);
    }
  }

  void render() {
    _needsRender = false;
    const int r = _cfg.radius;
    const int d = r * 2;
    const float rf = (float)r;

    const float roll_rad = (_roll.value() / 10.0f) * (float)(M_PI / 180.0);
    const float sin_r = sinf(roll_rad);
    const float cos_r = cosf(roll_rad);
    const float ppd = pixPerDeg();
    // Positive pitch = nose up = horizon moves DOWN on screen
    const float pitch_px = (_pitch.value() / 10.0f) * ppd;

    const Rgb white = {0xFF, 0xFF, 0xFF};
    const Rgb ladder = {0xF0, 0xF0, 0xF0};
    const Rgb bezel = {0x48, 0x48, 0x48};
    const Rgb black = {0, 0, 0};
    const Rgb skyEdge = _grad[GRAD_HALF - 1];
    const Rgb gndEdge = _grad[GRAD_HALF + 1];

    const float bezelInner = rf - 3.0f;
    const float r2 = rf * rf;
    const float r2bezel = (bezelInner - 1.0f) * (bezelInner - 1.0f);
    const float rungLong = rf * 0.26f;   // 10° rungs
    const float rungShort = rf * 0.13f;  // 5° rungs
    const float rungStep = 5.0f * ppd;

    uint16_t* px = (uint16_t*)_cbuf;

    for (int y = 0; y < d; y++) {
      const float fy = y + 0.5f - rf;
      // Horizontal extent of the disc on this row: skip the black corners fast
      const float half = sqrtf(fmaxf(0.0f, r2 - fy * fy));
      const int xStart = (int)floorf(rf - half);
      const int xEnd = (int)ceilf(rf + half);
      uint16_t* row = px + y * d;

      for (int x = 0; x < xStart && x < d; x++) row[x] = 0;
      for (int x = xEnd < 0 ? 0 : xEnd; x < d; x++) row[x] = 0;

      // Aircraft-frame coordinates advance linearly along the row
      float fx = xStart + 0.5f - rf;
      float ax = fx * cos_r + fy * sin_r;  // along the horizon
      float ay = -fx * sin_r + fy * cos_r; // across the horizon (+ = down)

      for (int x = xStart < 0 ? 0 : xStart; x < xEnd && x < d; x++) {
        const float s = ay - pitch_px;  // signed distance to the horizon, + = ground
        Rgb c;

        // Sky / ground with an anti-aliased boundary
        if (s <= -1.0f || s >= 1.0f) {
          int gi = (int)s;
          if (gi < -GRAD_HALF) gi = -GRAD_HALF;
          if (gi > GRAD_HALF) gi = GRAD_HALF;
          c = _grad[gi + GRAD_HALF];
        } else {
          c = mix(skyEdge, gndEdge, clamp01(s + 0.5f));
        }

        // Horizon line, 2 px wide
        const float as = fabsf(s);
        if (as < 2.0f) c = mix(c, white, clamp01(1.5f - as));

        // Pitch ladder: one rung every 5°, the nearest one is the only candidate
        const int k = (int)floorf(-s / rungStep + 0.5f);
        if (k != 0 && k >= -6 && k <= 6) {
          const float halfLen = (k % 2 == 0) ? rungLong : rungShort;
          const float sk = s + k * rungStep;
          const float aax = fabsf(ax);
          if (fabsf(sk) < 2.0f && aax < halfLen + 2.0f) {
            const float along = aax > halfLen ? aax - halfLen : 0.0f;
            const float dist = sqrtf(along * along + sk * sk);
            c = mix(c, ladder, clamp01(1.25f - dist));  // ~1.5 px line, round ends
          }
        }

        // Bezel ring and anti-aliased outer edge
        const float dist2 = fx * fx + fy * fy;
        if (dist2 > r2bezel) {
          const float dist = sqrtf(dist2);
          c = mix(c, bezel, clamp01(dist - bezelInner + 0.5f));
          c = mix(black, c, clamp01(rf - dist + 0.5f));
        }

        row[x] = dither(c, x, y);

        fx += 1.0f;
        ax += cos_r;
        ay -= sin_r;
      }
    }

    drawOverlay(sin_r, cos_r, pitch_px, ppd, rungLong);
    lv_obj_invalidate(_canvas);
    updateHeadingLabel();
  }

  // Bank scale, roll pointer, pitch numbers and the aircraft symbol, drawn
  // with LVGL's anti-aliased primitives on top of the rasterised ball.
  void drawOverlay(float sin_r, float cos_r, float pitch_px, float ppd, float rungLong) {
    const float r = (float)_cfg.radius;
    const float c = r;  // canvas centre
    lv_layer_t layer;
    lv_canvas_init_layer(_canvas, &layer);

    // Bank scale: fixed ticks at 10, 20, 30, 45 and 60 degrees each side
    lv_draw_line_dsc_t tick;
    lv_draw_line_dsc_init(&tick);
    tick.color = lv_color_white();
    tick.width = 2;
    tick.round_start = 1;
    tick.round_end = 1;
    static const int BANK[] = {-60, -45, -30, -20, -10, 10, 20, 30, 45, 60};
    const float outer = r - 5.0f;
    for (int deg : BANK) {
      float len = (deg % 30 == 0) ? 9.0f : 5.0f;
      float a = deg * (float)(M_PI / 180.0);
      tick.p1.x = (lv_value_precise_t)lroundf(c + outer * sinf(a));
      tick.p1.y = (lv_value_precise_t)lroundf(c - outer * cosf(a));
      tick.p2.x = (lv_value_precise_t)lroundf(c + (outer - len) * sinf(a));
      tick.p2.y = (lv_value_precise_t)lroundf(c - (outer - len) * cosf(a));
      lv_draw_line(&layer, &tick);
    }

    // Zero-bank index (fixed) and roll pointer (turns with the horizon)
    lv_draw_triangle_dsc_t tri;
    lv_draw_triangle_dsc_init(&tri);
    tri.bg_opa = LV_OPA_COVER;
    tri.bg_color = lv_color_white();
    // index: tip points at the centre, base on the tick arc
    setTriangle(tri, c, c - (outer - 7.0f), 5.0f, -7.0f, 0.0f, 1.0f);
    lv_draw_triangle(&layer, &tri);

    tri.bg_color = lv_color_make(255, 220, 0);
    // pointer: "up" in the horizon frame, tip towards the index
    const float pr = outer - 9.0f;
    setTriangle(tri, c + pr * sin_r, c - pr * cos_r, 5.0f, 7.0f, sin_r, cos_r);
    lv_draw_triangle(&layer, &tri);

    // Pitch numbers beside the 10° and 20° rungs
    lv_draw_label_dsc_t num;
    lv_draw_label_dsc_init(&num);
    num.color = lv_color_white();
    num.font = &lv_font_montserrat_10;
    num.align = LV_TEXT_ALIGN_CENTER;
    static const char* const TXT[] = {"20", "10", "10", "20"};
    static const int DEG[] = {20, 10, -10, -20};
    for (int i = 0; i < 4; i++) {
      const float ay = pitch_px - DEG[i] * ppd;
      for (int side = -1; side <= 1; side += 2) {
        const float ax = side * (rungLong + 10.0f);
        const float sx = ax * cos_r - ay * sin_r;
        const float sy = ax * sin_r + ay * cos_r;
        if (sx * sx + sy * sy > (r - 18.0f) * (r - 18.0f)) continue;
        lv_area_t a;
        a.x1 = (int32_t)lroundf(c + sx) - 9;
        a.x2 = a.x1 + 18;
        a.y1 = (int32_t)lroundf(c + sy) - 6;
        a.y2 = a.y1 + 12;
        num.text = TXT[i];
        lv_draw_label(&layer, &num, &a);
      }
    }

    // Aircraft symbol: black outline under yellow wings and centre dot
    const int ci = (int)c;
    const int wingOuter = _cfg.radius / 3;
    const int wingInner = _cfg.radius / 9;
    lv_draw_line_dsc_t wing;
    lv_draw_line_dsc_init(&wing);
    wing.round_start = 1;
    wing.round_end = 1;
    for (int pass = 0; pass < 2; pass++) {
      wing.color = pass == 0 ? lv_color_black() : lv_color_make(255, 220, 0);
      wing.width = pass == 0 ? 6 : 3;
      for (int side = -1; side <= 1; side += 2) {
        wing.p1.x = ci + side * wingInner;
        wing.p1.y = ci;
        wing.p2.x = ci + side * wingOuter;
        wing.p2.y = ci;
        lv_draw_line(&layer, &wing);
        // short downward tick at the inner end, like a classic AI symbol
        wing.p1.x = ci + side * wingInner;
        wing.p1.y = ci;
        wing.p2.x = ci + side * wingInner;
        wing.p2.y = ci + 4;
        lv_draw_line(&layer, &wing);
      }
      lv_draw_rect_dsc_t dot;
      lv_draw_rect_dsc_init(&dot);
      dot.bg_color = wing.color;
      dot.radius = LV_RADIUS_CIRCLE;
      const int dr = pass == 0 ? 4 : 2;
      lv_area_t a = {ci - dr, ci - dr, ci + dr, ci + dr};
      lv_draw_rect(&layer, &dot, &a);
    }

    lv_canvas_finish_layer(_canvas, &layer);
  }

  /// Isosceles triangle with its tip at (tx, ty) and its base `h` further
  /// "down" in a frame rotated by (s, co) = (sin, cos) of the roll; a negative
  /// `h` puts the base above the tip. `w` is the half-width of the base.
  static void setTriangle(lv_draw_triangle_dsc_t& t, float tx, float ty, float w, float h,
                          float s, float co) {
    // Direction from tip to base is "down" in the rotated frame
    const float dx = -s, dy = co;          // unit vector pointing down (rotated)
    const float nx = co, ny = s;           // perpendicular
    const float bx = tx + dx * h, by = ty + dy * h;
    t.p[0].x = (lv_value_precise_t)lroundf(tx);
    t.p[0].y = (lv_value_precise_t)lroundf(ty);
    t.p[1].x = (lv_value_precise_t)lroundf(bx + nx * w);
    t.p[1].y = (lv_value_precise_t)lroundf(by + ny * w);
    t.p[2].x = (lv_value_precise_t)lroundf(bx - nx * w);
    t.p[2].y = (lv_value_precise_t)lroundf(by - ny * w);
  }

  void updateHeadingLabel() {
    int hdg = ((int)lroundf(_heading.value() / 10.0f) % 360 + 360) % 360;
    if (hdg == _shownHdg) return;
    _shownHdg = hdg;
    char buf[16];
    snprintf(buf, sizeof(buf), "HDG %03d", hdg);
    lv_label_set_text(_label, buf);
  }

  static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

  static Rgb mix(const Rgb& a, const Rgb& b, float t) {
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    return {
      (uint8_t)(a.r + (b.r - a.r) * t + 0.5f),
      (uint8_t)(a.g + (b.g - a.g) * t + 0.5f),
      (uint8_t)(a.b + (b.b - a.b) * t + 0.5f),
    };
  }

  /// RGB888 -> RGB565 with a 4x4 ordered (Bayer) dither: gradients that would
  /// band in 16 bit get the in-between shades back as a fine, static pattern.
  static uint16_t dither(const Rgb& c, int x, int y) {
    static const uint8_t BAYER[4][4] = {
      { 0,  8,  2, 10},
      {12,  4, 14,  6},
      { 3, 11,  1,  9},
      {15,  7, 13,  5},
    };
    const int t = BAYER[y & 3][x & 3];
    int r = c.r + (t >> 1);   // 0..7: one 5-bit step
    int g = c.g + (t >> 2);   // 0..3: one 6-bit step
    int b = c.b + (t >> 1);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  }
};

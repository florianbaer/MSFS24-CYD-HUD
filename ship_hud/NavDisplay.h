#pragma once
#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "hud_proto.h"
#include "Anim.h"
#include "Smoothing.h"

/// Navigation display: lat/lon, heading bug, waypoint distance/bearing, and a
/// compass card that turns with the aircraft heading (fed from the attitude
/// message) showing the heading bug and a bearing pointer to the waypoint.
class NavDisplay {
public:
  NavDisplay() = default;

  void create(lv_obj_t* parent) {
    _parent = parent;
    // Title
    lv_obj_t* title = lv_label_create(parent);
    lv_obj_set_style_text_color(title, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_10, 0);
    lv_label_set_text(title, "MSFS NAV");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);

    // Latitude
    lv_obj_t* latLabel = lv_label_create(parent);
    lv_obj_set_style_text_color(latLabel, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(latLabel, &lv_font_montserrat_10, 0);
    lv_label_set_text(latLabel, "LAT");
    lv_obj_set_pos(latLabel, 15, 25);

    _lblLat = lv_label_create(parent);
    lv_obj_set_style_text_color(_lblLat, lv_color_make(0, 220, 0), 0);
    lv_obj_set_style_text_font(_lblLat, &lv_font_montserrat_18, 0);
    lv_label_set_text(_lblLat, "0.000000");
    lv_obj_set_pos(_lblLat, 15, 38);

    // Longitude
    lv_obj_t* lonLabel = lv_label_create(parent);
    lv_obj_set_style_text_color(lonLabel, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(lonLabel, &lv_font_montserrat_10, 0);
    lv_label_set_text(lonLabel, "LON");
    lv_obj_set_pos(lonLabel, 15, 65);

    _lblLon = lv_label_create(parent);
    lv_obj_set_style_text_color(_lblLon, lv_color_make(0, 220, 0), 0);
    lv_obj_set_style_text_font(_lblLon, &lv_font_montserrat_18, 0);
    lv_label_set_text(_lblLon, "0.000000");
    lv_obj_set_pos(_lblLon, 15, 78);

    // Heading bug
    lv_obj_t* bugTitle = lv_label_create(parent);
    lv_obj_set_style_text_color(bugTitle, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(bugTitle, &lv_font_montserrat_10, 0);
    lv_label_set_text(bugTitle, "HDG BUG");
    lv_obj_set_pos(bugTitle, 15, 115);

    _lblHdgBug = lv_label_create(parent);
    lv_obj_set_style_text_color(_lblHdgBug, lv_color_make(0, 180, 255), 0);
    lv_obj_set_style_text_font(_lblHdgBug, &lv_font_montserrat_28, 0);
    lv_label_set_text(_lblHdgBug, "000");
    lv_obj_set_pos(_lblHdgBug, 15, 128);

    // Waypoint distance
    lv_obj_t* wpTitle = lv_label_create(parent);
    lv_obj_set_style_text_color(wpTitle, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(wpTitle, &lv_font_montserrat_10, 0);
    lv_label_set_text(wpTitle, "WPT DIST");
    lv_obj_set_pos(wpTitle, 15, 172);

    _lblWpDist = lv_label_create(parent);
    lv_obj_set_style_text_color(_lblWpDist, lv_color_make(200, 200, 0), 0);
    lv_obj_set_style_text_font(_lblWpDist, &lv_font_montserrat_28, 0);
    lv_label_set_text(_lblWpDist, "0.0");
    lv_obj_set_pos(_lblWpDist, 15, 185);

    lv_obj_t* nmLabel = lv_label_create(parent);
    lv_obj_set_style_text_color(nmLabel, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(nmLabel, &lv_font_montserrat_10, 0);
    lv_label_set_text(nmLabel, "NM");
    lv_obj_set_pos(nmLabel, 15, 219);

    // Waypoint bearing
    lv_obj_t* brgTitle = lv_label_create(parent);
    lv_obj_set_style_text_color(brgTitle, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(brgTitle, &lv_font_montserrat_10, 0);
    lv_label_set_text(brgTitle, "WPT BRG");
    lv_obj_set_pos(brgTitle, 110, 172);

    _lblWpBrg = lv_label_create(parent);
    lv_obj_set_style_text_color(_lblWpBrg, lv_color_make(255, 80, 255), 0);
    lv_obj_set_style_text_font(_lblWpBrg, &lv_font_montserrat_28, 0);
    lv_label_set_text(_lblWpBrg, "000");
    lv_obj_set_pos(_lblWpBrg, 110, 185);

    createCompass(parent);
  }

  /// Aircraft heading in tenths of a degree (from the attitude message).
  void setHeading(int16_t heading) {
    if (!_compass) return;
    _heading.setTarget(heading, !hud::animate || !_hasHeading);
    _hasHeading = true;
    _compassDirty = true;
    if (!hud::animate) layoutCompass();
  }

  /// Call every loop iteration: turns the card smoothly while the screen is shown.
  void tick(uint32_t nowMs) {
    if (!_compass || !hud::animate) return;
    if (nowMs - _lastTickMs < FRAME_MS) return;
    uint32_t dt = nowMs - _lastTickMs;
    _lastTickMs = nowMs;
    if (dt > 200) dt = 200;
    if (_heading.step(dt, TAU_MS)) _compassDirty = true;
    if (_compassDirty && lv_obj_get_screen(_compass) == lv_screen_active()) layoutCompass();
  }

  void setValue(int32_t lat, int32_t lon, int16_t hdgBug,
                uint16_t wpDist, int16_t wpBearing) {
    if (!_lblLat) return;
    if (lat == _prevLat && lon == _prevLon && hdgBug == _prevHdgBug
        && wpDist == _prevWpDist && wpBearing == _prevWpBearing) return;
    _prevLat = lat; _prevLon = lon; _prevHdgBug = hdgBug;
    _prevWpDist = wpDist; _prevWpBearing = wpBearing;

    char buf[32];

    // Lat/lon from 1e7 to decimal degrees
    snprintf(buf, sizeof(buf), "%c%d.%06d",
      lat >= 0 ? 'N' : 'S',
      (int)(abs(lat) / 10000000),
      (int)((abs(lat) % 10000000) / 10));
    lv_label_set_text(_lblLat, buf);

    snprintf(buf, sizeof(buf), "%c%d.%06d",
      lon >= 0 ? 'E' : 'W',
      (int)(abs(lon) / 10000000),
      (int)((abs(lon) % 10000000) / 10));
    lv_label_set_text(_lblLon, buf);

    _hdgBug = hdgBug;
    _wpBearing = wpBearing;
    _hasWaypoint = wpDist > 0;
    _compassDirty = true;
    if (!hud::animate) layoutCompass();

    // Heading bug (tenths → degrees)
    snprintf(buf, sizeof(buf), "%03d", hdgBug / 10);
    lv_label_set_text(_lblHdgBug, buf);

    // WP distance (tenths of NM)
    snprintf(buf, sizeof(buf), "%u.%u", (unsigned)(wpDist / 10), (unsigned)(wpDist % 10));
    lv_label_set_text(_lblWpDist, buf);

    // WP bearing (tenths → degrees)
    snprintf(buf, sizeof(buf), "%03d", wpBearing / 10);
    lv_label_set_text(_lblWpBrg, buf);
  }

private:
  static const uint32_t FRAME_MS = 33;
  static constexpr float TAU_MS = 60.0f;
  // Compass card geometry (screen coordinates)
  static const int CX = 238, CY = 98, R = 64;

  lv_obj_t* _parent = nullptr;
  lv_obj_t* _compass = nullptr;
  lv_obj_t* _lblHdg = nullptr;
  lv_obj_t* _bug = nullptr;
  lv_obj_t* _brgHead = nullptr;
  lv_obj_t* _brgTail = nullptr;
  lv_obj_t* _brgArrow = nullptr;
  lv_point_precise_t _bugPts[2] = {};
  lv_point_precise_t _brgHeadPts[2] = {};
  lv_point_precise_t _brgArrowPts[3] = {};
  lv_point_precise_t _brgTailPts[2] = {};
  SmoothedValue _heading{3600.0f};
  int16_t _hdgBug = 0, _wpBearing = 0;
  bool _hasWaypoint = false;
  bool _hasHeading = false;
  bool _compassDirty = true;
  uint32_t _lastTickMs = 0;
  int _shownHdg = -1;

  void createCompass(lv_obj_t* parent) {
    _compass = lv_scale_create(parent);
    lv_obj_set_size(_compass, 2 * R, 2 * R);
    lv_obj_set_pos(_compass, CX - R, CY - R);
    lv_obj_remove_flag(_compass, LV_OBJ_FLAG_CLICKABLE);
    lv_scale_set_mode(_compass, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_range(_compass, 0, 360);
    lv_scale_set_angle_range(_compass, 360);
    lv_scale_set_total_tick_count(_compass, 37);   // every 10°
    lv_scale_set_major_tick_every(_compass, 3);    // labelled every 30°
    lv_scale_set_label_show(_compass, true);
    static const char* LABELS[] = {"N", "3", "6", "E", "12", "15", "S", "21", "24", "W", "30", "33", "", NULL};
    lv_scale_set_text_src(_compass, LABELS);

    lv_obj_set_style_bg_opa(_compass, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(_compass, lv_color_make(18, 22, 30), 0);
    lv_obj_set_style_radius(_compass, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_compass, 2, 0);
    lv_obj_set_style_border_color(_compass, lv_color_make(70, 70, 70), 0);
    lv_obj_set_style_arc_width(_compass, 0, LV_PART_MAIN);
    lv_obj_set_style_length(_compass, 5, LV_PART_ITEMS);
    lv_obj_set_style_length(_compass, 9, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(_compass, lv_color_make(150, 150, 150), LV_PART_ITEMS);
    lv_obj_set_style_line_width(_compass, 1, LV_PART_ITEMS);
    lv_obj_set_style_line_color(_compass, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_line_width(_compass, 2, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(_compass, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_text_font(_compass, &lv_font_montserrat_10, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(_compass, 3, 0);

    _brgTail = makeNeedle(parent, _brgTailPts, lv_color_make(255, 80, 255), 3);
    _brgHead = makeNeedle(parent, _brgHeadPts, lv_color_make(255, 80, 255), 3);
    _brgArrow = makeNeedle(parent, _brgArrowPts, lv_color_make(255, 80, 255), 3);
    lv_line_set_points(_brgArrow, _brgArrowPts, 3);
    _bug = makeNeedle(parent, _bugPts, lv_color_make(0, 180, 255), 6);

    // Lubber line: the aircraft's nose, fixed at the top of the card
    static lv_point_precise_t lubber[2] = {{CX, CY - R - 4}, {CX, CY - R + 12}};
    lv_obj_t* l = lv_line_create(parent);
    lv_line_set_points(l, lubber, 2);
    lv_obj_set_style_line_color(l, lv_color_make(255, 220, 0), 0);
    lv_obj_set_style_line_width(l, 3, 0);
    lv_obj_set_style_line_rounded(l, true, 0);

    _lblHdg = lv_label_create(parent);
    lv_obj_set_style_text_color(_lblHdg, lv_color_white(), 0);
    lv_obj_set_style_text_font(_lblHdg, &lv_font_montserrat_18, 0);
    lv_obj_set_width(_lblHdg, 50);
    lv_obj_set_style_text_align(_lblHdg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(_lblHdg, CX - 25, CY - 10);
    lv_label_set_text(_lblHdg, "000");

    _heading.setTarget(0, true);
    layoutCompass();
  }

  static lv_obj_t* makeNeedle(lv_obj_t* parent, lv_point_precise_t* pts, lv_color_t color, int width) {
    lv_obj_t* line = lv_line_create(parent);
    lv_line_set_points(line, pts, 2);
    lv_obj_set_style_line_color(line, color, 0);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
    return line;
  }

  /// Point at `radius` from the centre in the direction of `bearingTenths`,
  /// with the current heading at the top of the card.
  /// `side` shifts the point sideways (perpendicular to the bearing), in px.
  void polar(float bearingTenths, float radius, lv_point_precise_t& p, float side = 0.0f) const {
    float a = (bearingTenths - _heading.value()) / 10.0f * (float)(M_PI / 180.0);
    float s = sinf(a), c = cosf(a);
    p.x = (lv_value_precise_t)lroundf(CX + radius * s + side * c);
    p.y = (lv_value_precise_t)lroundf(CY - radius * c + side * s);
  }

  void layoutCompass() {
    _compassDirty = false;
    float hdg = _heading.value();
    // The scale's value 0 sits at `rotation` degrees (0 = 3 o'clock, clockwise)
    int rot = ((270 - (int)lroundf(hdg / 10.0f)) % 360 + 360) % 360;
    lv_scale_set_rotation(_compass, rot);

    polar(_hdgBug, R - 3, _bugPts[0]);
    polar(_hdgBug, R - 11, _bugPts[1]);
    lv_line_set_points(_bug, _bugPts, 2);

    // Bearing pointer: arrow towards the waypoint, short tail opposite,
    // both kept clear of the heading readout in the middle
    lv_obj_t* parts[] = {_brgHead, _brgArrow, _brgTail};
    for (lv_obj_t* o : parts) {
      if (_hasWaypoint) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
    if (_hasWaypoint) {
      polar(_wpBearing, R - 14, _brgHeadPts[0]);
      polar(_wpBearing, 22, _brgHeadPts[1]);
      polar(_wpBearing, R - 22, _brgArrowPts[0], -6.0f);
      polar(_wpBearing, R - 14, _brgArrowPts[1]);
      polar(_wpBearing, R - 22, _brgArrowPts[2], 6.0f);
      polar(_wpBearing + 1800, 22, _brgTailPts[0]);
      polar(_wpBearing + 1800, R - 38, _brgTailPts[1]);
      lv_line_set_points(_brgHead, _brgHeadPts, 2);
      lv_line_set_points(_brgArrow, _brgArrowPts, 3);
      lv_line_set_points(_brgTail, _brgTailPts, 2);
    }

    int shown = ((int)lroundf(hdg / 10.0f) % 360 + 360) % 360;
    if (shown != _shownHdg) {
      _shownHdg = shown;
      char buf[8];
      snprintf(buf, sizeof(buf), "%03d", shown);
      lv_label_set_text(_lblHdg, buf);
    }
  }

  int32_t _prevLat = INT32_MIN, _prevLon = INT32_MIN;
  int16_t _prevHdgBug = INT16_MIN, _prevWpBearing = INT16_MIN;
  uint16_t _prevWpDist = UINT16_MAX;
  lv_obj_t* _lblLat = nullptr;
  lv_obj_t* _lblLon = nullptr;
  lv_obj_t* _lblHdgBug = nullptr;
  lv_obj_t* _lblWpDist = nullptr;
  lv_obj_t* _lblWpBrg = nullptr;
};

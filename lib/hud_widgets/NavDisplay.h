#pragma once
#include <lvgl.h>
#include <stdio.h>
#include "hud_proto.h"

/// Navigation display: lat/lon, heading bug, waypoint distance/bearing.
class NavDisplay {
public:
  NavDisplay() = default;

  void create(lv_obj_t* parent) {
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
    lv_obj_set_pos(wpTitle, 190, 25);

    _lblWpDist = lv_label_create(parent);
    lv_obj_set_style_text_color(_lblWpDist, lv_color_make(200, 200, 0), 0);
    lv_obj_set_style_text_font(_lblWpDist, &lv_font_montserrat_28, 0);
    lv_label_set_text(_lblWpDist, "0.0");
    lv_obj_set_pos(_lblWpDist, 190, 38);

    lv_obj_t* nmLabel = lv_label_create(parent);
    lv_obj_set_style_text_color(nmLabel, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(nmLabel, &lv_font_montserrat_10, 0);
    lv_label_set_text(nmLabel, "NM");
    lv_obj_set_pos(nmLabel, 190, 72);

    // Waypoint bearing
    lv_obj_t* brgTitle = lv_label_create(parent);
    lv_obj_set_style_text_color(brgTitle, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(brgTitle, &lv_font_montserrat_10, 0);
    lv_label_set_text(brgTitle, "WPT BRG");
    lv_obj_set_pos(brgTitle, 190, 95);

    _lblWpBrg = lv_label_create(parent);
    lv_obj_set_style_text_color(_lblWpBrg, lv_color_make(200, 200, 0), 0);
    lv_obj_set_style_text_font(_lblWpBrg, &lv_font_montserrat_28, 0);
    lv_label_set_text(_lblWpBrg, "000");
    lv_obj_set_pos(_lblWpBrg, 190, 108);
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
  int32_t _prevLat = INT32_MIN, _prevLon = INT32_MIN;
  int16_t _prevHdgBug = INT16_MIN, _prevWpBearing = INT16_MIN;
  uint16_t _prevWpDist = UINT16_MAX;
  lv_obj_t* _lblLat = nullptr;
  lv_obj_t* _lblLon = nullptr;
  lv_obj_t* _lblHdgBug = nullptr;
  lv_obj_t* _lblWpDist = nullptr;
  lv_obj_t* _lblWpBrg = nullptr;
};

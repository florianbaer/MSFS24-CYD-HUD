/**
 * LVGL 9.x configuration for the MSFS HUD.
 *
 * Arduino IDE / arduino-cli: copy this file next to the lvgl library folder
 * (e.g. Arduino/libraries/lv_conf.h, NOT inside lvgl/).
 * PlatformIO and the screenshot tool pick it up from here directly.
 *
 * Anything not set here falls back to the LVGL defaults.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

/* Memory for all widgets of all 7 screens (~52 KB at peak with animations).
 * On the ESP32 LVGL allocates from the system heap: a fixed pool would sit in
 * static RAM, and the WiFi build has no room left there for one big enough.
 * The desktop screenshot tool keeps LVGL's own pool so it can report usage. */
#ifdef ARDUINO
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#else
#define LV_MEM_SIZE (64U * 1024U)
#endif

/* Render dirty areas up to ~60 times a second. Only changed regions are
 * redrawn, so static screens cost nothing; the gyro paces itself (~40 fps). */
#define LV_DEF_REFR_PERIOD 16
#define LV_USE_LOG 0

/* Every font size the widgets reference must be enabled. */
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#endif

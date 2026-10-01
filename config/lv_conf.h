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

/* LVGL's own heap: all widgets of all 7 screens live in here. */
#define LV_MEM_SIZE (64U * 1024U)

#define LV_DEF_REFR_PERIOD 33
#define LV_USE_LOG 0

/* Every font size the widgets reference must be enabled. */
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#endif

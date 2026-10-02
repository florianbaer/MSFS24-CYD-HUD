#pragma once
#include <lvgl.h>

/// Gauge motion shared by all screens: arcs and bars glide to each new value
/// instead of jumping, so 20 Hz telemetry looks continuous.
///
/// Every new sample restarts a short ease-out from wherever the gauge is now,
/// which behaves like a low-pass filter: smooth, no overshoot, ~100 ms lag.
namespace hud {

/// Off by default so desktop renders (tools/screenshots) show final values;
/// the firmware switches it on in setup().
inline bool animate = false;
inline uint32_t animMs = 120;

namespace detail {
inline void arcExec(void* obj, int32_t v) { lv_arc_set_value((lv_obj_t*)obj, v); }
inline void barExec(void* obj, int32_t v) { lv_bar_set_value((lv_obj_t*)obj, v, LV_ANIM_OFF); }

inline void glide(void* obj, lv_anim_exec_xcb_t exec, int32_t from, int32_t to) {
  if (!animate || from == to) {
    lv_anim_delete(obj, exec);
    exec(obj, to);
    return;
  }
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_exec_cb(&a, exec);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_duration(&a, animMs);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);  // replaces a running animation of the same gauge
}
}  // namespace detail

inline void arcTo(lv_obj_t* arc, int32_t value) {
  detail::glide(arc, detail::arcExec, lv_arc_get_value(arc), value);
}

inline void barTo(lv_obj_t* bar, int32_t value) {
  detail::glide(bar, detail::barExec, lv_bar_get_value(bar), value);
}

/// Glides any value: `exec(var, v)` draws it, `from` is what is shown now.
inline void valueTo(void* var, lv_anim_exec_xcb_t exec, int32_t from, int32_t to) {
  detail::glide(var, exec, from, to);
}

}  // namespace hud

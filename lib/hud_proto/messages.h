#pragma once
#include <stdint.h>

enum MsgType : uint8_t {
  MSG_ATTITUDE    = 0x02,
  MSG_ENGINE      = 0x03,
  MSG_FLIGHT_DATA = 0x04,
  MSG_GFORCE      = 0x05,
  MSG_ALERTS      = 0x06,
  MSG_NAV_DATA    = 0x07,
  MSG_CONFIG      = 0x08,
  MSG_AUTOPILOT   = 0x09,
};

/// Attitude message payload (PC -> ESP32).
struct __attribute__((packed)) AttitudeMsg {
  int16_t pitch;    // tenths of degrees, -1800..+1800
  int16_t roll;     // tenths of degrees, -1800..+1800
  int16_t heading;  // tenths of degrees, 0..3599
};

/// Engine gauges message payload (PC -> ESP32).
struct __attribute__((packed)) EngineMsg {
  uint8_t  engine_idx;  // 0-based engine index (0..3)
  uint16_t rpm;
  uint8_t  throttle;    // 0-100 %
  uint8_t  fuel_flow;   // 0-255 mapped
  uint8_t  oil_temp;    // 0-255 mapped
  uint8_t  oil_press;   // 0-255 mapped
};

/// Flight data message payload (PC -> ESP32).
struct __attribute__((packed)) FlightDataMsg {
  uint16_t airspeed;     // tenths of knots
  int32_t  altitude;     // feet
  int16_t  vspeed;       // fpm
  uint16_t ground_speed; // tenths of knots
};

/// G-force message payload (PC -> ESP32).
struct __attribute__((packed)) GForceMsg {
  int16_t gforce_x;  // hundredths of G (longitudinal)
  int16_t gforce_y;  // hundredths of G (vertical, ~100 = 1G level)
  int16_t gforce_z;  // hundredths of G (lateral)
};

/// Alert flags payload (PC -> ESP32).
/// Bit 0: stall, 1: overspeed, 2: gear_unsafe, 3: low_fuel, 4: engine_fire, 5: ap_disconnect
struct __attribute__((packed)) AlertsMsg {
  uint16_t flags;
};

/// Navigation data payload (PC -> ESP32).
struct __attribute__((packed)) NavDataMsg {
  int32_t  lat;         // degrees * 1e7
  int32_t  lon;         // degrees * 1e7
  int16_t  hdg_bug;     // tenths of degrees, 0..3599
  uint16_t wp_dist;     // nautical miles * 10
  int16_t  wp_bearing;  // tenths of degrees, 0..3599
};

/// Aircraft configuration payload (PC -> ESP32).
struct __attribute__((packed)) ConfigMsg {
  uint8_t flaps_pct;    // 0-100
  uint8_t gear_state;   // 0=up, 1=transit, 2=down
  int8_t  elev_trim;    // -100..+100
  int8_t  rudder_trim;  // -100..+100
};

/// Autopilot status payload (PC -> ESP32).
/// mode_flags bits: 0=master, 1=hdg_lock, 2=alt_lock, 3=vs_lock, 4=nav_lock, 5=apr_lock
struct __attribute__((packed)) AutopilotMsg {
  uint16_t mode_flags;
  int32_t  target_alt;  // feet
  int16_t  target_hdg;  // tenths of degrees
};

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
  MSG_ECAM_ENGINE = 0x0A,
  MSG_ECAM_STATUS = 0x0B,
  MSG_COMMAND     = 0x20,  // ESP32 -> PC: a control on the display was used
};

/// Commands the display sends when a control is tapped (CommandMsg.command).
/// The sender turns them into simulator events.
enum HudCommand : uint8_t {
  CMD_AP_MASTER = 1,   // toggle autopilot master
  CMD_AP_HDG    = 2,   // toggle heading hold
  CMD_AP_ALT    = 3,   // toggle altitude hold
  CMD_AP_VS     = 4,   // toggle vertical speed hold
  CMD_AP_NAV    = 5,   // toggle NAV1 hold
  CMD_AP_APR    = 6,   // toggle approach hold
  CMD_HDG_INC   = 7,   // heading bug +1°
  CMD_HDG_DEC   = 8,   // heading bug -1°
  CMD_ALT_INC   = 9,   // autopilot altitude + one step (usually 100 ft)
  CMD_ALT_DEC   = 10,  // autopilot altitude - one step
};

/// Command payload (ESP32 -> PC).
struct __attribute__((packed)) CommandMsg {
  uint8_t command;  // HudCommand
};

/// Attitude message payload (PC -> ESP32).
struct __attribute__((packed)) AttitudeMsg {
  int16_t pitch;    // tenths of degrees, -1800..+1800, positive = nose up
  int16_t roll;     // tenths of degrees, -1800..+1800, positive = left wing down
  int16_t heading;  // tenths of degrees, 0..3599, magnetic
};

/// Engine gauges message payload (PC -> ESP32).
struct __attribute__((packed)) EngineMsg {
  uint8_t  engine_idx;  // 0-based engine index (0..3)
  uint16_t rpm;
  uint8_t  throttle;    // 0-100 %
  uint8_t  fuel_flow;   // 0-255 = 0..50 GPH
  uint8_t  oil_temp;    // 0-255 = 0..250 °F
  uint8_t  oil_press;   // 0-255 = 0..100 PSI
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
  int16_t gforce_y;  // hundredths of G (vertical load factor, 100 = 1G level)
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

/// Turbine engine payload for the ECAM screen (PC -> ESP32), one per engine.
struct __attribute__((packed)) EcamEngineMsg {
  uint8_t  engine_idx;  // 0-based
  uint16_t n1;          // tenths of %
  uint16_t n2;          // tenths of %
  int16_t  egt;         // degrees C
  uint16_t fuel_flow;   // kg/h
};

/// ECAM memo flags (EcamStatusMsg.memo_flags)
enum EcamMemo : uint16_t {
  MEMO_PARK_BRK     = 1 << 0,
  MEMO_SPEED_BRK    = 1 << 1,  // speed brakes extended
  MEMO_SPLRS_ARMED  = 1 << 2,  // ground spoilers armed
  MEMO_SEAT_BELTS   = 1 << 3,
  MEMO_APU_AVAIL    = 1 << 4,
  MEMO_ENG_ANTI_ICE = 1 << 5,
  MEMO_LDG_LT       = 1 << 6,
};

/// Aircraft status for the ECAM screen (PC -> ESP32).
struct __attribute__((packed)) EcamStatusMsg {
  uint32_t fob;          // fuel on board, kg
  uint8_t  flaps_index;  // flaps handle detent, 0 = up
  uint8_t  slats_pct;    // leading-edge position 0..100
  uint8_t  flaps_pct;    // trailing-edge position 0..100
  uint16_t memo_flags;   // EcamMemo bits
};

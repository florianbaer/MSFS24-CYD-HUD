#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/// Text commands that configure the display over USB serial without
/// re-flashing it (the installer sends them). One command per line:
///
///   HUDCFG INFO                               -> HUDCFG INFO fw=... wifi=...
///   HUDCFG WIFI <ssid hex> <password hex>     -> HUDCFG OK   (stored in flash)
///   HUDCFG WIFI-OFF                           -> HUDCFG OK
///
/// The SSID and password are sent as the hex of their UTF-8 bytes, so spaces
/// and any other character survive the line format. An open network has an
/// empty password, written as "-".
///
/// Telemetry frames arrive on the same serial line. LineCollector only keeps
/// lines made of printable ASCII, so binary frame bytes never form a command;
/// the 0x00 that ends every frame starts a fresh line.

enum class CfgCmd : uint8_t { None, Info, Wifi, WifiOff, Invalid };

struct CfgRequest {
  CfgCmd cmd = CfgCmd::None;
  char ssid[33] = {};  // 802.11: at most 32 bytes
  char pass[64] = {};  // WPA2: at most 63 characters
};

/// Collects printable ASCII lines from a byte stream that also carries binary data.
class LineCollector {
public:
  /// Returns true when a complete line is available in line().
  bool feed(uint8_t c) {
    if (c == 0x00) {  // frame delimiter: whatever follows starts a fresh line
      _len = 0;
      _dirty = false;
      return false;
    }
    if (c == '\n' || c == '\r') {
      bool ready = _len > 0 && !_dirty;
      _buf[_len] = '\0';
      _len = 0;
      _dirty = false;
      return ready;
    }
    if (c < 0x20 || c > 0x7E) {  // binary telemetry: this line is not a command
      _dirty = true;
      return false;
    }
    if (_len < sizeof(_buf) - 1) _buf[_len++] = (char)c;
    else _dirty = true;          // too long to be a command
    return false;
  }

  const char* line() const { return _buf; }

private:
  char _buf[256] = {};
  size_t _len = 0;
  bool _dirty = false;
};

namespace cfg_detail {

inline int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/// Decodes `len` hex characters into a NUL-terminated string of at most max-1 bytes.
inline bool hexDecode(const char* hex, size_t len, char* out, size_t max) {
  if (len == 1 && hex[0] == '-') { out[0] = '\0'; return true; }
  if (len % 2 != 0 || len / 2 >= max) return false;
  for (size_t i = 0; i < len / 2; i++) {
    int hi = hexValue(hex[2 * i]), lo = hexValue(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    char ch = (char)((hi << 4) | lo);
    if (ch == '\0') return false;
    out[i] = ch;
  }
  out[len / 2] = '\0';
  return true;
}

}  // namespace cfg_detail

/// Parses one line. Lines that are not HUDCFG commands give CfgCmd::None.
inline CfgRequest parseConfigLine(const char* line) {
  CfgRequest r;
  static const char PREFIX[] = "HUDCFG ";
  if (strncmp(line, PREFIX, sizeof(PREFIX) - 1) != 0) return r;
  const char* p = line + sizeof(PREFIX) - 1;

  if (strcmp(p, "INFO") == 0) { r.cmd = CfgCmd::Info; return r; }
  if (strcmp(p, "WIFI-OFF") == 0) { r.cmd = CfgCmd::WifiOff; return r; }

  r.cmd = CfgCmd::Invalid;
  if (strncmp(p, "WIFI ", 5) != 0) return r;
  p += 5;
  const char* space = strchr(p, ' ');
  if (!space || space == p || strchr(space + 1, ' ')) return r;
  const char* pass = space + 1;
  if (!cfg_detail::hexDecode(p, (size_t)(space - p), r.ssid, sizeof(r.ssid))) return r;
  if (r.ssid[0] == '\0') return r;  // an SSID is required
  if (!cfg_detail::hexDecode(pass, strlen(pass), r.pass, sizeof(r.pass))) return r;
  r.cmd = CfgCmd::Wifi;
  return r;
}

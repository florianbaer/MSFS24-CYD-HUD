// Host-side test for lib/hud_proto/config_command.h (USB configuration commands).
//
//   c++ -std=c++17 -Ilib/hud_proto tests/proto/config_command_test.cpp -o config_command_test && ./config_command_test

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "hud_proto.h"

static int failures = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
      failures++;                                                       \
    }                                                                   \
  } while (0)

static std::string hex(const std::string& s) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  for (unsigned char c : s) { out += digits[c >> 4]; out += digits[c & 15]; }
  return out;
}

/// Feeds bytes through a LineCollector and returns every completed line.
static std::vector<std::string> lines(const std::vector<uint8_t>& bytes) {
  LineCollector lc;
  std::vector<std::string> out;
  for (uint8_t b : bytes) if (lc.feed(b)) out.push_back(lc.line());
  return out;
}

static std::vector<uint8_t> bytesOf(const std::string& s) { return {s.begin(), s.end()}; }

// Minimal COBS encoder for building test frames (the firmware only decodes)
static size_t cobsEncode(const uint8_t* src, size_t len, uint8_t* dst) {
  size_t code = 0, out = 1;
  uint8_t run = 1;
  for (size_t i = 0; i < len; i++) {
    if (src[i] == 0) { dst[code] = run; code = out++; run = 1; }
    else { dst[out++] = src[i]; run++; }
  }
  dst[code] = run;
  return out;
}

static void parsesCommands() {
  CHECK(parseConfigLine("HUDCFG INFO").cmd == CfgCmd::Info);
  CHECK(parseConfigLine("HUDCFG WIFI-OFF").cmd == CfgCmd::WifiOff);
  CHECK(parseConfigLine("hello").cmd == CfgCmd::None);
  CHECK(parseConfigLine("HUDCFG").cmd == CfgCmd::None);
  CHECK(parseConfigLine("HUDCFG REBOOT").cmd == CfgCmd::Invalid);
}

static void wifiCredentialsRoundTrip() {
  // Spaces, quotes and UTF-8 survive because they travel as hex
  std::string ssid = "My \"Home\" WLAN \xc3\xa4";
  std::string pass = "p@ss word\\with\ttab";
  CfgRequest r = parseConfigLine(("HUDCFG WIFI " + hex(ssid) + " " + hex(pass)).c_str());
  CHECK(r.cmd == CfgCmd::Wifi);
  CHECK(ssid == r.ssid);
  CHECK(pass == r.pass);
}

static void openNetworkHasEmptyPassword() {
  CfgRequest r = parseConfigLine(("HUDCFG WIFI " + hex("Cafe") + " -").c_str());
  CHECK(r.cmd == CfgCmd::Wifi);
  CHECK(std::string(r.pass).empty());
}

static void rejectsBadWifiArguments() {
  CHECK(parseConfigLine("HUDCFG WIFI").cmd == CfgCmd::Invalid);
  CHECK(parseConfigLine(("HUDCFG WIFI " + hex("ssid")).c_str()).cmd == CfgCmd::Invalid);           // no password
  CHECK(parseConfigLine("HUDCFG WIFI - -").cmd == CfgCmd::Invalid);                               // empty SSID
  CHECK(parseConfigLine("HUDCFG WIFI 6g 6161").cmd == CfgCmd::Invalid);                          // not hex
  CHECK(parseConfigLine("HUDCFG WIFI 616 6161").cmd == CfgCmd::Invalid);                         // odd length
  CHECK(parseConfigLine("HUDCFG WIFI 6100 6161").cmd == CfgCmd::Invalid);                        // embedded NUL
  CHECK(parseConfigLine(("HUDCFG WIFI " + hex(std::string(33, 'a')) + " -").c_str()).cmd == CfgCmd::Invalid);  // SSID > 32
  CHECK(parseConfigLine(("HUDCFG WIFI " + hex("a") + " " + hex(std::string(64, 'p'))).c_str()).cmd == CfgCmd::Invalid);  // pass > 63
  CHECK(parseConfigLine(("HUDCFG WIFI " + hex(std::string(32, 'a')) + " " + hex(std::string(63, 'p'))).c_str()).cmd == CfgCmd::Wifi);
}

static void collectsLinesBetweenTelemetry() {
  // A telemetry frame, a command, another frame, a command with CRLF
  std::vector<uint8_t> stream;
  uint8_t frame[] = {0x00, 0x05, 0x02, 0x41, 0x0a, 0x7f, 0x00};  // contains '\n' and printable bytes
  stream.insert(stream.end(), frame, frame + sizeof(frame));
  auto cmd1 = bytesOf("HUDCFG INFO\n");
  stream.insert(stream.end(), cmd1.begin(), cmd1.end());
  stream.insert(stream.end(), frame, frame + sizeof(frame));
  auto cmd2 = bytesOf("HUDCFG WIFI-OFF\r\n");
  stream.insert(stream.end(), cmd2.begin(), cmd2.end());

  auto got = lines(stream);
  CHECK(got.size() == 2);
  if (got.size() == 2) {
    CHECK(got[0] == "HUDCFG INFO");
    CHECK(got[1] == "HUDCFG WIFI-OFF");
  }
}

static void binaryNeverFormsACommand() {
  // Printable bytes glued to binary ones on the same line are dropped
  std::vector<uint8_t> stream = {0x01};
  auto cmd = bytesOf("HUDCFG INFO\n");
  stream.insert(stream.end(), cmd.begin(), cmd.end());
  CHECK(lines(stream).empty());
}

static void overlongLinesAreDropped() {
  std::string longLine = "HUDCFG WIFI " + std::string(300, 'a') + "\nHUDCFG INFO\n";
  auto got = lines(bytesOf(longLine));
  CHECK(got.size() == 1 && got[0] == "HUDCFG INFO");
}

static void decoderStillDecodesAroundCommands() {
  // The frame decoder sees the command bytes too and must resync afterwards
  FrameDecoder d;
  uint8_t att[] = {0x02, 0x10, 0x00, 0x20, 0x00, 0x30, 0x00};  // type + pitch/roll/heading
  uint8_t raw[8];
  memcpy(raw, att, 7);
  raw[7] = crc8(raw, 7);
  uint8_t enc[16];
  size_t n = cobsEncode(raw, 8, enc);
  std::vector<uint8_t> stream = bytesOf("HUDCFG INFO\n");
  stream.push_back(0x00);
  stream.insert(stream.end(), enc, enc + n);
  stream.push_back(0x00);
  int frames = 0;
  for (uint8_t b : stream) {
    d.feed(b);
    if (d.available()) { frames += d.msgType() == MSG_ATTITUDE; d.clear(); }
  }
  CHECK(frames == 1);
}

int main() {
  parsesCommands();
  wifiCredentialsRoundTrip();
  openNetworkHasEmptyPassword();
  rejectsBadWifiArguments();
  collectsLinesBetweenTelemetry();
  binaryNeverFormsACommand();
  overlongLinesAreDropped();
  decoderStillDecodesAroundCommands();
  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("config commands: all tests passed\n");
  return 0;
}

// Host-side test for lib/hud_proto (the ESP32 receive path).
//
//   c++ -std=c++17 -Ilib/hud_proto tests/proto/decoder_test.cpp -o decoder_test && ./decoder_test
//
// The golden frames are the same bytes the C# sender tests assert on
// (msfs-sender/MsfsHudSender.Tests/FrameTests.cs), so both ends are pinned
// to one wire format.

#include <cstdio>
#include <cstring>
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

static_assert(sizeof(AttitudeMsg) == 6, "wire size");
static_assert(sizeof(EngineMsg) == 7, "wire size");
static_assert(sizeof(FlightDataMsg) == 10, "wire size");
static_assert(sizeof(GForceMsg) == 6, "wire size");
static_assert(sizeof(AlertsMsg) == 2, "wire size");
static_assert(sizeof(NavDataMsg) == 14, "wire size");
static_assert(sizeof(ConfigMsg) == 4, "wire size");
static_assert(sizeof(AutopilotMsg) == 8, "wire size");
static_assert(sizeof(EcamEngineMsg) == 9, "wire size");
static_assert(sizeof(EcamStatusMsg) == 9, "wire size");

static std::vector<uint8_t> fromHex(const char* hex) {
  std::vector<uint8_t> out;
  for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) {
    unsigned v;
    sscanf(hex + i, "%2x", &v);
    out.push_back((uint8_t)v);
  }
  return out;
}

static std::vector<uint8_t> cobsEncode(const std::vector<uint8_t>& data) {
  std::vector<uint8_t> out(1, 0);
  size_t codeIdx = 0;
  uint8_t code = 1;
  for (uint8_t b : data) {
    if (b == 0) {
      out[codeIdx] = code;
      codeIdx = out.size();
      out.push_back(0);
      code = 1;
    } else {
      out.push_back(b);
      if (++code == 0xFF) {
        out[codeIdx] = code;
        codeIdx = out.size();
        out.push_back(0);
        code = 1;
      }
    }
  }
  out[codeIdx] = code;
  return out;
}

/// Build [0x00][COBS(type|payload|crc)][0x00] like the sender does.
static std::vector<uint8_t> buildFrame(uint8_t type, const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> raw(1, type);
  raw.insert(raw.end(), payload.begin(), payload.end());
  raw.push_back(crc8(raw.data(), raw.size()));
  std::vector<uint8_t> frame(1, 0x00);
  auto enc = cobsEncode(raw);
  frame.insert(frame.end(), enc.begin(), enc.end());
  frame.push_back(0x00);
  return frame;
}

static void feedAll(FrameDecoder& d, const std::vector<uint8_t>& bytes) {
  for (uint8_t b : bytes) d.feed(b);
}

static void testCrc() {
  const uint8_t one[] = {0x01}, ff[] = {0xFF};
  CHECK(crc8(nullptr, 0) == 0x00);
  CHECK(crc8(one, 1) == 0x31);
  CHECK(crc8(ff, 1) == 0xAC);
}

static void testGoldenAttitude() {
  FrameDecoder d;
  feedAll(d, fromHex("000902c201d4fe8c0a6200"));
  CHECK(d.available());
  CHECK(d.msgType() == MSG_ATTITUDE);
  uint8_t buf[64];
  CHECK(d.payload(buf, sizeof(buf)) == (int)sizeof(AttitudeMsg));
  AttitudeMsg msg;
  memcpy(&msg, buf, sizeof(msg));
  CHECK(msg.pitch == 450);
  CHECK(msg.roll == -300);
  CHECK(msg.heading == 2700);
}

static void testGoldenFlightData() {
  FrameDecoder d;
  feedAll(d, fromHex("000d04e204ceffffff24fa7805c700"));
  CHECK(d.available());
  CHECK(d.msgType() == MSG_FLIGHT_DATA);
  uint8_t buf[64];
  CHECK(d.payload(buf, sizeof(buf)) == (int)sizeof(FlightDataMsg));
  FlightDataMsg msg;
  memcpy(&msg, buf, sizeof(msg));
  CHECK(msg.airspeed == 1250);
  CHECK(msg.altitude == -50);
  CHECK(msg.vspeed == -1500);
  CHECK(msg.ground_speed == 1400);
}

static void testGoldenGForce() {
  FrameDecoder d;
  feedAll(d, fromHex("000505f1ff780205023d00"));
  CHECK(d.available());
  CHECK(d.msgType() == MSG_GFORCE);
  uint8_t buf[64];
  d.payload(buf, sizeof(buf));
  GForceMsg msg;
  memcpy(&msg, buf, sizeof(msg));
  CHECK(msg.gforce_x == -15);
  CHECK(msg.gforce_y == 120);
  CHECK(msg.gforce_z == 5);
}

static void testGoldenEcam() {
  FrameDecoder d;
  uint8_t buf[64];
  feedAll(d, fromHex("000c0a013803a70367028004a100"));
  CHECK(d.available() && d.msgType() == MSG_ECAM_ENGINE);
  CHECK(d.payload(buf, sizeof(buf)) == (int)sizeof(EcamEngineMsg));
  EcamEngineMsg e;
  memcpy(&e, buf, sizeof(e));
  CHECK(e.engine_idx == 1 && e.n1 == 824 && e.n2 == 935 && e.egt == 615 && e.fuel_flow == 1152);
  d.clear();

  feedAll(d, fromHex("00040b60180105024b1e4902b300"));
  CHECK(d.available() && d.msgType() == MSG_ECAM_STATUS);
  CHECK(d.payload(buf, sizeof(buf)) == (int)sizeof(EcamStatusMsg));
  EcamStatusMsg st;
  memcpy(&st, buf, sizeof(st));
  CHECK(st.fob == 6240 && st.flaps_index == 2 && st.slats_pct == 75 && st.flaps_pct == 30);
  CHECK(st.memo_flags == (MEMO_PARK_BRK | MEMO_SEAT_BELTS | MEMO_LDG_LT));
}

static void testClearConsumesFrame() {
  FrameDecoder d;
  feedAll(d, fromHex("000902c201d4fe8c0a6200"));
  CHECK(d.available());
  d.clear();
  CHECK(!d.available());
  uint8_t buf[8];
  CHECK(d.payload(buf, sizeof(buf)) == 0);
}

static void testCorruptedFrameRejected() {
  FrameDecoder d;
  auto frame = fromHex("000902c201d4fe8c0a6200");
  frame[4] ^= 0x10; // flip a payload bit
  feedAll(d, frame);
  CHECK(!d.available());
}

static void testResyncAfterGarbage() {
  FrameDecoder d;
  feedAll(d, {0x17, 0x99, 0x42}); // tail of a frame we joined mid-stream
  feedAll(d, fromHex("000902c201d4fe8c0a6200"));
  CHECK(d.available());
  CHECK(d.msgType() == MSG_ATTITUDE);
}

static void testBackToBackFrames() {
  FrameDecoder d;
  int seen = 0;
  uint8_t types[2] = {0, 0};
  auto stream = fromHex("000902c201d4fe8c0a6200");
  auto second = fromHex("000505f1ff780205023d00");
  stream.insert(stream.end(), second.begin(), second.end());
  for (uint8_t b : stream) {
    d.feed(b);
    if (d.available()) {
      if (seen < 2) types[seen] = d.msgType();
      seen++;
      d.clear();
    }
  }
  CHECK(seen == 2);
  CHECK(types[0] == MSG_ATTITUDE);
  CHECK(types[1] == MSG_GFORCE);
}

static void testRoundTripAllZeroPayload() {
  FrameDecoder d;
  feedAll(d, buildFrame(MSG_ALERTS, {0x00, 0x00}));
  CHECK(d.available());
  CHECK(d.msgType() == MSG_ALERTS);
  uint8_t buf[8] = {0xAA, 0xAA};
  CHECK(d.payload(buf, sizeof(buf)) == 2);
  CHECK(buf[0] == 0 && buf[1] == 0);
}

// A frame longer than the receive buffer must be dropped as a whole, even when
// the part that fits is a well-formed frame with a matching CRC.
static void testOversizedFrameRejected() {
  std::vector<uint8_t> payload(69, 0x11);
  auto frame = buildFrame(MSG_ATTITUDE, payload);
  CHECK(frame.size() == 74); // delimiter + 72 encoded bytes + delimiter
  frame.insert(frame.end() - 1, {0x55, 0x66, 0x77}); // overrun before the delimiter

  FrameDecoder d;
  feedAll(d, frame);
  CHECK(!d.available());

  // ... and the decoder must recover on the next good frame
  feedAll(d, fromHex("0505f1ff780205023d00"));
  CHECK(d.available());
  CHECK(d.msgType() == MSG_GFORCE);
}

// Display -> PC command frames: the bytes the sender's FrameReader must accept
// (msfs-sender/MsfsHudSender.Tests/CommandFrameTests.cs asserts the same bytes)
static void commandFramesMatchGolden() {
  struct { uint8_t cmd; uint8_t bytes[6]; } golden[] = {
    {CMD_AP_MASTER, {0x00, 0x04, 0x20, 0x01, 0xed, 0x00}},
    {CMD_AP_ALT,    {0x00, 0x04, 0x20, 0x03, 0x8f, 0x00}},
    {CMD_ALT_DEC,   {0x00, 0x04, 0x20, 0x0a, 0x07, 0x00}},
  };
  for (auto& g : golden) {
    CommandMsg msg{g.cmd};
    uint8_t out[16];
    size_t n = encodeFrame(MSG_COMMAND, (const uint8_t*)&msg, sizeof(msg), out, sizeof(out));
    CHECK(n == 6);
    CHECK(memcmp(out, g.bytes, 6) == 0);
  }
  // ... and they round-trip through the firmware's own decoder
  FrameDecoder d;
  CommandMsg msg{CMD_HDG_INC};
  uint8_t out[16];
  size_t n = encodeFrame(MSG_COMMAND, (const uint8_t*)&msg, sizeof(msg), out, sizeof(out));
  int seen = 0;
  for (size_t i = 0; i < n; i++) {
    d.feed(out[i]);
    if (d.available()) {
      uint8_t p[4];
      seen += d.msgType() == MSG_COMMAND && d.payload(p, sizeof(p)) == 1 && p[0] == CMD_HDG_INC;
      d.clear();
    }
  }
  CHECK(seen == 1);
  // A buffer that is too small is refused
  CHECK(encodeFrame(MSG_COMMAND, (const uint8_t*)&msg, sizeof(msg), out, 5) == 0);
}

int main() {
  commandFramesMatchGolden();
  testCrc();
  testGoldenAttitude();
  testGoldenFlightData();
  testGoldenGForce();
  testGoldenEcam();
  testClearConsumesFrame();
  testCorruptedFrameRejected();
  testResyncAfterGarbage();
  testBackToBackFrames();
  testRoundTripAllZeroPayload();
  testOversizedFrameRejected();

  if (failures) {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("all protocol decoder tests passed\n");
  return 0;
}

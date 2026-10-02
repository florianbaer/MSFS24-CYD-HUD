#pragma once
#include <stddef.h>
#include <stdint.h>
#include "crc8.h"

/// Builds a wire frame, the same format the sender uses (FrameBuilder.cs):
///   [0x00] [COBS(msg_type | payload | CRC8)] [0x00]
/// Returns the frame length, or 0 if `out` is too small.
/// Room needed: payload_len + 5 (type, CRC, COBS code byte, two delimiters)
/// for payloads under 250 bytes.
static inline size_t encodeFrame(uint8_t msgType, const uint8_t* payload, size_t payloadLen,
                                 uint8_t* out, size_t outMax) {
  const size_t rawLen = payloadLen + 2;
  if (rawLen > 250 || outMax < rawLen + 3) return 0;

  uint8_t raw[252];
  raw[0] = msgType;
  for (size_t i = 0; i < payloadLen; i++) raw[1 + i] = payload[i];
  raw[rawLen - 1] = crc8(raw, rawLen - 1);

  // COBS: replace each zero by the distance to the next one
  size_t o = 0;
  out[o++] = 0x00;
  size_t codeAt = o++;
  uint8_t code = 1;
  for (size_t i = 0; i < rawLen; i++) {
    if (raw[i] == 0) {
      out[codeAt] = code;
      codeAt = o++;
      code = 1;
    } else {
      out[o++] = raw[i];
      code++;
    }
  }
  out[codeAt] = code;
  out[o++] = 0x00;
  return o;
}

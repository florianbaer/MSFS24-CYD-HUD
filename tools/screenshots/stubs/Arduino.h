#pragma once
// Minimal Arduino stand-in so the widget headers compile on a desktop.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define ESP_ARDUINO_VERSION_MAJOR 3

inline bool ledcAttach(uint8_t, uint32_t, uint8_t) { return true; }
inline bool ledcWrite(uint8_t, uint32_t) { return true; }

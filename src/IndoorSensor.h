#pragma once

#include <Arduino.h>

#include "boards.h"

struct IndoorReading {
  bool valid = false;
  float temperature = 0.0f;  // °C
  float humidity = 0.0f;     // %RH
};

// Reads the onboard temperature/humidity sensor. Returns an invalid reading on boards without one.
IndoorReading readIndoorSensor();

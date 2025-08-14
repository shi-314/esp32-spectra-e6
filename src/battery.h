#pragma once

#include <Arduino.h>

#define BATTERY_PIN 35

#define BATTERY_MAX_VOLTAGE 4.2
#define BATTERY_MIN_VOLTAGE 3.3
#define VOLTAGE_DIVIDER_RATIO 2.0 

String getBatteryStatus(); 
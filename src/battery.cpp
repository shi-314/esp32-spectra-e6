#include "battery.h"

String getBatteryStatus() {
  int rawValue = analogRead(BATTERY_PIN);
  // Convert to voltage (ESP32 ADC is 12-bit, 0-3.3V)
  float voltage = (rawValue * 3.3) / 4095.0;
  // Adjust for voltage divider
  voltage *= VOLTAGE_DIVIDER_RATIO;

  // LiPo battery specific calculation
  float percentage;
  if (voltage >= 4.2) {
    percentage = 100.0;
  } else if (voltage <= 3.0) {
    percentage = 0.0;
  } else if (voltage >= 3.7) {
    // Upper range (3.7V to 4.2V) - more gradual change
    percentage = 100.0 * (voltage - 3.7) / (4.2 - 3.7) + 80.0;
  } else {
    // Lower range (3.0V to 3.7V) - steeper change
    percentage = 80.0 * (voltage - 3.0) / (3.7 - 3.0);
  }

  // Ensure percentage is between 0 and 100
  percentage = constrain(percentage, 0, 100);

  return String((int)percentage) + "%";
}
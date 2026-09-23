#include "IndoorSensor.h"

#ifdef SHT4X_I2C_ADDRESS

#include <Wire.h>

namespace {
// SHT4x high precision measurement, takes about 9ms
const uint8_t SHT4X_MEASURE_HIGH_PRECISION = 0xFD;
const uint32_t SHT4X_MEASUREMENT_DELAY_MS = 12;
}  // namespace

IndoorReading readIndoorSensor() {
  IndoorReading reading;

  Wire.begin(SHT4X_I2C_SDA, SHT4X_I2C_SCL);

  Wire.beginTransmission(SHT4X_I2C_ADDRESS);
  Wire.write(SHT4X_MEASURE_HIGH_PRECISION);
  if (Wire.endTransmission() != 0) {
    Serial.println("Indoor sensor did not acknowledge measurement command");
    return reading;
  }

  delay(SHT4X_MEASUREMENT_DELAY_MS);

  // Two 16 bit values, each followed by a CRC byte we do not verify
  const uint8_t expectedBytes = 6;
  if (Wire.requestFrom((uint8_t)SHT4X_I2C_ADDRESS, expectedBytes) != expectedBytes) {
    Serial.println("Indoor sensor returned no measurement");
    return reading;
  }

  uint8_t data[expectedBytes];
  for (uint8_t i = 0; i < expectedBytes; i++) {
    data[i] = Wire.read();
  }

  uint16_t temperatureTicks = (data[0] << 8) | data[1];
  uint16_t humidityTicks = (data[3] << 8) | data[4];

  reading.temperature = -45.0f + 175.0f * temperatureTicks / 65535.0f;
  reading.humidity = constrain(-6.0f + 125.0f * humidityTicks / 65535.0f, 0.0f, 100.0f);
  reading.valid = true;

  Serial.printf("Indoor sensor: %.1f °C, %.0f %%RH\n", reading.temperature, reading.humidity);
  return reading;
}

#else

IndoorReading readIndoorSensor() { return IndoorReading(); }

#endif

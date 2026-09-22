#pragma once

// Seeed Studio reTerminal E1002 (XIAO ESP32-S3, 7.3" E Ink Spectra 6, GDEP073E01)
// https://wiki.seeedstudio.com/reterminal_e10xx_with_arduino/

#define BOARD_NAME "reTerminal E1002"

// Display driver and orientation
#define EPD_DRIVER GxEPD2_730c_GDEP073E01
#define BOARD_DISPLAY_ROTATION 0
#define EPD_BUSY_TIMEOUT_US 60000000  // A full refresh takes ~25-30s, above GxEPD2's 20s default

// E-paper control pins
#define EPD_CS 10
#define EPD_DC 11
#define EPD_RSET 12
#define EPD_BUSY 13

// SPI pins for e-paper (SCK/MOSI are shared with the microSD slot)
#define EPD_MOSI 9
#define EPD_MISO (-1)
#define EPD_SCLK 7
#define EPD_SPI_HOST HSPI           // Dedicated bus and 2MHz clock, as in Seeed's reference setup
#define EPD_SPI_FREQUENCY 2000000

// microSD slot shares SCK/MOSI with the e-paper. Its pull-ups sit on the switched SD rail, so with
// the rail off they drag the shared lines down and the panel never refreshes. Power the rail and
// keep the card deselected before talking to the display.
#define SD_POWER_PIN 16
#define SD_CS_PIN 14

// Green refresh button (KEY0, active low with external pull-up) wakes the device from deep sleep
#define WAKE_BUTTON_PIN 3

// Battery monitoring: the divider is only powered while the enable pin is high
#define BATTERY_PIN 1
#define BATTERY_ENABLE_PIN 21
#define VOLTAGE_DIVIDER_RATIO 2.0

// LED configuration (user LED on GPIO6)
#define LED_PIN 6
#define LED_ON (LOW)  // LED active low

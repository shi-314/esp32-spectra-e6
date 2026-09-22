#pragma once

// LilyGO T7-S3 with Waveshare 7.3" e-Paper HAT (E) (Spectra 6, GDEP073E01)

#define BOARD_NAME "LilyGO T7-S3"

// Display driver and orientation
#define EPD_DRIVER GxEPD2_730c_GDEP073E01
#define BOARD_DISPLAY_ROTATION 2

// E-paper control pins
#define EPD_CS 10    // Chip Select for SPI communication
#define EPD_DC 45    // Data/Command selection for the display
#define EPD_RSET 46  // Reset pin for the e-Paper display
#define EPD_BUSY 47  // Indicates when the display is busy

// SPI pins for e-paper (ESP32-S3 hardware SPI pins)
#define EPD_MOSI 11    // SPI Data In (Master Out Slave In)
#define EPD_MISO (-1)  // Not used by e-paper
#define EPD_SCLK 12    // SPI Clock

// Battery monitoring
#define BATTERY_PIN 1  // ADC1_CH0 pin for battery monitoring (connected via voltage divider)
#define VOLTAGE_DIVIDER_RATIO 2.0

// LED configuration (built-in LED on GPIO17)
#define LED_PIN 17
#define LED_ON (HIGH)  // LED active high

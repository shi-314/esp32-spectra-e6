
#pragma once

// Pin definitions for Wemos S2 Mini with Waveshare e-Paper HAT
#define EPD_CS 33    // Chip Select for SPI communication
#define EPD_DC 18    // Data/Command selection for the display
#define EPD_RSET 16  // Reset pin for the e-Paper display
#define EPD_BUSY 10  // Indicates when the display is busy

// SPI pins for e-paper (ESP32-S2 hardware SPI pins)
#define EPD_MOSI 11    // SPI Data In (Master Out Slave In)
#define EPD_MISO (-1)  // Not used by e-paper
#define EPD_SCLK 12    // SPI Clock

// Button configuration
#define BUTTON_1 39  // Button GPIO pin

// Battery monitoring
#define BATTERY_PIN 35  // ADC pin for battery monitoring

// LED configuration (built-in LED on Wemos S2 Mini)
#define LED_PIN 15     // Built-in LED on GPIO15
#define LED_ON (HIGH)  // LED active high
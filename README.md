# ESP32 Spectra E6 - E-Paper Image Display

An ESP32 firmware for displaying images on e-paper displays with automatic dithering and color optimization. The device
fetches images from a configurable URL, processes them through a dithering service for optimal e-paper display, and
shows them with efficient power management.

![FinishedDevice](https://blog.shvn.dev/posts/2025-esp32-spectra-e6/cover_hu_598c6daf125e5264.jpg)

See also the [blog post](https://blog.shvn.dev/posts/2025-esp32-spectra-e6/) for more details about this project.

## Features

- Image fetching and display from configurable URLs
- Automatic image dithering and color optimization for e-paper displays
- Configuration via web interface (captive portal)
- Deep sleep mode for battery efficiency
- ETag-based caching to avoid unnecessary downloads
- Battery level monitoring

## Image Processing

- **Image dithering**: Processed through dithering service
- **Color palette**: Optimized for 6-color e-paper displays (black, white, yellow, red, blue, green)
- **Resolution**: Automatically scaled to match display dimensions

## Hardware Required

- **ESP32 Development Board**: [LilyGO T7-S3](https://lilygo.cc/products/t7-s3) - or any other ESP32 board
- **E-Paper Display**: Compatible with 6-color e-paper displays such as
  [E Ink Spectra E6](https://www.waveshare.com/product/displays/e-paper/epaper-1/7.3inch-e-paper-hat-e.htm)
- **Battery**: 3.7V LiPo battery
- **WiFi Network**: For initial configuration and image downloads
- **Mobile Device**: For connecting to configuration interface

## Setup

1. Install PlatformIO
2. Clone this repository
3. **Configure display type** (if using a different display):
   - Edit `src/DisplayType.h` to match your specific e-paper display
   - Update pin definitions in `include/boards.h` if needed
4. Build and upload the firmware:
   ```bash
   pio run --target upload
   ```
5. Upload the filesystem image (contains web interface files):
   ```bash
   pio run --target uploadfs
   ```

## Configuration

1. Power on the device (shows configuration screen with QR code on first boot)
2. Scan the QR code with your phone to connect to the device's WiFi hotspot
3. Your web browser will open automatically showing the configuration page
4. Configure:
   - **WiFi credentials**: Your home network SSID and password
   - **Image URL**: Direct URL to the image you want to display

Configuration mode will automatically activate if WiFi connection fails or credentials are invalid.

## Usage

- **Auto-refresh**: Device periodically downloads and displays the configured image, then enters deep sleep
- **Configuration mode**: Automatically shown when no WiFi credentials are configured or WiFi connection fails
- **Battery monitoring**: Current battery level is displayed on screen
- **ETag caching**: Only downloads new images when they've changed (saves bandwidth and battery)

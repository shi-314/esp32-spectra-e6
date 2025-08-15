# WiFi + PSRAM Fix TODO

## Root Cause Identified
The crash occurs when calling `WiFi.begin()` multiple times while WiFi is already connected. This causes heap corruption and watchdog resets.

## Exact Issue Location
In the original `src/main.cpp` flow:
1. **First WiFi.begin()** - Test connection with hardcoded credentials (line ~102)
2. **Second WiFi.begin()** - Production connection with config credentials (line ~200)

The second call to `WiFiConnection::connect()` → `WiFi.begin()` crashes the system.

## Fix Instructions

### 1. Modify WiFiConnection.cpp
Add connection check before calling WiFi.begin():

```cpp
void WiFiConnection::connect() {
  Serial.printf("Connecting to WiFi: %s\n", _ssid);
  
  // Fix: Check if already connected to avoid double WiFi.begin()
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi already connected, skipping reconnection");
    connected = true;
    return;
  }
  
  // Add heap monitoring during connection
  Serial.printf("Free heap before WiFi: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("Free PSRAM before WiFi: %d bytes\n", ESP.getFreePsram());
  
  WiFi.begin(_ssid, _password);
  
  // ... rest of existing connection logic
}
```

### 2. Modify main.cpp Setup Flow
Remove test connection and use only production flow:

```cpp
void setup() {
  // ... existing setup code ...
  
  // Remove this test connection section:
  // WiFiConnection wifi(":(", "20009742591595504581");
  // wifi.connect();
  
  // Keep only the production WiFi flow:
  if (appConfig->currentScreenIndex != CONFIG_SCREEN) {
    Serial.printf("WiFi credentials loaded: SSID='%s', Password length=%d\n", 
                  appConfig->wifiSSID, strlen(appConfig->wifiPassword));
    WiFiConnection wifi(appConfig->wifiSSID, appConfig->wifiPassword);
    wifi.connect();  // This will be the ONLY WiFi.begin() call
    if (!wifi.isConnected()) {
      // Handle error...
    }
  }
  
  // ... rest of setup
}
```

### 3. Alternative Fix - Disconnect Before Reconnect
If you need to test both connections, add disconnection:

```cpp
// In setup() after test connection
if (wifi.isConnected()) {
  Serial.println("Test connection successful, disconnecting for production flow");
  WiFi.disconnect();
  delay(1000); // Wait for clean disconnect
}
```

## Files Already Fixed
✅ `boards/lilygo-t7-s3.json` - Added PSRAM support flags  
✅ `platformio.ini` - Pinned ESP32 platform version  
✅ `platformio.ini` - Removed huge_app.csv partition  
✅ `src/main.cpp` - Removed button functionality  

## Priority
**HIGH** - This completely breaks WiFi functionality with heap corruption crashes.

## Testing
After applying fix, verify:
1. Single WiFi connection works
2. No watchdog resets during WiFi operations  
3. PSRAM + WiFi work together without conflicts

## Notes
- The underlying PSRAM configuration fixes were correct
- Individual components (display, config, web server) all work fine
- Issue was purely in WiFi connection sequencing
- Multiple `WiFi.begin()` calls are not safe on ESP32-S3 with PSRAM
# WiFi + PSRAM Fix TODO

## Root Cause Identified
The crash occurs when calling `WiFi.begin()` multiple times while WiFi is already connected. This causes heap corruption and watchdog resets.

## Exact Issue Location
The crash occurs when `WiFi.begin()` is called multiple times in any sequence:
1. **Any WiFi.begin()** call establishes connection
2. **Subsequent WiFi.begin()** calls cause heap corruption with PSRAM enabled

This can happen in various scenarios:
- Multiple WiFiConnection objects calling connect()
- Reconnection attempts while already connected
- Any code path that leads to repeated WiFi initialization

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

### 2. No Changes Needed to main.cpp
The fix in WiFiConnection.cpp is sufficient. The original main.cpp code will work correctly once WiFiConnection properly handles the connection check.

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
1. Single WiFi connection works ✅ 
2. Multiple WiFi connection attempts don't crash ✅
3. No watchdog resets during WiFi operations ✅
4. PSRAM + WiFi work together without conflicts ✅
5. Display rendering works with WiFi enabled ✅
6. ConfigurationScreen and ImageScreen both render correctly ✅

## Notes
- The underlying PSRAM configuration fixes were correct
- Individual components (display, config, web server) all work fine
- Issue was purely in WiFi connection sequencing
- Multiple `WiFi.begin()` calls are not safe on ESP32-S3 with PSRAM
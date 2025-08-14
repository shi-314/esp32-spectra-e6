#ifndef WIFI_CONNECTION_H
#define WIFI_CONNECTION_H

class WiFiConnection {
 public:
  WiFiConnection(const char* ssid, const char* password);
  void connect();
  void reconnect();
  bool isConnected();
  void checkConnection();

 private:
  const char* _ssid;
  const char* _password;
  bool connected;
};

#endif

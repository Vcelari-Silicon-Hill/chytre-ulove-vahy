// Wi-Fi (WPA2-PSK or WPA2-Enterprise PEAP) and HTTPS upload.
#pragma once
#include <Arduino.h>

bool wifiConnect(uint32_t timeoutMs);
void wifiOff();
int wifiRssi();
String wifiIp();
// POST JSON to cfg.url with "Authorization: Bearer <token>". Returns HTTP status, <0 on network error.
int httpPostJson(const String &body, String *response);

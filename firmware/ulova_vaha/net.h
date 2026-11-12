// Wi-Fi (WPA2-PSK or WPA2-Enterprise PEAP) and HTTPS upload.
#pragma once
#include <Arduino.h>

bool wifiConnect(uint32_t timeoutMs);
void wifiOff();
int wifiRssi();
// Reason code of the last Wi-Fi disconnect (ESP-IDF wifi_err_reason_t), 0 = none.
int wifiLastReason();
void wifiScan(Print &p);
String wifiIp();
// POST JSON to cfg.url with "Authorization: Bearer <token>". Returns HTTP status, <0 on network error.
int httpPostJson(const String &body, String *response);

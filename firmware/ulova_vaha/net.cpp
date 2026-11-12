#include "net.h"
#include "config.h"
#include "roots.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

static volatile int lastReason = 0;

bool wifiConnect(uint32_t timeoutMs) {
  static bool hooked = false;
  if (!hooked) {
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) { lastReason = info.wifi_sta_disconnected.reason; },
                 ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    hooked = true;
  }
  lastReason = 0;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  if (cfg.eapUser.length()) {
    const String &identity = cfg.eapIdentity.length() ? cfg.eapIdentity : cfg.eapUser;
    WiFi.begin(cfg.ssid.c_str(), WPA2_AUTH_PEAP, identity.c_str(), cfg.eapUser.c_str(), cfg.eapPass.c_str(),
               cfg.eapCa.length() ? cfg.eapCa.c_str() : NULL);
  } else {
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.length() ? cfg.pass.c_str() : NULL);
  }
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) delay(50);
  return WiFi.status() == WL_CONNECTED;
}

void wifiOff() {
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
}

int wifiRssi() {
  return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
}

int httpPostJson(const String &body, String *response) {
  HTTPClient http;
  WiFiClientSecure secure;
  WiFiClient plain;
  bool ok;
  if (cfg.url.startsWith("https://")) {
    if (cfg.tls == "insecure") secure.setInsecure();
    else secure.setCACert(ROOT_CA_PEM);
    ok = http.begin(secure, cfg.url);
  } else {
    ok = http.begin(plain, cfg.url);
  }
  if (!ok) return -100;
  http.setTimeout(15000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + cfg.token);
  int code = http.POST(body);
  if (response) *response = code > 0 ? http.getString() : http.errorToString(code);
  http.end();
  return code;
}

int wifiLastReason() {
  return lastReason;
}

// Lists visible networks; the ESP32-C3 only sees 2.4 GHz.
void wifiScan(Print &p) {
  static const char *AUTH[] = {"otevrena", "WEP", "WPA", "WPA2", "WPA/WPA2", "WPA2-Enterprise", "WPA3", "WPA2/WPA3", "WAPI", "OWE", "WPA3-Enterprise"};
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    int a = WiFi.encryptionType(i);
    p.printf("  %-32s %4d dBm  kanal %2d  %s\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i),
             a < (int)(sizeof(AUTH) / sizeof(AUTH[0])) ? AUTH[a] : "?");
  }
  p.printf("@scan {\"count\":%d}\n", n < 0 ? 0 : n);
  WiFi.scanDelete();
  wifiOff();
}

String wifiIp() {
  return WiFi.localIP().toString();
}

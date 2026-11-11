#include "net.h"
#include "config.h"
#include "roots.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

bool wifiConnect(uint32_t timeoutMs) {
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

String wifiIp() {
  return WiFi.localIP().toString();
}

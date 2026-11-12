// Configuration stored in NVS (flash). Edited from the serial console / web config page.
#pragma once
#include <Arduino.h>

#define FW_VERSION "1.0.1"

// LaskaKit Meteo Mini v4.1 (ESP32-C3)
#define PIN_USUP_PWR 4   // HIGH = 3.3 V on the µŠup connectors (Click + DS18B20)
#define PIN_SDA 19
#define PIN_SCL 18
#define PIN_ONEWIRE 10
#define PIN_VBAT 0       // battery voltage through a divider
#define PIN_BUTTON 5     // hold at reset = stay in console

struct Config {
  // Wi-Fi. If eapUser is set, WPA2-Enterprise (PEAP/MSCHAPv2) is used, otherwise WPA2-PSK.
  String ssid;
  String pass;
  String eapIdentity;  // outer (anonymous) identity, empty = same as eapUser
  String eapUser;
  String eapPass;
  String eapCa;        // optional PEM of the RADIUS server CA
  // Server
  String url;          // e.g. https://vahy.example.org/api/v1/ingest
  String scaleId;      // e.g. vaha-1
  String token;        // per-scale secret from the server admin tool
  String tls;          // "verify" (Let's Encrypt roots built in) or "insecure"
  // Temperature sensors (16 hex chars each)
  String dsCell;       // glued to the load cell
  String dsBox;        // inside the box, next to the battery
  // Timing
  uint32_t interval;     // s between measurements
  uint32_t intervalCold; // s between measurements when the box is colder than coldBelow
  float coldBelow;       // degC
  uint16_t uploadEvery;  // upload after this many measurements
  // Calibration: kg = (raw - calOffset) / calFactor - tempCoef * (tCell - calTemp)
  int32_t calOffset;
  float calFactor;       // counts per kg, 0 = not calibrated
  float tempCoef;        // kg per degC
  float calTemp;         // degC during calibration
  float vbatK;           // battery divider ratio (Vbat / Vadc)
};

extern Config cfg;

void configLoad();
bool configSet(const String &key, const String &value);  // false = unknown key or bad value
void configSaveCal();
void configFactoryReset();
bool configReady();             // enough to measure and upload
String configJson(bool maskSecrets);
String macString();             // Wi-Fi MAC, needed for network registration

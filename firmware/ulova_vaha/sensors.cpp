#include "sensors.h"
#include "config.h"
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <SparkFun_Qwiic_Scale_NAU7802_Arduino_Library.h>

static NAU7802 scale;
static OneWire oneWire(PIN_ONEWIRE);
static DallasTemperature ds(&oneWire);
static bool scaleReady = false;

void sensorsPower(bool on) {
  pinMode(PIN_USUP_PWR, OUTPUT);
  digitalWrite(PIN_USUP_PWR, on ? HIGH : LOW);
  if (on) {
    delay(50);
    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setClock(100000);
    // weak internal pull-up (~45 kOhm) as a fallback when the board's 4.7 kOhm (R15, jumper JP1) is not connected;
    // enough for a short cable, not a replacement for the resistor in the field
    pinMode(PIN_ONEWIRE, INPUT_PULLUP);
  } else {
    Wire.end();
    scaleReady = false;
    // keep the bus pins from back-powering the sensors through their pull-ups
    pinMode(PIN_SDA, INPUT);
    pinMode(PIN_SCL, INPUT);
    pinMode(PIN_ONEWIRE, INPUT);
  }
}

bool scaleBegin() {
  scaleReady = false;
  if (!scale.begin(Wire)) return false;
  scale.setLDO(NAU7802_LDO_3V0);      // 3.0 V bridge excitation, stable on a 3.3 V supply
  scale.setGain(NAU7802_GAIN_128);
  scale.setSampleRate(NAU7802_SPS_10); // 10 SPS: lowest noise, rejects 50 Hz
  delay(250);                          // let AVDD settle
  scale.calibrateAFE();
  scaleReady = true;
  return true;
}

static int cmpInt32(const void *a, const void *b) {
  int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
  return (x > y) - (x < y);
}

bool scaleRawMedian(int32_t &out, int n) {
  if (!scaleReady) return false;
  const int discard = 3;
  if (n > 64) n = 64;
  int32_t v[64];
  int got = 0, seen = 0;
  uint32_t deadline = millis() + (uint32_t)(n + discard) * 150 + 1000;
  while (got < n && millis() < deadline) {
    if (scale.available()) {
      int32_t r = scale.getReading();
      if (seen++ >= discard) v[got++] = r;
    } else {
      delay(5);
    }
  }
  if (got < n / 2) return false;
  qsort(v, got, sizeof(int32_t), cmpInt32);
  out = v[got / 2];
  return true;
}

String dsHex(const uint8_t *a) {
  char b[17];
  for (int i = 0; i < 8; i++) sprintf(b + 2 * i, "%02X", a[i]);
  return String(b);
}

bool dsParse(const String &hex, uint8_t *a) {
  if (hex.length() != 16) return false;
  for (int i = 0; i < 8; i++) {
    char tmp[3] = {hex[2 * i], hex[2 * i + 1], 0};
    char *end;
    a[i] = (uint8_t)strtoul(tmp, &end, 16);
    if (*end) return false;
  }
  return true;
}

static float validTemp(float t) {
  return (t == DEVICE_DISCONNECTED_C || t == 85.0f) ? NAN : t;
}

int dsScan(String *addr, float *temp, int max) {
  ds.begin();
  ds.setWaitForConversion(true);
  int n = 0;
  DeviceAddress a;
  oneWire.reset_search();
  while (n < max && oneWire.search(a)) {
    if (OneWire::crc8(a, 7) != a[7] || a[0] != 0x28) continue;
    ds.setResolution(a, 12);
    addr[n++] = dsHex(a);
  }
  if (n) ds.requestTemperatures();
  for (int i = 0; i < n; i++) {
    uint8_t b[8];
    dsParse(addr[i], b);
    temp[i] = validTemp(ds.getTempC(b));
  }
  return n;
}

float batteryVolts() {
  analogSetPinAttenuation(PIN_VBAT, ADC_11db);
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(PIN_VBAT);
  return (sum / 8.0f) / 1000.0f * cfg.vbatK;
}

float rawToKg(int32_t raw, float tCell) {
  if (cfg.calFactor == 0.0f) return NAN;
  float kg = (raw - cfg.calOffset) / cfg.calFactor;
  if (!isnan(tCell) && cfg.tempCoef != 0.0f) kg -= cfg.tempCoef * (tCell - cfg.calTemp);
  return kg;
}

Measurement measure() {
  Measurement m;
  m.scaleOk = false;
  m.raw = 0;
  m.kg = NAN;
  m.tCell = NAN;
  m.tBox = NAN;

  // start the temperature conversion first (750 ms), read the load cell meanwhile
  ds.begin();
  ds.setWaitForConversion(false);
  uint8_t aCell[8], aBox[8];
  bool hasCell = dsParse(cfg.dsCell, aCell);
  bool hasBox = dsParse(cfg.dsBox, aBox);
  if (hasCell) ds.setResolution(aCell, 12);
  if (hasBox) ds.setResolution(aBox, 12);
  ds.requestTemperatures();
  uint32_t convStart = millis();

  if (scaleBegin()) m.scaleOk = scaleRawMedian(m.raw, 16);

  while (millis() - convStart < 800) delay(10);
  if (hasCell) m.tCell = validTemp(ds.getTempC(aCell));
  if (hasBox) m.tBox = validTemp(ds.getTempC(aBox));
  if (isnan(m.tCell) && isnan(m.tBox)) {
    String addr[2];
    float t[2];
    int n = dsScan(addr, t, 2);
    if (!hasCell && !hasBox) {
      // not assigned yet: use whatever sensors are there, first one as the load cell
      if (n > 0) m.tCell = t[0];
      if (n > 1) m.tBox = t[1];
    } else {
      // the assigned sensors are silent: any other one still drives the cold mode,
      // as the box so that it never feeds the weight compensation
      for (int i = 0; i < n; i++) if (!isnan(t[i])) { m.tBox = t[i]; break; }
    }
  }

  if (m.scaleOk) m.kg = rawToKg(m.raw, m.tCell);
  m.vbat = batteryVolts();
  if (m.scaleOk) scale.powerDown();
  return m;
}

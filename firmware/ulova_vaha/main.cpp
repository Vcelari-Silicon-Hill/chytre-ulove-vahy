// Chytré úly pro Silicon Hill – firmware for the hive scale (LaskaKit Meteo Mini v4.1, ESP32-C3)
// MIT License, see LICENSE in the repository root.
//
// Cycle: wake up -> measure -> store in RTC memory -> upload a batch now and then -> deep sleep.
// On power-up (not wake from sleep) a serial console is available for configuration.

#include <Arduino.h>
#include <esp_sleep.h>
#include <sys/time.h>
#include "config.h"
#include "sensors.h"
#include "net.h"
#include "app.h"

DualPrint out;

size_t DualPrint::write(uint8_t c) {
  if (Serial) Serial.write(c);
  Serial0.write(c);
  return 1;
}
size_t DualPrint::write(const uint8_t *buf, size_t size) {
  if (Serial) Serial.write(buf, size);
  Serial0.write(buf, size);
  return size;
}

// ---------- RTC memory (survives deep sleep, cleared on power loss) ----------
struct Sample {
  uint32_t t;      // seconds, device clock (keeps running in deep sleep)
  int32_t raw;
  float kg;
  int16_t tCell;   // 0.01 degC, INT16_MIN = missing
  int16_t tBox;
  uint16_t vbatMv;
  uint8_t ok;
};

#define BUF_MAX 96
#define RTC_MAGIC 0x56A11A01
RTC_DATA_ATTR static uint32_t rtcMagic;
RTC_DATA_ATTR static Sample buf[BUF_MAX];
RTC_DATA_ATTR static uint16_t bufHead, bufCount;
RTC_DATA_ATTR static uint32_t wakeCount;
RTC_DATA_ATTR static uint32_t lastUploadT;
RTC_DATA_ATTR static uint32_t fastUntil;
RTC_DATA_ATTR static uint16_t failStreak;
RTC_DATA_ATTR static float lastKg;
RTC_DATA_ATTR static int8_t lastRssi;

static uint32_t nowS() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint32_t)tv.tv_sec;
}

static void log(const String &s) {
  if (Serial) Serial.println(s);
}

static int16_t packT(float t) { return isnan(t) ? INT16_MIN : (int16_t)lroundf(t * 100.0f); }

void bufferPush(const Measurement &m) {
  Sample s;
  s.t = nowS();
  s.raw = m.raw;
  s.kg = m.kg;
  s.tCell = packT(m.tCell);
  s.tBox = packT(m.tBox);
  s.vbatMv = (uint16_t)lroundf(m.vbat * 1000.0f);
  s.ok = m.scaleOk ? 1 : 0;
  uint16_t idx = (bufHead + bufCount) % BUF_MAX;
  if (bufCount == BUF_MAX) {  // full: overwrite the oldest
    bufHead = (bufHead + 1) % BUF_MAX;
    idx = (bufHead + bufCount - 1) % BUF_MAX;
  } else {
    bufCount++;
  }
  buf[idx] = s;
}

int bufferCount() { return bufCount; }

static String num(float v, int dec) { return isnan(v) ? String("null") : String(v, dec); }
static String numT(int16_t v) { return v == INT16_MIN ? String("null") : String(v / 100.0f, 2); }

int uploadBuffer(String *response) {
  if (!bufCount) return 0;
  uint32_t now = nowS();
  String body;
  body.reserve(200 + bufCount * 80);
  body = "{\"scale\":\"" + cfg.scaleId + "\",\"fw\":\"" FW_VERSION "\",\"mac\":\"" + macString() +
         "\",\"rssi\":" + String(wifiRssi()) + ",\"boot\":" + String(wakeCount) + ",\"fails\":" + String(failStreak) +
         ",\"readings\":[";
  for (uint16_t i = 0; i < bufCount; i++) {
    const Sample &s = buf[(bufHead + i) % BUF_MAX];
    if (i) body += ',';
    body += "{\"age\":" + String(now - s.t);
    body += ",\"raw\":" + (s.ok ? String(s.raw) : String("null"));
    body += ",\"kg\":" + (s.ok ? num(s.kg, 3) : String("null"));
    body += ",\"tc\":" + numT(s.tCell);
    body += ",\"tb\":" + numT(s.tBox);
    body += ",\"vb\":" + String(s.vbatMv / 1000.0f, 3) + "}";
  }
  body += "]}";
  int code = httpPostJson(body, response);
  if (code >= 200 && code < 300) {
    bufCount = 0;
    bufHead = 0;
    failStreak = 0;
    lastUploadT = now;
  } else {
    failStreak++;
  }
  return code;
}

String measurementJson(const Measurement &m) {
  return "{\"scale_ok\":" + String(m.scaleOk ? "true" : "false") + ",\"raw\":" + String(m.raw) +
         ",\"kg\":" + num(m.kg, 3) + ",\"t_cell\":" + num(m.tCell, 2) + ",\"t_box\":" + num(m.tBox, 2) +
         ",\"vbat\":" + num(m.vbat, 3) + ",\"buffered\":" + String(bufCount) + "}";
}

static void goToSleep(uint32_t seconds) {
  log("sleep " + String(seconds) + " s");
  if (Serial) Serial.flush();
  sensorsPower(false);
  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  esp_deep_sleep_start();
}

static void measureCycle() {
  wakeCount++;
  sensorsPower(true);
  Measurement m = measure();
  sensorsPower(false);
  bufferPush(m);
  log("measure " + measurementJson(m));

  uint32_t now = nowS();
  bool haveKg = m.scaleOk && !isnan(m.kg) && !isnan(lastKg);
  float delta = haveKg ? fabsf(m.kg - lastKg) : 0.0f;
  if (delta > 0.5f) fastUntil = now + 1800;  // something is happening: sample every minute for 30 min
  if (m.scaleOk && !isnan(m.kg)) lastKg = m.kg;

  float t = !isnan(m.tBox) ? m.tBox : m.tCell;
  bool cold = !isnan(t) && t < cfg.coldBelow;
  uint32_t interval = cold ? cfg.intervalCold : cfg.interval;
  uint32_t uploadPeriod = cfg.uploadEvery * cfg.interval;
  if (failStreak >= 3) uploadPeriod *= 4;               // network down: try less often
  bool due = (now - lastUploadT) >= uploadPeriod;
  bool urgent = delta > 1.0f;                            // possible swarm: send right away
  bool nearlyFull = bufCount >= BUF_MAX - 4;
  if (m.vbat < 3.30f && !nearlyFull) due = urgent = false; // save the battery

  if (configReady() && (due || urgent || nearlyFull)) {
    if (wifiConnect(cfg.eapUser.length() ? 25000 : 15000)) {
      lastRssi = wifiRssi();
      String resp;
      int code = uploadBuffer(&resp);
      log("upload " + String(code) + " " + resp);
    } else {
      failStreak++;
      log("wifi failed");
    }
    wifiOff();
  }

  uint32_t sleepS = now < fastUntil ? 60 : interval;
  if (m.vbat > 0.5f && m.vbat < 3.20f) sleepS = max(sleepS, (uint32_t)(3 * 3600));
  goToSleep(sleepS);
}

void setup() {
  Serial.setTxTimeoutMs(0);
  Serial.begin(115200);
  Serial0.begin(115200);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  configLoad();

  bool fromSleep = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
  if (!fromSleep || rtcMagic != RTC_MAGIC) {
    rtcMagic = RTC_MAGIC;
    bufHead = bufCount = 0;
    wakeCount = 0;
    lastUploadT = nowS();
    fastUntil = 0;
    failStreak = 0;
    lastKg = NAN;
  }

  if (!fromSleep) {
    delay(1500);  // give the USB port time to enumerate
    consoleRun(configReady());
  }
  measureCycle();  // never returns, ends in deep sleep
}

void loop() {}

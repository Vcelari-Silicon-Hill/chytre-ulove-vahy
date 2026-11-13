// Serial console for setup and calibration (115200 Bd, one command per line).
// Lines starting with "@" carry JSON for the web config page.

#include <Arduino.h>
#include "config.h"
#include "sensors.h"
#include "net.h"
#include "app.h"
#include <WiFi.h>

static const uint32_t WAIT_CONFIGURED_MS = 20000;   // window after power-up when already configured
static const uint32_t IDLE_TIMEOUT_MS = 10UL * 60 * 1000;

static String lineBuf;

static bool readLine(String &line) {
  while (Serial.available() || Serial0.available()) {
    int c = Serial.available() ? Serial.read() : Serial0.read();
    if (c == '\r') continue;
    if (c == '\n') {
      line = lineBuf;
      lineBuf = "";
      line.trim();
      return true;
    }
    if (lineBuf.length() < 6000) lineBuf += (char)c;
  }
  return false;
}

static void help() {
  out.println(F("Prikazy:"));
  out.println(F("  show                  vypise nastaveni (JSON)"));
  out.println(F("  set <klic> <hodnota>  ulozi nastaveni, napr. set ssid SiliconHill"));
  out.println(F("     klice: ssid pass eap_user eap_pass eap_identity eap_ca url scale_id token tls"));
  out.println(F("            ds_cell ds_box interval interval_cold cold_below upload_every"));
  out.println(F("            cal_offset cal_factor temp_coef cal_temp vbat_k"));
  out.println(F("  read                  jedno mereni (JSON)"));
  out.println(F("  tare                  tara prazdne vahy"));
  out.println(F("  cal <kg>              kalibrace zavazim se znamou hmotnosti"));
  out.println(F("  ds                    seznam teplomeru DS18B20"));
  out.println(F("  dsfind                urci teplomer na tenzometru (zahrejte ho v dlani)"));
  out.println(F("  wifi                  test pripojeni k Wi-Fi"));
  out.println(F("  scan (networks)       seznam viditelnych siti (jen 2,4 GHz)"));
  out.println(F("  upload                zmeri a hned odesle na server"));
  out.println(F("  mac                   MAC adresa pro registraci v siti"));
  out.println(F("  factory               smaze vsechna nastaveni"));
  out.println(F("  run                   ukonci konzoli a zacne merit"));
  out.println(F("  reboot                restart"));
}

static void printConfig() {
  out.print("@config ");
  out.println(configJson(true));
}

static void cmdRead() {
  sensorsPower(true);
  Measurement m = measure();
  sensorsPower(false);
  out.print("@reading ");
  out.println(measurementJson(m));
}

static bool rawWithPower(int32_t &raw, float &tCell) {
  // Same path as a normal measurement: the reading still settles after power-up, so a
  // different sample window here gave tare a constant offset against the measured weight.
  sensorsPower(true);
  Measurement m = measure();
  sensorsPower(false);
  raw = m.raw;
  tCell = m.tCell;
  return m.scaleOk;
}

static void cmdTare() {
  int32_t raw;
  float t;
  if (!rawWithPower(raw, t)) { out.println(F("ERR prevodnik NAU7802 nenalezen (I2C 0x2A)")); return; }
  cfg.calOffset = raw;
  configSaveCal();
  out.printf("OK tara offset=%ld\n", (long)raw);
  printConfig();
}

static void cmdCal(const String &arg) {
  float kg = arg.toFloat();
  if (kg <= 0.0f) { out.println(F("ERR zadejte hmotnost zavazi v kg, napr. cal 20.15")); return; }
  int32_t raw;
  float t;
  if (!rawWithPower(raw, t)) { out.println(F("ERR prevodnik NAU7802 nenalezen (I2C 0x2A)")); return; }
  float factor = (raw - cfg.calOffset) / kg;
  if (fabsf(factor) < 10.0f) { out.println(F("ERR zmena je prilis mala - je zavazi na vaze a byla udelana tara?")); return; }
  cfg.calFactor = factor;
  if (!isnan(t)) cfg.calTemp = t;
  configSaveCal();
  out.printf("OK kalibrace faktor=%.4f (citu/kg), teplota %.2f C\n", factor, cfg.calTemp);
  if (factor < 0) out.println(F("INFO surova hodnota pri zatizeni klesa - funguje to, jen jsou prohozene vodice A+/A-"));
  printConfig();
}

static void cmdDs() {
  String a[8];
  float t[8];
  sensorsPower(true);
  int n = dsScan(a, t, 8);
  sensorsPower(false);
  out.print("@ds [");
  for (int i = 0; i < n; i++) {
    if (i) out.print(',');
    out.printf("{\"addr\":\"%s\",\"t\":%s,\"role\":\"%s\"}", a[i].c_str(), isnan(t[i]) ? "null" : String(t[i], 2).c_str(),
               a[i] == cfg.dsCell ? "cell" : (a[i] == cfg.dsBox ? "box" : ""));
  }
  out.println("]");
}

static void cmdDsFind() {
  String a[8];
  float base[8], t[8];
  sensorsPower(true);
  int n = dsScan(a, base, 8);
  if (n == 1) {
    // a single thermometer needs no warming: it serves the load cell and the cold mode
    sensorsPower(false);
    cfg.dsCell = a[0];
    cfg.dsBox = "";
    configSaveCal();
    out.printf("OK jediny teplomer=%s (%.2f C), pouzije se pro tenzometr i zimni rezim\n", cfg.dsCell.c_str(), base[0]);
    printConfig();
    return;
  }
  if (n != 2) {
    sensorsPower(false);
    out.printf("ERR nalezeno %d teplomeru, cekam 1 nebo 2. Zkontrolujte rezistor 4,7 kOhm mezi DQ a 3V3 a poradi vodicu.\n", n);
    return;
  }
  out.println(F("INFO zahrejte teplomer, ktery patri na tenzometr, v dlani (max. 60 s)"));
  uint32_t start = millis();
  int found = -1;
  while (millis() - start < 60000 && found < 0) {
    String a2[8];
    int n2 = dsScan(a2, t, 8);
    if (n2 != 2) continue;
    for (int i = 0; i < 2; i++) {
      int j = (a2[i] == a[0]) ? 0 : 1;
      if (!isnan(t[i]) && !isnan(base[j]) && t[i] - base[j] > 1.5f) found = j;
    }
    out.printf("INFO %.2f C / %.2f C\n", t[0], t[1]);
  }
  sensorsPower(false);
  if (found < 0) { out.println(F("ERR zadny teplomer se neohral o 1,5 C")); return; }
  cfg.dsCell = a[found];
  cfg.dsBox = a[1 - found];
  configSaveCal();
  out.printf("OK tenzometr=%s krabice=%s\n", cfg.dsCell.c_str(), cfg.dsBox.c_str());
  printConfig();
}

static void cmdWifi() {
  if (!cfg.ssid.length()) { out.println(F("ERR neni nastavene ssid")); return; }
  uint32_t t0 = millis();
  bool ok = wifiConnect(cfg.eapUser.length() ? 25000 : 15000);
  uint32_t ms = millis() - t0;
  int reason = ok ? 0 : wifiLastReason();
  out.printf("@wifi {\"ok\":%s,\"ms\":%lu,\"rssi\":%d,\"ip\":\"%s\",\"mac\":\"%s\",\"reason\":%d,\"why\":\"%s\"}\n", ok ? "true" : "false",
             (unsigned long)ms, wifiRssi(), ok ? wifiIp().c_str() : "", macString().c_str(), reason,
             reason ? WiFi.disconnectReasonName((wifi_err_reason_t)reason) : "");
  wifiOff();
}

static void cmdUpload() {
  if (!configReady()) { out.println(F("ERR chybi ssid, url, scale_id nebo token")); return; }
  sensorsPower(true);
  Measurement m = measure();
  sensorsPower(false);
  bufferPush(m);
  if (!wifiConnect(cfg.eapUser.length() ? 25000 : 15000)) {
    out.println(F("ERR Wi-Fi se nepripojila"));
    wifiOff();
    return;
  }
  String resp;
  int code = uploadBuffer(&resp);
  wifiOff();
  resp.replace("\n", " ");
  out.printf("@upload {\"code\":%d,\"buffered\":%d}\n", code, bufferCount());
  out.println((code >= 200 && code < 300 ? "OK server " : "ERR server ") + String(code) + " " + resp);
}

static bool handle(const String &line, bool &exitConsole) {
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String arg = sp < 0 ? "" : line.substring(sp + 1);
  cmd.toLowerCase();
  if (cmd == "help" || cmd == "?") help();
  else if (cmd == "show") printConfig();
  else if (cmd == "set") {
    int sp2 = arg.indexOf(' ');
    String key = sp2 < 0 ? arg : arg.substring(0, sp2);
    String val = sp2 < 0 ? "" : arg.substring(sp2 + 1);
    if (configSet(key, val)) out.println("OK " + key);
    else out.println("ERR neznamy klic nebo spatna hodnota: " + key);
  }
  else if (cmd == "read") cmdRead();
  else if (cmd == "tare") cmdTare();
  else if (cmd == "cal") cmdCal(arg);
  else if (cmd == "ds") cmdDs();
  else if (cmd == "dsfind") cmdDsFind();
  else if (cmd == "wifi") cmdWifi();
  else if (cmd == "scan" || cmd == "networks") wifiScan(out);
  else if (cmd == "upload") cmdUpload();
  else if (cmd == "mac") out.println("@mac \"" + macString() + "\"");
  else if (cmd == "factory") { configFactoryReset(); out.println(F("OK nastaveni smazano")); printConfig(); }
  else if (cmd == "reboot") { out.println(F("OK restart")); delay(200); ESP.restart(); }
  else if (cmd == "run") {
    if (!configReady()) out.println(F("WARN chybi ssid/url/scale_id/token - bude jen merit a ukladat"));
    out.println(F("OK spoustim mereni"));
    exitConsole = true;
  }
  else if (cmd.length()) { out.println("ERR neznamy prikaz: " + cmd + " (help)"); return false; }
  return true;
}

void consoleRun(bool configuredAtStart) {
  out.println();
  out.println(F("== Chytre ule pro Silicon Hill - ulova vaha, firmware " FW_VERSION " =="));
  out.println("@hello {\"fw\":\"" FW_VERSION "\",\"mac\":\"" + macString() + "\",\"configured\":" +
              (configuredAtStart ? "true" : "false") + "}");
  bool button = digitalRead(PIN_BUTTON) == LOW;
  if (configuredAtStart && !button)
    out.println(F("Konzole: napiste 'help' do 20 s, jinak vaha zacne merit."));
  else
    out.println(F("Konzole: vaha neni nastavena, napiste 'help'."));

  bool active = !configuredAtStart || button;
  uint32_t lastInput = millis();
  bool exitConsole = false;
  while (!exitConsole) {
    String line;
    if (readLine(line)) {
      active = true;
      lastInput = millis();
      handle(line, exitConsole);
    }
    uint32_t idle = millis() - lastInput;
    if (!active && idle > WAIT_CONFIGURED_MS) break;
    if (active && idle > IDLE_TIMEOUT_MS) break;  // nobody there: continue (or sleep) to protect the battery
    delay(10);
  }
}

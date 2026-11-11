#include "config.h"
#include <Preferences.h>
#include <esp_mac.h>

Config cfg;
static Preferences prefs;
static const char *NS = "vaha";

struct StrKey { const char *name; String Config::*field; bool secret; };
static const StrKey STR_KEYS[] = {
  {"ssid", &Config::ssid, false},
  {"pass", &Config::pass, true},
  {"eap_identity", &Config::eapIdentity, false},
  {"eap_user", &Config::eapUser, false},
  {"eap_pass", &Config::eapPass, true},
  {"eap_ca", &Config::eapCa, false},
  {"url", &Config::url, false},
  {"scale_id", &Config::scaleId, false},
  {"token", &Config::token, true},
  {"tls", &Config::tls, false},
  {"ds_cell", &Config::dsCell, false},
  {"ds_box", &Config::dsBox, false},
};

static void setDefaults() {
  cfg = Config();
  cfg.tls = "verify";
  cfg.interval = 900;
  cfg.intervalCold = 3600;
  cfg.coldBelow = 3.0f;
  cfg.uploadEvery = 4;
  cfg.calOffset = 0;
  cfg.calFactor = 0.0f;
  cfg.tempCoef = 0.0f;
  cfg.calTemp = 20.0f;
  cfg.vbatK = 2.3f / 1.3f;  // 1 MOhm / 1.3 MOhm divider on the Meteo Mini, check with a multimeter
}

void configLoad() {
  setDefaults();
  prefs.begin(NS, true);
  for (const auto &k : STR_KEYS) cfg.*(k.field) = prefs.getString(k.name, cfg.*(k.field));
  cfg.interval = prefs.getUInt("interval", cfg.interval);
  cfg.intervalCold = prefs.getUInt("interval_cold", cfg.intervalCold);
  cfg.coldBelow = prefs.getFloat("cold_below", cfg.coldBelow);
  cfg.uploadEvery = prefs.getUShort("upload_every", cfg.uploadEvery);
  cfg.calOffset = prefs.getInt("cal_offset", cfg.calOffset);
  cfg.calFactor = prefs.getFloat("cal_factor", cfg.calFactor);
  cfg.tempCoef = prefs.getFloat("temp_coef", cfg.tempCoef);
  cfg.calTemp = prefs.getFloat("cal_temp", cfg.calTemp);
  cfg.vbatK = prefs.getFloat("vbat_k", cfg.vbatK);
  prefs.end();
}

static bool parseFloat(const String &v, float &out) {
  char *end = nullptr;
  float f = strtof(v.c_str(), &end);
  if (end == v.c_str() || isnan(f)) return false;
  out = f;
  return true;
}

static bool parseUInt(const String &v, uint32_t &out, uint32_t lo, uint32_t hi) {
  char *end = nullptr;
  unsigned long x = strtoul(v.c_str(), &end, 10);
  if (end == v.c_str() || x < lo || x > hi) return false;
  out = x;
  return true;
}

// "\n" written as two characters is turned into a newline (for PEM certificates on one line)
static String unescape(const String &v) {
  String s = v;
  s.replace("\\n", "\n");
  return s;
}

bool configSet(const String &key, const String &rawValue) {
  String value = rawValue;
  value.trim();
  if (value == "\"\"" || value == "-") value = "";  // way to clear a value
  prefs.begin(NS, false);
  bool ok = true;
  bool found = false;
  for (const auto &k : STR_KEYS) {
    if (key == k.name) {
      found = true;
      String v = (key == "eap_ca") ? unescape(value) : value;
      if (key == "tls" && v != "verify" && v != "insecure") { ok = false; break; }
      if ((key == "ds_cell" || key == "ds_box") && v.length() && v.length() != 16) { ok = false; break; }
      if (key == "url" && v.length() && !v.startsWith("http://") && !v.startsWith("https://")) { ok = false; break; }
      cfg.*(k.field) = v;
      prefs.putString(k.name, v);
    }
  }
  if (!found) {
    found = true;
    uint32_t u;
    float f;
    if (key == "interval") { ok = parseUInt(value, u, 30, 86400); if (ok) { cfg.interval = u; prefs.putUInt("interval", u); } }
    else if (key == "interval_cold") { ok = parseUInt(value, u, 30, 86400); if (ok) { cfg.intervalCold = u; prefs.putUInt("interval_cold", u); } }
    else if (key == "upload_every") { ok = parseUInt(value, u, 1, 48); if (ok) { cfg.uploadEvery = u; prefs.putUShort("upload_every", u); } }
    else if (key == "cold_below") { ok = parseFloat(value, f); if (ok) { cfg.coldBelow = f; prefs.putFloat("cold_below", f); } }
    else if (key == "cal_offset") { ok = value.length() > 0; if (ok) { cfg.calOffset = value.toInt(); prefs.putInt("cal_offset", cfg.calOffset); } }
    else if (key == "cal_factor") { ok = parseFloat(value, f); if (ok) { cfg.calFactor = f; prefs.putFloat("cal_factor", f); } }
    else if (key == "temp_coef") { ok = parseFloat(value, f); if (ok) { cfg.tempCoef = f; prefs.putFloat("temp_coef", f); } }
    else if (key == "cal_temp") { ok = parseFloat(value, f); if (ok) { cfg.calTemp = f; prefs.putFloat("cal_temp", f); } }
    else if (key == "vbat_k") { ok = parseFloat(value, f) && f > 1.0f && f < 5.0f; if (ok) { cfg.vbatK = f; prefs.putFloat("vbat_k", f); } }
    else found = false;
  }
  prefs.end();
  return found && ok;
}

void configSaveCal() {
  prefs.begin(NS, false);
  prefs.putInt("cal_offset", cfg.calOffset);
  prefs.putFloat("cal_factor", cfg.calFactor);
  prefs.putFloat("cal_temp", cfg.calTemp);
  prefs.putFloat("temp_coef", cfg.tempCoef);
  prefs.putString("ds_cell", cfg.dsCell);
  prefs.putString("ds_box", cfg.dsBox);
  prefs.end();
}

void configFactoryReset() {
  prefs.begin(NS, false);
  prefs.clear();
  prefs.end();
  setDefaults();
}

bool configReady() {
  return cfg.ssid.length() && cfg.url.length() && cfg.scaleId.length() && cfg.token.length();
}

static String jsonEscape(const String &s) {
  String o;
  o.reserve(s.length() + 2);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o;
}

String macString() {
  uint8_t m[6];
  esp_read_mac(m, ESP_MAC_WIFI_STA);
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return String(b);
}

String configJson(bool maskSecrets) {
  String j = "{";
  for (const auto &k : STR_KEYS) {
    String v = cfg.*(k.field);
    if (maskSecrets && k.secret && v.length()) v = "***";
    if (k.field == &Config::eapCa && v.length()) v = "(PEM, " + String(v.length()) + " B)";
    j += "\"" + String(k.name) + "\":\"" + jsonEscape(v) + "\",";
  }
  j += "\"interval\":" + String(cfg.interval);
  j += ",\"interval_cold\":" + String(cfg.intervalCold);
  j += ",\"cold_below\":" + String(cfg.coldBelow, 1);
  j += ",\"upload_every\":" + String(cfg.uploadEvery);
  j += ",\"cal_offset\":" + String(cfg.calOffset);
  j += ",\"cal_factor\":" + String(cfg.calFactor, 4);
  j += ",\"temp_coef\":" + String(cfg.tempCoef, 5);
  j += ",\"cal_temp\":" + String(cfg.calTemp, 2);
  j += ",\"vbat_k\":" + String(cfg.vbatK, 4);
  j += ",\"fw\":\"" FW_VERSION "\"";
  j += ",\"mac\":\"" + macString() + "\"";
  j += "}";
  return j;
}

// DS18B20 batch test – LaskaKit Meteo Mini v4.1 (ESP32-C3)
// Libraries: OneWire, DallasTemperature (Arduino Library Manager)
// Wiring: all red -> 3V3, all black -> GND, all yellow -> IO10 (µŠup IO connector).
// Serial monitor: 115200 Bd. Output is CSV + a summary table every 6 rounds.

#include <OneWire.h>
#include <DallasTemperature.h>

#define PIN_USUP_PWR 4      // switches 3.3 V on the µŠup connectors
#define PIN_1WIRE    10

const int      MAX_SENSORS = 16;
const uint32_t PERIOD_MS   = 10000;  // one round every 10 s
const int      WARMUP      = 3;      // rounds ignored in the statistics
// Reference temperature: NAN = compare each sensor to the median of all sensors
// (room / water bath test). Set to 0.0 for the ice bath test.
const float    REFERENCE   = NAN;

OneWire ow(PIN_1WIRE);
DallasTemperature ds(&ow);
DeviceAddress addr[MAX_SENSORS];
int n = 0;

double sumDev[MAX_SENSORS], sumDev2[MAX_SENSORS];
int    good[MAX_SENSORS], bad[MAX_SENSORS];
long   rounds = 0;

void printAddr(const DeviceAddress a) {
  for (int i = 0; i < 8; i++) { if (a[i] < 16) Serial.print('0'); Serial.print(a[i], HEX); }
}

float median(float *v, int k) {
  for (int i = 1; i < k; i++) {               // insertion sort, k is small
    float x = v[i]; int j = i - 1;
    while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; }
    v[j + 1] = x;
  }
  return (k % 2) ? v[k / 2] : (v[k / 2 - 1] + v[k / 2]) / 2.0f;
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  pinMode(PIN_USUP_PWR, OUTPUT);
  digitalWrite(PIN_USUP_PWR, HIGH);
  delay(300);

  ds.begin();
  ds.setWaitForConversion(true);

  DeviceAddress a;
  ow.reset_search();
  while (n < MAX_SENSORS && ow.search(a)) {
    if (OneWire::crc8(a, 7) != a[7]) { Serial.print("CRC ERROR, check wiring: "); printAddr(a); Serial.println(); continue; }
    if (a[0] != 0x28) { Serial.printf("Device with family 0x%02X is not a DS18B20, skipped\n", a[0]); continue; }
    memcpy(addr[n], a, 8);
    n++;
  }

  Serial.printf("\nFound %d DS18B20\n", n);
  for (int i = 0; i < n; i++) {
    ds.setResolution(addr[i], 12);
    Serial.printf("#%02d  ", i + 1);
    printAddr(addr[i]);
    Serial.println(ds.readPowerSupply(addr[i]) ? "  PARASITE POWER - red wire not connected!" : "  powered OK");
  }

  Serial.print("\nround");
  for (int i = 0; i < n; i++) Serial.printf(",#%02d", i + 1);
  Serial.println(",ref,warmest");
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last < PERIOD_MS && rounds > 0) return;
  last = millis();
  rounds++;

  ds.requestTemperatures();
  float t[MAX_SENSORS], valid[MAX_SENSORS];
  int k = 0;
  for (int i = 0; i < n; i++) {
    t[i] = ds.getTempC(addr[i]);
    bool ok = (t[i] != DEVICE_DISCONNECTED_C) && (t[i] != 85.0f);  // 85.0 = power-on value, -127 = lost
    if (ok) valid[k++] = t[i]; else t[i] = NAN;
  }
  float ref = isnan(REFERENCE) ? (k ? median(valid, k) : NAN) : REFERENCE;

  // CSV line + the sensor that is clearly warmer than the rest (hold a probe in your hand to identify it)
  Serial.print(rounds);
  int warm = -1; float warmDev = 1.0f;
  for (int i = 0; i < n; i++) {
    Serial.print(','); if (isnan(t[i])) Serial.print("ERR"); else Serial.print(t[i], 3);
    if (!isnan(t[i]) && t[i] - ref > warmDev) { warmDev = t[i] - ref; warm = i; }
  }
  Serial.printf(",%.3f,", ref);
  if (warm >= 0) Serial.printf("#%02d (+%.1f C)", warm + 1, warmDev);
  Serial.println();

  if (rounds > WARMUP) {
    for (int i = 0; i < n; i++) {
      if (isnan(t[i])) { bad[i]++; continue; }
      double d = t[i] - ref;
      sumDev[i] += d; sumDev2[i] += d * d; good[i]++;
    }
  }

  if (rounds > WARMUP && (rounds - WARMUP) % 6 == 0) {
    Serial.println("\n #   ROM               mean dev  noise   errors  verdict");
    for (int i = 0; i < n; i++) {
      double m = good[i] ? sumDev[i] / good[i] : NAN;
      double sd = good[i] > 1 ? sqrt(max(0.0, sumDev2[i] / good[i] - m * m)) : NAN;
      const char *v = "OK";
      if (bad[i] > 0)              v = "REJECT (read errors)";
      else if (fabs(m) > 0.5)      v = "REJECT (offset > 0.5 C)";
      else if (sd > 0.1)           v = "CHECK (noisy)";
      Serial.printf("#%02d  ", i + 1); printAddr(addr[i]);
      Serial.printf("  %+6.2f   %5.3f   %3d     %s\n", m, sd, bad[i], v);
    }
    Serial.println();
  }
}

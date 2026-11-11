// Load cell (NAU7802 on Load Cell 2 Click), 2x DS18B20 and battery voltage.
#pragma once
#include <Arduino.h>

struct Measurement {
  bool scaleOk;
  int32_t raw;    // median of the ADC readings
  float kg;       // NAN when not calibrated or scale missing
  float tCell;    // degC, NAN when missing
  float tBox;     // degC, NAN when missing
  float vbat;     // V
};

void sensorsPower(bool on);              // switches 3.3 V on the µŠup connectors
bool scaleBegin();                       // init NAU7802, returns false if not found
bool scaleRawMedian(int32_t &out, int n);// median of n readings (first ones discarded)
Measurement measure();                   // full measurement, sensors must be powered
float batteryVolts();
float rawToKg(int32_t raw, float tCell);

// DS18B20 helpers
int dsScan(String *addr, float *temp, int max);   // all sensors on the bus with their temperature
String dsHex(const uint8_t *a);
bool dsParse(const String &hex, uint8_t *a);

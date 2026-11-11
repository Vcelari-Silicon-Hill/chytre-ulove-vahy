// Shared state between the measuring cycle and the console.
#pragma once
#include <Arduino.h>
#include "sensors.h"

// Output to both the native USB port and UART0, whichever the board uses.
class DualPrint : public Print {
public:
  size_t write(uint8_t c) override;
  size_t write(const uint8_t *buf, size_t size) override;
};
extern DualPrint out;

void bufferPush(const Measurement &m);   // store a measurement in RTC memory
int bufferCount();
int uploadBuffer(String *response);       // HTTP status, buffer is cleared on success
void consoleRun(bool configuredAtStart);  // returns on "run"
String measurementJson(const Measurement &m);

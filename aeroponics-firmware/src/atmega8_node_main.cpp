#include "atmega8_eeprom_schedule_storage.h"

// The MEGA8 application composition root is intentionally kept separate from
// the ESP32 gateway. The production node firmware wires its RF transport,
// actuator driver, and authenticated command processor here; this minimal
// target keeps the adapter/build gate linkable until the selected RF module
// driver is locked by the hardware decision gate.
extern "C" void setup() {}
extern "C" void loop() {}

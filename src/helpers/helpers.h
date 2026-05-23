#ifndef HELPERS_H
#define HELPERS_H

#include <Arduino.h>
#include "BluetoothSerial.h"

extern BluetoothSerial SerialBT;

#define DEBUG_PRINT(x)     { Serial.print(x); SerialBT.print(x); }
#define DEBUG_PRINTLN(x)   { Serial.println(x); SerialBT.println(x); }
#define DEBUG_PRINTF(...)  { Serial.printf(__VA_ARGS__); SerialBT.printf(__VA_ARGS__); }
#define DEBUG_PRINT_F(x,p) { Serial.print(x,p); SerialBT.print(x,p); }

#endif
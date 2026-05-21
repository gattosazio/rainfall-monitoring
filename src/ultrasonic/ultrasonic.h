#ifndef ULTRASONIC_H
#define ULTRASONIC_H

#include <Arduino.h>
#include "../domain/telemetry.h"

void initUltrasonic();
UltrasonicReading readUltrasonic();

#endif

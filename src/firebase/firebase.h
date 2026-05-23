#ifndef FIREBASE_H
#define FIREBASE_H

#include <Arduino.h>
#include "../domain/telemetry.h"

bool sendToFirebase(const SensorSnapshot& snapshot);

#endif

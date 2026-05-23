#ifndef FIREBASE_H
#define FIREBASE_H

#include <Arduino.h>
#include "../domain/telemetry.h"

bool sendToFirebase(const SensorSnapshot& snapshot);

// Returns true if a Firebase "remote button" requested an OTA check.
// If the command is true, it will be cleared back to false (requires auth).
bool firebaseConsumeOtaCheckCommand(bool& outRequested);

#endif

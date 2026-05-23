#pragma once

#include <Arduino.h>

// Checks OTA manifest and performs cellular OTA if a newer version is available.
// Returns true if an update was applied (device will reboot on success).
bool otaCheckAndUpdate();


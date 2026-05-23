#include "loggers.h"
#include <Arduino.h>
#include "FS.h"
#include "SPIFFS.h"
#include "../helpers/helpers.h"

void initLoggers() {
  if (!SPIFFS.begin(true)) {
    DEBUG_PRINTLN("SPIFFS mount failed!");
    while (true);
  }
  if (!SPIFFS.exists("/sensor_log.csv")) {
    File file = SPIFFS.open("/sensor_log.csv", FILE_WRITE);
    if (file) {
      file.println("Timestamp,Rainfall_mm,WaterLevel_cm");
      file.close();
    }
  }
  DEBUG_PRINTLN("SPIFFS ready.");
}
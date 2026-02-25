#include <Arduino.h>
#include "helpers/helpers.h"
#include "modem/modem.h"
#include "gps/gps.h"
#include "raingauge/raingauge.h"
#include "ultrasonic/ultrasonic.h"
#include "firebase/firebase.h"
#include "hibernation/hibernation.h"
#include "loggers/loggers.h"

// Timing Settings
const unsigned long SENSOR_READ_INTERVAL = 3000;
unsigned long lastSensorRead = 0;

const unsigned long FIREBASE_SEND_INTERVAL = 300000UL;  
unsigned long lastFirebaseSend = 0;

const unsigned long GPS_UPDATE_INTERVAL = 30000;
unsigned long lastGPSUpdate = 0;

const unsigned long ACTIVE_DURATION_MS = 7200000UL;  
unsigned long bootTime = 0;

int lastSentTipCount = 0;

void setup() {
  Serial.begin(115200);
  SerialBT.begin("LILYGO_Debug");
  delay(1000);

  bootTime = millis();

  initRainGauge();
  initUltrasonic();
  DEBUG_PRINTLN("Sensors initialized.");

  initLoggers();

  powerOnModem();
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(1500);
  connectNetwork();
  delay(1000);

  String currentTime = getModemTime();
  DEBUG_PRINTLN("Modem clock synchronized: " + currentTime);

  DEBUG_PRINTLN(" Initializing GPS...");
  if (enableGPS()) {
    DEBUG_PRINTLN("GPS initialization successful");
  } else {
    DEBUG_PRINTLN("GPS initialization failed - will retry in loop");
  }
  delay(2000);

  DEBUG_PRINTLN("System is now active for 2 hours before hibernation.\n");
}

void loop() {
  unsigned long now = millis();
  static volatile unsigned long localTipCount;

  updateRainfall();

  // --- SENSOR READ ---
  if (now - lastSensorRead >= SENSOR_READ_INTERVAL) {
    String timestamp = getModemTime();
    DEBUG_PRINTLN("\n--- Reading Sensors at " + timestamp + " ---");

    float rawDist = getDistanceCM_refined();
    if (rawDist > 0) {
      Serial.printf("Raw Ultrasonic Distance: %.1f cm\n", rawDist);
    } else {
      Serial.println("Raw Ultrasonic Distance: INVALID");
    }

    float distance = getWaterLevelCM();
    DEBUG_PRINTF("Water Depth: %.1f cm\n", distance);
    DEBUG_PRINTF("Total Rainfall: %.2f mm\n", totalRainfall);
    DEBUG_PRINTF("Rainfall Rate: %.2f mm/hr\n", rainRate);

    float percent = getWaterPercent();
    String label = getWaterLabel();
    Serial.printf("Water Level: %.1f%% → %s\n", percent, label.c_str());

    lastSensorRead = now;
  }

  // --- GPS UPDATE ---
  if (now - lastGPSUpdate >= GPS_UPDATE_INTERVAL) {
    DEBUG_PRINTLN("\n Attempting GPS update...");
    if (updateGPSLocation()) {
      DEBUG_PRINTLN("GPS location updated");
    } else {
      DEBUG_PRINTLN("Waiting for GPS fix...");
      printGPSDiagnostics();
    }
    lastGPSUpdate = now;
  }

  // --- RAIN GAUGE EVENT ---
  noInterrupts();
  localTipCount = tipCount;
  interrupts();

  if (localTipCount > lastSentTipCount) {
    updateRainfall();
    String eventTime = getModemTime();
    float waterDepth = getWaterLevelCM();
    if (waterDepth < 0) waterDepth = 0.0;

    if (sendToFirebase(eventTime, totalRainfall, waterDepth)) {
      DEBUG_PRINTLN("Data sent to Firebase (rain gauge tipped)");
      lastSentTipCount = localTipCount;
      lastFirebaseSend = now;  
    } else {
      DEBUG_PRINTLN("Failed to send data to Firebase");
    }
  }

  // --- PERIODIC FIREBASE UPDATE ---
  if (now - lastFirebaseSend >= FIREBASE_SEND_INTERVAL) {
    DEBUG_PRINTLN("\n Periodic Firebase update triggered (5 min interval)...");
    String timestamp = getModemTime();
    float waterDepth = getWaterLevelCM();
    if (waterDepth < 0) waterDepth = 0.0;
    
    if (sendToFirebase(timestamp, totalRainfall, waterDepth)) {
      DEBUG_PRINTLN("Periodic data sent to Firebase");
      lastFirebaseSend = now;
    } else {
      DEBUG_PRINTLN("Failed to send periodic data");
      lastFirebaseSend = now - FIREBASE_SEND_INTERVAL + 60000UL; // Retry 1 min
    }
  }

  // --- AUTO-HIBERNATION HANDLER ---
  if (millis() - bootTime >= ACTIVE_DURATION_MS) {
    DEBUG_PRINTLN("\n Active window elapsed (2 hours). Preparing for hibernation...");
    detachInterrupt(digitalPinToInterrupt(TIP_PIN));
    enterHibernation();
  }

  delay(100);
}
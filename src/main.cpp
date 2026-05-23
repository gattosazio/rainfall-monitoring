#include <Arduino.h>
#include "helpers/helpers.h"
#include "modem/modem.h"
#include "raingauge/raingauge.h"
#include "ultrasonic/ultrasonic.h"
#include "firebase/firebase.h"
#include "hibernation/hibernation.h"
#include "loggers/loggers.h"
#include "ota/ota.h"
#include "domain/config.h"
#include "domain/scheduler.h"
#include "domain/telemetry.h"

unsigned long lastSensorRead = 0;
unsigned long lastFirebaseSend = 0;
unsigned long bootTime = 0;
unsigned long dryWindowStart = 0;
unsigned long lastRainEventAttempt = 0;
unsigned long lastOtaCommandPoll = 0;

unsigned long lastSentTipCount = 0;
bool wasRainingLastLoop = false;

SensorSnapshot captureSnapshot(const String& timestamp, const String& sendReason) {
  SensorSnapshot snapshot;
  snapshot.timestamp = timestamp;
  snapshot.sendReason = sendReason;
  snapshot.rainGauge = getRainGaugeReading();
  snapshot.ultrasonic = readUltrasonic();
  return snapshot;
}

bool isDryUploadWindowActive(unsigned long now) {
  return dryWindowStart > 0 &&
         (now - dryWindowStart) <= Config::FIREBASE_DRY_UPLOAD_WINDOW_MS;
}

void setup() {
  Serial.begin(115200);
  SerialBT.begin("node2_debug");
  delay(1000);

  bootTime = millis();

  initRainGauge();
  initUltrasonic();
  DEBUG_PRINTLN("Sensors initialized.");

  initLoggers();

  powerOnModem();
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(1500);
  bool networkOk = false;
  for (int attempt = 1; attempt <= 12; ++attempt) {
    DEBUG_PRINTF("[DEBUG] Network bring-up attempt %d/12\n", attempt);
    if (connectNetwork()) {
      networkOk = true;
      break;
    }
    DEBUG_PRINTLN("[WARN] connectNetwork failed; retrying in 5s...");
    delay(5000);
  }
  if (!networkOk) {
    DEBUG_PRINTLN("[ERROR] Network bring-up failed; continuing without data.");
  }
  delay(1000);

  if (networkOk && Config::OTA_ENABLED) {
    delay(Config::OTA_CHECK_DELAY_MS);
    otaCheckAndUpdate();
  }

  String currentTime = getModemTime();
  DEBUG_PRINTLN("Modem clock synchronized: " + currentTime);
  delay(2000);

  SensorSnapshot bootSnapshot = captureSnapshot(currentTime, "heartbeat_boot");
  if (sendToFirebase(bootSnapshot)) {
    DEBUG_PRINTLN("Boot heartbeat sent to Firebase");
    lastFirebaseSend = millis();
  } else {
    DEBUG_PRINTLN("Failed to send boot heartbeat");
  }

  DEBUG_PRINTLN("System is now running with hibernation disabled.\n");
}

void loop() {
  unsigned long now = millis();

  updateRainfall();
  RainGaugeReading currentRainReading = getRainGaugeReading();
  bool rainingNow = currentRainReading.isRaining;

  if (wasRainingLastLoop && !rainingNow && dryWindowStart == 0) {
    dryWindowStart = now;
    DEBUG_PRINTLN("Rain window ended. Starting 2-hour dry upload window.");
  } else if (rainingNow) {
    dryWindowStart = 0;
  }

  wasRainingLastLoop = rainingNow;

  if (now - lastSensorRead >= Config::SENSOR_READ_INTERVAL_MS) {
    String timestamp = getModemTime();
    UltrasonicReading ultrasonicReading = readUltrasonic();
    DEBUG_PRINTLN("\n--- Reading Sensors at " + timestamp + " ---");

    if (ultrasonicReading.rawDistanceCm > 0) {
      DEBUG_PRINTF("Raw Ultrasonic Distance: %.1f cm\n", ultrasonicReading.rawDistanceCm);
      DEBUG_PRINTF("Median Ultrasonic Distance: %.1f cm\n",
                   ultrasonicReading.medianRawDistanceCm);
    } else {
      DEBUG_PRINTLN("Raw Ultrasonic Distance: INVALID");
      DEBUG_PRINTLN("Median Ultrasonic Distance: INVALID");
    }

    DEBUG_PRINTF("Water Depth: %.1f cm\n", ultrasonicReading.waterLevelCm);
    DEBUG_PRINTF("Total Rainfall: %.2f mm\n", currentRainReading.totalRainfallMm);
    DEBUG_PRINTF("Rainfall Rate: %.2f mm/hr\n", currentRainReading.rainRateMmPerHour);

    lastSensorRead = now;
  }

  unsigned long localTipCount = getRainGaugeTipCount();
  if (localTipCount > lastSentTipCount &&
      now - lastRainEventAttempt >= Config::FIREBASE_RAIN_EVENT_RETRY_MS) {
    lastRainEventAttempt = now;
    String eventTime = getModemTime();
    SensorSnapshot snapshot = captureSnapshot(eventTime, "rain_event");

    if (sendToFirebase(snapshot)) {
      DEBUG_PRINTLN("Data sent to Firebase (rain gauge tipped)");
      lastSentTipCount = localTipCount;
      lastFirebaseSend = now;
    } else {
      DEBUG_PRINTLN("Failed to send data to Firebase");
    }
  }

  bool dryWindowActive = isDryUploadWindowActive(now);
  PeriodicScheduleDecision periodicDecision =
      getPeriodicScheduleDecision(rainingNow, dryWindowActive);

  if (periodicDecision.shouldSend &&
      now - lastFirebaseSend >= periodicDecision.intervalMs) {
    DEBUG_PRINTLN(periodicDecision.debugMessage);

    String timestamp = getModemTime();
    SensorSnapshot snapshot = captureSnapshot(timestamp, periodicDecision.sendReason);

    if (sendToFirebase(snapshot)) {
      DEBUG_PRINTLN("Periodic data sent to Firebase");
      lastFirebaseSend = now;
    } else {
      DEBUG_PRINTLN("Failed to send periodic data");
      lastFirebaseSend = now - periodicDecision.intervalMs + Config::FIREBASE_RETRY_DELAY_MS;
    }
  }

  if (Config::OTA_ENABLED &&
      (lastOtaCommandPoll == 0 ||
       now - lastOtaCommandPoll >= Config::OTA_COMMAND_POLL_INTERVAL_MS)) {
    lastOtaCommandPoll = now;
    bool requested = false;
    if (firebaseConsumeOtaCheckCommand(requested) && requested) {
      DEBUG_PRINTLN("[OTA] Remote update requested (Firebase command).");
      otaCheckAndUpdate();
    }
  }

  if (Config::HIBERNATION_ENABLED &&
      millis() - bootTime >= Config::ACTIVE_DURATION_MS) {
    DEBUG_PRINTLN("\n Active window elapsed (2 hours). Preparing for hibernation...");
    detachInterrupt(digitalPinToInterrupt(Config::TIP_PIN));
    enterHibernation();
  }

  delay(100);
}

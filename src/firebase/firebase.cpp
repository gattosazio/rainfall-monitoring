#include "firebase.h"
#include <ArduinoJson.h>
#include "../helpers/helpers.h"
#include "../modem/modem.h"
#include "../domain/config.h"

namespace {
String buildPayload(const SensorSnapshot& snapshot) {
  JsonDocument doc;

  doc["timestamp"] = snapshot.timestamp;
  doc["send_reason"] = snapshot.sendReason;

  JsonObject rainRaw = doc["rain_gauge"]["raw"].to<JsonObject>();
  rainRaw["tip_count"] = snapshot.rainGauge.totalTips;
  rainRaw["ignored_tip_count"] = snapshot.rainGauge.ignoredTips;
  rainRaw["tips_in_window"] = snapshot.rainGauge.tipsInWindow;
  rainRaw["last_tip_gap_ms"] = snapshot.rainGauge.lastTipGapMs;
  rainRaw["is_raining"] = snapshot.rainGauge.isRaining;

  JsonObject rainProcessed = doc["rain_gauge"]["processed"].to<JsonObject>();
  rainProcessed["total_mm"] = snapshot.rainGauge.totalRainfallMm;
  rainProcessed["rate_mm_per_hr"] = snapshot.rainGauge.rainRateMmPerHour;
  rainProcessed["mm_per_tip"] = snapshot.rainGauge.mmPerTip;

  JsonObject ultrasonicRaw = doc["ultrasonic"]["raw"].to<JsonObject>();
  ultrasonicRaw["pulse_us"] = snapshot.ultrasonic.rawPulseUs;
  ultrasonicRaw["distance_cm"] = snapshot.ultrasonic.rawDistanceCm;
  ultrasonicRaw["median_distance_cm"] = snapshot.ultrasonic.medianRawDistanceCm;
  ultrasonicRaw["valid_samples"] = snapshot.ultrasonic.validSampleCount;
  ultrasonicRaw["valid"] = snapshot.ultrasonic.isValid;

  JsonObject ultrasonicProcessed = doc["ultrasonic"]["processed"].to<JsonObject>();
  ultrasonicProcessed["filtered_distance_cm"] = snapshot.ultrasonic.filteredDistanceCm;
  ultrasonicProcessed["water_level_cm"] = snapshot.ultrasonic.waterLevelCm;
  ultrasonicProcessed["empty_distance_cm"] = Config::ULTRASONIC_EMPTY_DISTANCE_CM;
  ultrasonicProcessed["max_water_level_cm"] = Config::ULTRASONIC_MAX_WATER_LEVEL_CM;

  String payload;
  serializeJson(doc, payload);
  return payload;
}

enum class RecoveryStage {
  None = 0,
  HttpReset = 1,
  ReattachData = 2,
};

void ensureDataOn() {
  sendATCommand("AT+CGATT=1", 5000);
  delay(300);
  sendATCommand("AT+CGACT=1,1", 5000);
  delay(800);
}

bool ensureHttpReady(RecoveryStage stage) {
  if (stage == RecoveryStage::ReattachData) {
    DEBUG_PRINTLN("[FIREBASE] Recovery: Reconnecting mobile data...");
    ensureDataOn();
  }

  if (stage == RecoveryStage::HttpReset || stage == RecoveryStage::ReattachData) {
    DEBUG_PRINTLN("[FIREBASE] Recovery: Resetting HTTP service...");
    sendATCommand("AT+HTTPTERM", 2000);
    delay(300);
  }

  // Always ensure HTTP is initialized before a request.
  String initResp = sendATCommandWithResponse("AT+HTTPINIT", 5000);
  if (initResp.indexOf("OK") < 0) {
    DEBUG_PRINTLN("[FIREBASE] HTTPINIT failed");
    return false;
  }
  delay(300);
  return true;
}

bool postPayloadOnce(const String& payload, RecoveryStage stage) {
  // Keep mobile data ON by default. Only do recovery steps if we had failures.
  if (!ensureHttpReady(stage)) return false;

  sendATCommand("AT+HTTPPARA=\"CID\",1", 2000);
  sendATCommand("AT+HTTPSSL=1", 2000);
  String urlCmd = "AT+HTTPPARA=\"URL\",\"" + String(Config::FIREBASE_URL) + "\"";
  sendATCommand(urlCmd, 2000);
  sendATCommand("AT+HTTPPARA=\"CONTENT\",\"application/json\"", 2000);

  String dataCmd = "AT+HTTPDATA=" + String(payload.length()) + ",30000";
  SerialAT.println(dataCmd);

  if (!waitForPrompt("DOWNLOAD", 20000)) {
    DEBUG_PRINTLN("[ERROR] DOWNLOAD prompt timeout");
    sendATCommand("AT+HTTPTERM", 2000);
    return false;
  }

  SerialAT.print(payload);
  delay(1000);
  sendATCommand("AT+HTTPACTION=1", 1000);

  unsigned long start = millis();
  bool success = false;
  String response = "";
  while (millis() - start < 30000) {
    if (SerialAT.available()) {
      char c = SerialAT.read();
      response += c;
      if (response.indexOf("+HTTPACTION: 1,200") >= 0) {
        success = true;
        break;
      }
    }
    delay(100);
  }

  sendATCommand("AT+HTTPTERM", 2000);
  return success;
}
}

bool sendToFirebase(const SensorSnapshot& snapshot) {
  DEBUG_PRINTLN("[DEBUG] Starting Firebase send...");

  String payload = buildPayload(snapshot);
  DEBUG_PRINTLN("[DEBUG] Payload: " + payload);

  static unsigned int persistentFailCount = 0;

  for (unsigned int attempt = 1; attempt <= Config::FIREBASE_HTTP_RETRIES; ++attempt) {
    RecoveryStage stage = RecoveryStage::None;
    if (attempt == 2) stage = RecoveryStage::HttpReset;
    if (attempt >= 3) stage = RecoveryStage::ReattachData;

    if (postPayloadOnce(payload, stage)) {
      DEBUG_PRINTF("[DEBUG] Firebase response check: SUCCESS on attempt %u\n", attempt);
      persistentFailCount = 0;
      return true;
    }
    DEBUG_PRINTF("[DEBUG] Firebase response check: FAILED on attempt %u\n", attempt);
    delay(2000);
  }

  persistentFailCount++;
  DEBUG_PRINTF("[WARN] Firebase persistent failures: %u\n", persistentFailCount);
  if (Config::FIREBASE_REBOOT_ON_PERSISTENT_FAIL &&
      persistentFailCount >= Config::FIREBASE_PERSISTENT_FAIL_REBOOT_COUNT) {
    DEBUG_PRINTLN("[ERROR] Too many Firebase failures. Rebooting device...");
    delay(1000);
    ESP.restart();
  }

  return false;
}

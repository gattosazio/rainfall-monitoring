#include "firebase.h"
#include <ArduinoJson.h>
#include "../helpers/helpers.h"
#include "../modem/modem.h"
#include "../domain/config.h"
#include "firebase_auth.h"

namespace {
struct HttpActionResult {
  int method = -1;
  int status = -1;
  int dataLen = 0;
};

bool waitForHttpAction(unsigned long timeoutMs, HttpActionResult& out) {
  unsigned long start = millis();
  String buf;
  while (millis() - start < timeoutMs) {
    while (SerialAT.available()) {
      char c = (char)SerialAT.read();
      buf += c;
      int idx = buf.indexOf("+HTTPACTION:");
      if (idx >= 0) {
        int end = buf.indexOf('\n', idx);
        if (end >= 0) {
          String line = buf.substring(idx, end);
          int method = -1, status = -1, len = 0;
          if (sscanf(line.c_str(), "+HTTPACTION: %d,%d,%d", &method, &status,
                     &len) == 3) {
            out.method = method;
            out.status = status;
            out.dataLen = len;
            return true;
          }
        }
      }
    }
    delay(10);
  }
  return false;
}

bool readLine(String& out, unsigned long timeoutMs) {
  out = "";
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    while (SerialAT.available()) {
      char c = (char)SerialAT.read();
      out += c;
      if (c == '\n') return true;
    }
    delay(5);
  }
  return out.length() > 0;
}

bool httpReadChunk(size_t offset, size_t maxBytes, uint8_t* outBuf,
                   size_t& outLen, unsigned long timeoutMs = 20000UL) {
  outLen = 0;
  while (SerialAT.available()) SerialAT.read();
  SerialAT.println("AT+HTTPREAD=" + String(offset) + "," + String(maxBytes));

  String line;
  unsigned long start = millis();
  int n = -1;
  while (millis() - start < timeoutMs) {
    if (!readLine(line, 2000UL)) continue;
    int idx = line.indexOf("+HTTPREAD:");
    if (idx < 0) continue;
    if (sscanf(line.substring(idx).c_str(), "+HTTPREAD: %d", &n) == 1) break;
  }
  if (n < 0) return false;
  if (n == 0) {
    outLen = 0;
    return true;
  }

  size_t got = 0;
  unsigned long dataStart = millis();
  while (got < (size_t)n && millis() - dataStart < timeoutMs) {
    while (SerialAT.available() && got < (size_t)n) {
      outBuf[got++] = (uint8_t)SerialAT.read();
    }
    delay(1);
  }
  if (got != (size_t)n) return false;

  outLen = got;
  return true;
}

String appendAuth(const String& url, const String& idToken) {
  if (idToken.length() == 0) return url;
  if (url.indexOf('?') >= 0) return url + "&auth=" + idToken;
  return url + "?auth=" + idToken;
}

bool httpGetToString(const String& url, String& outBody, int& outStatus) {
  outBody = "";
  outStatus = -1;

  sendATCommand("AT+HTTPTERM", 2000);
  delay(200);
  sendATCommand("AT+HTTPINIT", 5000);
  delay(200);
  sendATCommand("AT+HTTPPARA=\"CID\",1", 2000);
  sendATCommand("AT+HTTPSSL=1", 2000);

  String urlCmd = "AT+HTTPPARA=\"URL\",\"" + url + "\"";
  sendATCommand(urlCmd, 5000);

  SerialAT.println("AT+HTTPACTION=0");  // GET
  HttpActionResult action;
  if (!waitForHttpAction(60000UL, action)) {
    sendATCommand("AT+HTTPTERM", 2000);
    return false;
  }

  outStatus = action.status;

  const size_t total = (action.dataLen > 0) ? (size_t)action.dataLen : 0;
  const size_t chunk = 1024;
  uint8_t buf[1024];
  size_t offset = 0;

  while (true) {
    size_t want = chunk;
    if (total > 0 && offset + want > total) want = total - offset;
    if (want == 0) break;

    size_t got = 0;
    if (!httpReadChunk(offset, want, buf, got)) break;
    if (got == 0) break;
    outBody.reserve(outBody.length() + got);
    for (size_t i = 0; i < got; ++i) outBody += (char)buf[i];
    offset += got;
    if (total > 0 && offset >= total) break;
  }

  sendATCommand("AT+HTTPTERM", 2000);
  return true;
}

bool httpSendWithBody(int method, const String& url, const String& contentType,
                      const String& body, int& outStatus) {
  outStatus = -1;

  sendATCommand("AT+HTTPTERM", 2000);
  delay(200);
  sendATCommand("AT+HTTPINIT", 5000);
  delay(200);
  sendATCommand("AT+HTTPPARA=\"CID\",1", 2000);
  sendATCommand("AT+HTTPSSL=1", 2000);

  String urlCmd = "AT+HTTPPARA=\"URL\",\"" + url + "\"";
  sendATCommand(urlCmd, 5000);
  String contentCmd = "AT+HTTPPARA=\"CONTENT\",\"" + contentType + "\"";
  sendATCommand(contentCmd, 2000);

  String dataCmd = "AT+HTTPDATA=" + String(body.length()) + ",30000";
  SerialAT.println(dataCmd);
  if (!waitForPrompt("DOWNLOAD", 15000)) {
    sendATCommand("AT+HTTPTERM", 2000);
    return false;
  }
  SerialAT.print(body);
  delay(1000);

  SerialAT.println("AT+HTTPACTION=" + String(method));
  HttpActionResult action;
  if (!waitForHttpAction(60000UL, action)) {
    sendATCommand("AT+HTTPTERM", 2000);
    return false;
  }

  outStatus = action.status;
  sendATCommand("AT+HTTPTERM", 2000);
  return true;
}

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

bool putPayloadAuthed(const String& payload, const String& idToken) {
  sendATCommand("AT+CGACT=0,1", 5000);
  delay(1000);
  sendATCommand("AT+CGACT=1,1", 5000);
  delay(1000);

  const String url = appendAuth(String(Config::FIREBASE_URL), idToken);
  int status = -1;
  // 4 = PUT (replace data at this path).
  if (!httpSendWithBody(4, url, "application/json", payload, status)) {
    DEBUG_PRINTLN("[ERROR] Firebase write failed (modem HTTP)");
    return false;
  }
  if (status != 200) {
    DEBUG_PRINTF("[ERROR] Firebase write failed HTTP %d\n", status);
    return false;
  }
  return true;
}
}

bool sendToFirebase(const SensorSnapshot& snapshot) {
  DEBUG_PRINTLN("[DEBUG] Starting Firebase send...");

  String payload = buildPayload(snapshot);
  DEBUG_PRINTLN("[DEBUG] Payload: " + payload);

  String idToken;
  if (!firebaseEnsureIdToken(idToken)) {
    DEBUG_PRINTLN("[ERROR] No Firebase Auth token; cannot send with locked rules.");
    return false;
  }

  for (unsigned int attempt = 1; attempt <= Config::FIREBASE_HTTP_RETRIES; ++attempt) {
    if (putPayloadAuthed(payload, idToken)) {
      DEBUG_PRINTF("[DEBUG] Firebase response check: SUCCESS on attempt %u\n", attempt);
      return true;
    }
    DEBUG_PRINTF("[DEBUG] Firebase response check: FAILED on attempt %u\n", attempt);
    delay(2000);
  }

  return false;
}

bool firebaseConsumeOtaCheckCommand(bool& outRequested) {
  outRequested = false;

  String idToken;
  if (!firebaseEnsureIdToken(idToken)) {
    return false;
  }

  const String url = appendAuth(String(Config::FIREBASE_COMMAND_OTA_URL), idToken);
  String body;
  int status = -1;
  if (!httpGetToString(url, body, status)) return false;
  if (status != 200) return false;

  body.trim();
  if (body == "true") {
    outRequested = true;
    int putStatus = -1;
    // Clear the command flag to prevent repeated updates.
    httpSendWithBody(4, url, "application/json", "false", putStatus);
  }

  return true;
}

#include "ota.h"

#include <ArduinoJson.h>
#include <Update.h>
#include <mbedtls/sha256.h>

#include "../domain/config.h"
#include "../helpers/helpers.h"
#include "../modem/modem.h"

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
          int method = -1;
          int status = -1;
          int len = 0;
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

  // Request a chunk from modem's HTTP response buffer.
  while (SerialAT.available()) SerialAT.read();
  SerialAT.println("AT+HTTPREAD=" + String(offset) + "," + String(maxBytes));

  // Wait for "+HTTPREAD: <n>" header line.
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

  // Read exactly n bytes of raw data.
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

bool httpGetToString(const String& url, String& outBody) {
  outBody = "";

  // Reset HTTP service each time to reduce state-related errors.
  sendATCommand("AT+HTTPTERM", 2000);
  delay(200);
  sendATCommand("AT+HTTPINIT", 5000);
  delay(200);

  // These settings are optional; some modem firmware returns ERROR but can still proceed.
  sendATCommand("AT+HTTPPARA=\"CID\",1", 2000);
  sendATCommand("AT+HTTPSSL=1", 2000);

  String urlCmd = "AT+HTTPPARA=\"URL\",\"" + url + "\"";
  sendATCommand(urlCmd, 5000);

  SerialAT.println("AT+HTTPACTION=0");  // GET
  HttpActionResult action;
  if (!waitForHttpAction(60000UL, action)) {
    DEBUG_PRINTLN("[OTA] HTTPACTION timeout");
    sendATCommand("AT+HTTPTERM", 2000);
    return false;
  }

  if (action.status != 200) {
    DEBUG_PRINTF("[OTA] HTTP GET failed (status %d)\n", action.status);
    sendATCommand("AT+HTTPTERM", 2000);
    return false;
  }

  const size_t total = (action.dataLen > 0) ? (size_t)action.dataLen : 0;
  const size_t chunk = (size_t)Config::OTA_HTTPREAD_CHUNK_BYTES;
  uint8_t buf[1024];
  size_t offset = 0;

  while (true) {
    size_t want = chunk;
    if (total > 0 && offset + want > total) want = total - offset;
    if (want == 0) break;

    size_t got = 0;
    if (!httpReadChunk(offset, want, buf, got)) {
      DEBUG_PRINTLN("[OTA] HTTPREAD failed (manifest)");
      sendATCommand("AT+HTTPTERM", 2000);
      return false;
    }
    if (got == 0) break;
    outBody.reserve(outBody.length() + got);
    for (size_t i = 0; i < got; ++i) outBody += (char)buf[i];
    offset += got;
    if (total > 0 && offset >= total) break;
  }

  sendATCommand("AT+HTTPTERM", 2000);
  return outBody.length() > 0;
}

bool parseHexSha256(const String& hex, uint8_t out[32]) {
  if (hex.length() != 64) return false;
  for (int i = 0; i < 32; ++i) {
    char c1 = hex[i * 2];
    char c2 = hex[i * 2 + 1];
    auto hexVal = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
      if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
      return -1;
    };
    int v1 = hexVal(c1);
    int v2 = hexVal(c2);
    if (v1 < 0 || v2 < 0) return false;
    out[i] = (uint8_t)((v1 << 4) | v2);
  }
  return true;
}

bool sha256HexEqual(const uint8_t a[32], const uint8_t b[32]) {
  for (int i = 0; i < 32; ++i) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

bool downloadAndFlashFirmware(const String& firmwareUrl, size_t expectedSize,
                              const String& expectedSha256Hex) {
  DEBUG_PRINTLN("[OTA] Starting firmware download...");

  sendATCommand("AT+HTTPTERM", 2000);
  delay(200);
  sendATCommand("AT+HTTPINIT", 5000);
  delay(200);
  sendATCommand("AT+HTTPPARA=\"CID\",1", 2000);
  sendATCommand("AT+HTTPSSL=1", 2000);

  String urlCmd = "AT+HTTPPARA=\"URL\",\"" + firmwareUrl + "\"";
  sendATCommand(urlCmd, 5000);

  SerialAT.println("AT+HTTPACTION=0");  // GET
  HttpActionResult action;
  if (!waitForHttpAction(120000UL, action)) {
    DEBUG_PRINTLN("[OTA] HTTPACTION timeout (firmware)");
    sendATCommand("AT+HTTPTERM", 2000);
    return false;
  }
  if (action.status != 200) {
    DEBUG_PRINTF("[OTA] Firmware GET failed (status %d)\n", action.status);
    sendATCommand("AT+HTTPTERM", 2000);
    return false;
  }

  size_t total = (action.dataLen > 0) ? (size_t)action.dataLen : expectedSize;
  if (expectedSize > 0 && total > 0 && expectedSize != total) {
    DEBUG_PRINTF("[OTA] Size mismatch (expected %u, got %u)\n",
                 (unsigned)expectedSize, (unsigned)total);
    sendATCommand("AT+HTTPTERM", 2000);
    return false;
  }
  if (total == 0) {
    DEBUG_PRINTLN("[OTA] Firmware size unknown; aborting for safety");
    sendATCommand("AT+HTTPTERM", 2000);
    return false;
  }

  if (!Update.begin(total, U_FLASH)) {
    DEBUG_PRINTLN("[OTA] Update.begin failed (not enough space?)");
    sendATCommand("AT+HTTPTERM", 2000);
    return false;
  }

  bool hasExpectedSha = expectedSha256Hex.length() == 64;
  uint8_t expectedSha[32];
  if (hasExpectedSha && !parseHexSha256(expectedSha256Hex, expectedSha)) {
    DEBUG_PRINTLN("[OTA] Invalid sha256 in manifest; aborting");
    Update.abort();
    sendATCommand("AT+HTTPTERM", 2000);
    return false;
  }

  mbedtls_sha256_context shaCtx;
  mbedtls_sha256_init(&shaCtx);
  mbedtls_sha256_starts_ret(&shaCtx, 0);

  const size_t chunk = (size_t)Config::OTA_HTTPREAD_CHUNK_BYTES;
  uint8_t buf[1024];
  size_t offset = 0;
  while (offset < total) {
    size_t want = chunk;
    if (offset + want > total) want = total - offset;

    size_t got = 0;
    if (!httpReadChunk(offset, want, buf, got, 30000UL) || got == 0) {
      DEBUG_PRINTLN("[OTA] HTTPREAD failed during firmware download");
      Update.abort();
      sendATCommand("AT+HTTPTERM", 2000);
      mbedtls_sha256_free(&shaCtx);
      return false;
    }

    mbedtls_sha256_update_ret(&shaCtx, buf, got);
    size_t written = Update.write(buf, got);
    if (written != got) {
      DEBUG_PRINTLN("[OTA] Flash write failed");
      Update.abort();
      sendATCommand("AT+HTTPTERM", 2000);
      mbedtls_sha256_free(&shaCtx);
      return false;
    }

    offset += got;
    if ((offset % (64UL * 1024UL)) < got) {
      DEBUG_PRINTF("[OTA] Downloaded %u/%u bytes\n", (unsigned)offset,
                   (unsigned)total);
    }
  }

  uint8_t actualSha[32];
  mbedtls_sha256_finish_ret(&shaCtx, actualSha);
  mbedtls_sha256_free(&shaCtx);

  if (hasExpectedSha && !sha256HexEqual(actualSha, expectedSha)) {
    DEBUG_PRINTLN("[OTA] SHA256 mismatch; aborting update");
    Update.abort();
    sendATCommand("AT+HTTPTERM", 2000);
    return false;
  }

  if (!Update.end(true)) {
    DEBUG_PRINTLN("[OTA] Update.end failed");
    sendATCommand("AT+HTTPTERM", 2000);
    return false;
  }

  sendATCommand("AT+HTTPTERM", 2000);
  DEBUG_PRINTLN("[OTA] Update applied. Rebooting...");
  delay(1000);
  ESP.restart();
  return true;
}
}  // namespace

bool otaCheckAndUpdate() {
  if (!Config::OTA_ENABLED) return false;
  if (String(Config::OTA_MANIFEST_URL).length() == 0) {
    DEBUG_PRINTLN("[OTA] OTA enabled but OTA_MANIFEST_URL is empty");
    return false;
  }

  DEBUG_PRINTLN("[OTA] Checking for update...");
  String manifest;
  if (!httpGetToString(String(Config::OTA_MANIFEST_URL), manifest)) {
    DEBUG_PRINTLN("[OTA] Failed to fetch manifest");
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, manifest);
  if (err) {
    DEBUG_PRINTLN("[OTA] Manifest JSON parse failed");
    return false;
  }

  String version = doc["version"].as<String>();
  String url = doc["url"].as<String>();
  size_t size = doc["size"].as<size_t>();
  String sha256 = doc["sha256"].as<String>();

  if (version.length() == 0 || url.length() == 0) {
    DEBUG_PRINTLN("[OTA] Manifest missing version/url");
    return false;
  }

  if (version == String(Config::FIRMWARE_VERSION)) {
    DEBUG_PRINTLN("[OTA] Already up to date");
    return false;
  }

  DEBUG_PRINTLN("[OTA] New firmware available: " + version);
  return downloadAndFlashFirmware(url, size, sha256);
}


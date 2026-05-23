#include "firebase_auth.h"

#include <ArduinoJson.h>

#include "../helpers/helpers.h"
#include "../modem/modem.h"

#if defined(__has_include) && __has_include("secrets.h")
#  include "secrets.h"
#else
namespace Secrets {
constexpr const char* FIREBASE_API_KEY = "";
constexpr const char* FIREBASE_AUTH_EMAIL = "";
constexpr const char* FIREBASE_AUTH_PASSWORD = "";
}  // namespace Secrets
#endif

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

bool httpPostJsonToString(const String& url, const String& jsonBody,
                          String& outBody, int& outStatus) {
  outBody = "";
  outStatus = -1;

  sendATCommand("AT+HTTPTERM", 2000);
  delay(200);
  sendATCommand("AT+HTTPINIT", 5000);
  delay(200);

  // Optional settings; some firmware returns ERROR but can still proceed.
  sendATCommand("AT+HTTPPARA=\"CID\",1", 2000);
  sendATCommand("AT+HTTPSSL=1", 2000);

  String urlCmd = "AT+HTTPPARA=\"URL\",\"" + url + "\"";
  sendATCommand(urlCmd, 5000);
  sendATCommand("AT+HTTPPARA=\"CONTENT\",\"application/json\"", 2000);

  String dataCmd = "AT+HTTPDATA=" + String(jsonBody.length()) + ",30000";
  SerialAT.println(dataCmd);
  if (!waitForPrompt("DOWNLOAD", 15000)) {
    DEBUG_PRINTLN("[AUTH] HTTPDATA not accepted (no DOWNLOAD)");
    sendATCommand("AT+HTTPTERM", 2000);
    return false;
  }
  SerialAT.print(jsonBody);
  delay(1000);

  SerialAT.println("AT+HTTPACTION=1");  // POST
  HttpActionResult action;
  if (!waitForHttpAction(60000UL, action)) {
    DEBUG_PRINTLN("[AUTH] HTTPACTION timeout");
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

bool secretsPresent() {
  return String(Secrets::FIREBASE_API_KEY).length() > 0 &&
         String(Secrets::FIREBASE_AUTH_EMAIL).length() > 0 &&
         String(Secrets::FIREBASE_AUTH_PASSWORD).length() > 0;
}

String cachedIdToken;
unsigned long cachedTokenExpiryMs = 0;
}  // namespace

bool firebaseEnsureIdToken(String& outIdToken) {
  const unsigned long now = millis();
  if (cachedIdToken.length() > 0 && cachedTokenExpiryMs > now) {
    outIdToken = cachedIdToken;
    return true;
  }

  if (!secretsPresent()) {
    DEBUG_PRINTLN("[AUTH] Missing secrets (API key / email / password)");
    return false;
  }

  const String url =
      "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=" +
      String(Secrets::FIREBASE_API_KEY);

  JsonDocument req;
  req["email"] = Secrets::FIREBASE_AUTH_EMAIL;
  req["password"] = Secrets::FIREBASE_AUTH_PASSWORD;
  req["returnSecureToken"] = true;
  String body;
  serializeJson(req, body);

  DEBUG_PRINTLN("[AUTH] Signing in to Firebase...");
  String resp;
  int status = -1;
  if (!httpPostJsonToString(url, body, resp, status)) {
    DEBUG_PRINTLN("[AUTH] Sign-in request failed");
    return false;
  }

  if (status != 200) {
    DEBUG_PRINTF("[AUTH] Sign-in HTTP status %d\n", status);
    if (resp.length()) DEBUG_PRINTLN("[AUTH] Details: " + resp);
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, resp);
  if (err) {
    DEBUG_PRINTLN("[AUTH] Sign-in JSON parse failed");
    return false;
  }

  const char* idToken = doc["idToken"];
  const char* expiresIn = doc["expiresIn"];  // string seconds
  if (!idToken || String(idToken).length() == 0) {
    DEBUG_PRINTLN("[AUTH] Sign-in response missing idToken");
    return false;
  }

  unsigned long expiresSec = 0;
  if (expiresIn) expiresSec = (unsigned long)String(expiresIn).toInt();
  if (expiresSec == 0) expiresSec = 3600;

  // Refresh 60 seconds early.
  cachedIdToken = String(idToken);
  cachedTokenExpiryMs = now + ((expiresSec > 60 ? (expiresSec - 60) : expiresSec) * 1000UL);

  outIdToken = cachedIdToken;
  DEBUG_PRINTLN("[AUTH] Signed in OK");
  return true;
}

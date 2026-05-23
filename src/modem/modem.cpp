#include "modem.h"
#include "../helpers/helpers.h"
#include "../gps/gps.h" 

const char apn[] = "internet";

namespace {
bool responseHasOk(const String& response) { return response.indexOf("OK") >= 0; }
int parseRegStat(const String& resp, const char* prefix);

String humanCmdLabel(const String& cmd) {
  if (cmd == "AT") return "Modem check";
  if (cmd == "ATE0") return "Modem setup";
  if (cmd.startsWith("AT+CPIN")) return "SIM check";
  if (cmd.startsWith("AT+CSQ")) return "Signal check";
  if (cmd.startsWith("AT+COPS")) return "Network selection";
  if (cmd.startsWith("AT+CREG") || cmd.startsWith("AT+CGREG") ||
      cmd.startsWith("AT+CEREG"))
    return "Network registration";
  if (cmd.startsWith("AT+CGDCONT")) return "APN setup";
  if (cmd.startsWith("AT+CGATT")) return "Mobile data attach";
  if (cmd.startsWith("AT+CGACT=0")) return "Mobile data reset";
  if (cmd.startsWith("AT+CGACT=1")) return "Mobile data on";
  if (cmd.startsWith("AT+CGPADDR")) return "IP address";
  if (cmd.startsWith("AT+CTZU") || cmd.startsWith("AT+CLTS")) return "Time sync setting";
  if (cmd.startsWith("AT+HTTPINIT")) return "HTTP start";
  if (cmd.startsWith("AT+HTTPTERM")) return "HTTP stop";
  if (cmd.startsWith("AT+HTTPSSL")) return "HTTPS setting";
  if (cmd.startsWith("AT+HTTPPARA")) return "HTTP setting";
  if (cmd.startsWith("AT+HTTPDATA")) return "Preparing upload";
  if (cmd.startsWith("AT+HTTPACTION")) return "Sending upload";
  if (cmd.startsWith("AT+CDNSCFG")) return "DNS setting";
  return "Modem command";
}

String firstNonEmptyLine(const String& s) {
  int start = 0;
  while (start < (int)s.length()) {
    while (start < (int)s.length() && (s[start] == '\r' || s[start] == '\n' || s[start] == ' ')) start++;
    if (start >= (int)s.length()) break;
    int end = s.indexOf('\n', start);
    if (end < 0) end = s.length();
    String line = s.substring(start, end);
    line.trim();
    if (line.length() > 0) return line;
    start = end + 1;
  }
  return "";
}

String summarizeCmeError(const String& resp) {
  int idx = resp.indexOf("+CME ERROR:");
  if (idx < 0) idx = resp.indexOf("+CMS ERROR:");
  if (idx < 0) return "";
  int end = resp.indexOf('\n', idx);
  if (end < 0) end = resp.length();
  String line = resp.substring(idx, end);
  line.trim();
  return line;
}

String regStatToEnglish(int stat) {
  switch (stat) {
    case 0: return "Not connected";
    case 1: return "Connected";
    case 2: return "Connecting...";
    case 3: return "Blocked (SIM/network)";
    case 4: return "Unknown";
    case 5: return "Connected (roaming)";
    default: return "registration status unknown";
  }
}

String summarizeAtResponse(const String& cmd, const String& resp) {
  if (resp.length() == 0) return "no response";

  // Prefer specific modem errors if present.
  String cme = summarizeCmeError(resp);
  if (cme.length() > 0) return cme;

  const bool hasOk = resp.indexOf("OK") >= 0;
  const bool hasErr = resp.indexOf("ERROR") >= 0;

  if (cmd == "AT") return hasOk ? "OK" : (hasErr ? "Failed" : "Reply received");
  if (cmd == "ATE0") return hasOk ? "OK" : (hasErr ? "Failed" : "Reply received");

  if (cmd.startsWith("AT+CPIN")) {
    if (resp.indexOf("+CPIN: READY") >= 0) return "SIM is ready";
    if (resp.indexOf("SIM busy") >= 0) return "SIM is still starting";
    String line = firstNonEmptyLine(resp);
    return line.length() ? line : (hasOk ? "SIM OK" : "SIM reply");
  }

  if (cmd.startsWith("AT+CSQ")) {
    int rssi = -1, ber = -1;
    int p = resp.indexOf("+CSQ:");
    if (p >= 0) {
      if (sscanf(resp.substring(p).c_str(), "+CSQ: %d,%d", &rssi, &ber) == 2) {
        if (rssi == 99) return "No signal";
        if (rssi <= 9) return "Very weak signal";
        if (rssi <= 14) return "Weak signal";
        if (rssi <= 20) return "OK signal";
        return "Strong signal";
      }
    }
    return hasOk ? "Signal checked" : "Signal reply";
  }

  if (cmd.startsWith("AT+CEREG") || cmd.startsWith("AT+CREG") || cmd.startsWith("AT+CGREG")) {
    const char* prefix = cmd.startsWith("AT+CEREG") ? "+CEREG" : (cmd.startsWith("AT+CGREG") ? "+CGREG" : "+CREG");
    int stat = parseRegStat(resp, prefix);
    if (stat >= 0) return regStatToEnglish(stat);
    return hasOk ? "Checked" : "Reply";
  }

  if (cmd.startsWith("AT+CGATT")) return hasOk ? "Mobile data attached" : (hasErr ? "Attach failed" : "Reply");
  if (cmd.startsWith("AT+CGACT=0")) return hasOk ? "Mobile data turned off" : (hasErr ? "Turn off failed" : "Reply");
  if (cmd.startsWith("AT+CGACT=1")) return hasOk ? "Mobile data turned on" : (hasErr ? "Turn on failed" : "Reply");

  if (cmd.startsWith("AT+CGPADDR")) return hasOk ? "IP address OK" : (hasErr ? "No IP address" : "IP reply");

  if (cmd.startsWith("AT+CGDCONT")) return hasOk ? "APN set" : (hasErr ? "APN failed" : "APN reply");
  if (cmd.startsWith("AT+COPS")) return hasOk ? "Network selected" : (hasErr ? "Network select failed" : "Reply");

  if (cmd.startsWith("AT+HTTPINIT")) return hasOk ? "HTTP ready" : (hasErr ? "HTTP failed" : "Reply");
  if (cmd.startsWith("AT+HTTPTERM")) return hasOk ? "HTTP stopped" : (hasErr ? "HTTP stop failed" : "Reply");
  if (cmd.startsWith("AT+HTTPSSL")) return hasOk ? "HTTPS on" : (hasErr ? "HTTPS setting failed" : "Reply");
  if (cmd.startsWith("AT+HTTPPARA")) return hasOk ? "HTTP set" : (hasErr ? "HTTP setting failed" : "Reply");
  if (cmd.startsWith("AT+HTTPDATA")) return resp.indexOf("DOWNLOAD") >= 0 ? "Ready to send data" : (hasErr ? "Not ready to send data" : "Reply");
  if (cmd.startsWith("AT+HTTPACTION")) return hasOk ? "Sending..." : (hasErr ? "Send failed" : "Reply");

  if (cmd.startsWith("AT+CDNSCFG")) return hasOk ? "DNS set" : (hasErr ? "DNS setting failed" : "Reply");

  // Generic fallback.
  if (hasOk && !hasErr) return "OK";
  if (hasErr && !hasOk) return "ERROR";
  return "reply received";
}

int parseRegStat(const String& resp, const char* prefix) {
  // Parses lines like: +CEREG: <n>,<stat>[,...]
  // Returns stat on success, -1 otherwise.
  int idx = resp.indexOf(prefix);
  if (idx < 0) return -1;
  int n = -1;
  int stat = -1;
  // Find the start of numbers after ':'
  int colon = resp.indexOf(':', idx);
  if (colon < 0) return -1;
  String tail = resp.substring(colon + 1);
  // sscanf will ignore leading spaces/newlines.
  if (sscanf(tail.c_str(), " %d,%d", &n, &stat) == 2) return stat;
  // Some firmwares omit <n>: +CREG: <stat>
  if (sscanf(tail.c_str(), " %d", &stat) == 1) return stat;
  return -1;
}

bool isRegisteredStat(int stat) {
  // 1=home, 5=roaming.
  return stat == 1 || stat == 5;
}

String atWithResponse(const String& cmd, unsigned long timeoutMs) {
  // Drain to avoid mixing.
  while (SerialAT.available()) SerialAT.read();
  SerialAT.println(cmd);
  unsigned long start = millis();
  String resp;
  while (millis() - start < timeoutMs) {
    while (SerialAT.available()) resp += (char)SerialAT.read();
    if (resp.indexOf("OK") >= 0 || resp.indexOf("ERROR") >= 0) break;
    delay(10);
  }
  DEBUG_PRINTLN("[MODEM] " + humanCmdLabel(cmd) + ": " + summarizeAtResponse(cmd, resp));
  if (resp.indexOf("ERROR") >= 0 || resp.indexOf("+CME ERROR:") >= 0 || resp.indexOf("+CMS ERROR:") >= 0) {
    DEBUG_PRINTLN("[MODEM] Details: " + firstNonEmptyLine(resp));
  }
  return resp;
}
}  // namespace

void powerOnModem() {
  pinMode(MODEM_POWER_ON, OUTPUT);
  digitalWrite(MODEM_POWER_ON, HIGH);
  delay(100);

  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(500);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(3000);
}

void sendATCommand(String cmd, unsigned long timeout) {
  // Drain any pending bytes to reduce chance of mixing responses between commands.
  while (SerialAT.available()) SerialAT.read();
  SerialAT.println(cmd);
  unsigned long start = millis();
  String response = "";
  while (millis() - start < timeout) {
    while (SerialAT.available()) {
      char c = SerialAT.read();
      response += c;
    }
    if (response.indexOf("OK") > 0 || response.indexOf("ERROR") > 0) break;
  }
  DEBUG_PRINTLN("[MODEM] " + humanCmdLabel(cmd) + ": " + summarizeAtResponse(cmd, response));
  if (response.indexOf("ERROR") >= 0 || response.indexOf("+CME ERROR:") >= 0 || response.indexOf("+CMS ERROR:") >= 0) {
    DEBUG_PRINTLN("[MODEM] Details: " + firstNonEmptyLine(response));
  }
}

String sendATCommandWithResponse(String cmd, unsigned long timeout) {
  SerialAT.println(cmd);
  unsigned long start = millis();
  String response = "";
  while (millis() - start < timeout) {
    while (SerialAT.available()) {
      char c = SerialAT.read();
      response += c;
    }
    if (response.indexOf("OK") > 0 || response.indexOf("ERROR") > 0) break;
  }
  return response;
}

bool connectNetwork() {
  // Wait for modem to respond to AT (it may still be booting).
  bool modemReady = false;
  for (int i = 0; i < 10; ++i) {
    String r = atWithResponse("AT", 1000);
    if (responseHasOk(r)) {
      modemReady = true;
      break;
    }
    delay(500);
  }
  if (!modemReady) {
    DEBUG_PRINTLN("[ERROR] Modem not responding to AT");
    return false;
  }

  atWithResponse("ATE0", 1000);

  // Wait for SIM ready (avoid +CME ERROR: SIM busy).
  bool simReady = false;
  unsigned long simStart = millis();
  while (millis() - simStart < 30000UL) {
    String r = atWithResponse("AT+CPIN?", 2000);
    if (r.indexOf("+CPIN: READY") >= 0) {
      simReady = true;
      break;
    }
    delay(1000);
  }
  if (!simReady) {
    DEBUG_PRINTLN("[ERROR] SIM not ready (CPIN)");
    return false;
  }

  atWithResponse("AT+CSQ", 2000);

  // Force automatic operator selection (helps recover after brownouts).
  atWithResponse("AT+COPS=0", 10000);

  String apnCmd = "AT+CGDCONT=1,\"IP\",\"" + String(apn) + "\"";
  atWithResponse(apnCmd, 2000);

  // Wait for network registration (without this, CGACT/HTTP will fail).
  bool registered = false;
  unsigned long regStart = millis();
  while (millis() - regStart < 60000UL) {
    String cereg = atWithResponse("AT+CEREG?", 2000);
    int stat = parseRegStat(cereg, "+CEREG");
    if (isRegisteredStat(stat)) {
      registered = true;
      break;
    }

    // Fallback checks (some firmwares report only CREG/CGREG).
    String creg = atWithResponse("AT+CREG?", 2000);
    int cstat = parseRegStat(creg, "+CREG");
    if (isRegisteredStat(cstat)) {
      registered = true;
      break;
    }

    String cgreg = atWithResponse("AT+CGREG?", 2000);
    int gstat = parseRegStat(cgreg, "+CGREG");
    if (isRegisteredStat(gstat)) {
      registered = true;
      break;
    }

    delay(2000);
  }

  if (!registered) {
    DEBUG_PRINTLN("[ERROR] Not registered on network (CREG/CGREG/CEREG)");
    return false;
  }

  // Attach and activate data context.
  atWithResponse("AT+CGATT=1", 5000);
  String act = atWithResponse("AT+CGACT=1,1", 5000);
  if (!responseHasOk(act)) {
    DEBUG_PRINTLN("[ERROR] Failed to activate PDP context (CGACT).");
    return false;
  }

  atWithResponse("AT+CGPADDR=1", 2000);

  // Optional time update flags (not supported on all modems; ignore errors).
  atWithResponse("AT+CTZU=1", 2000);
  atWithResponse("AT+CLTS=1", 2000);

  return true;
}

bool waitForPrompt(const char* prompt, unsigned long timeout) {
  unsigned long start = millis();
  String response = "";
  while (millis() - start < timeout) {
    if (SerialAT.available()) {
      char c = SerialAT.read();
      response += c;
      if (response.indexOf(prompt) >= 0) return true;
      if (response.indexOf("ERROR") >= 0) {
        DEBUG_PRINTLN("[AT] waitForPrompt saw ERROR. Buffer:");
        DEBUG_PRINTLN("[AT] " + response);
        return false;
      }
    }
    delay(10);
  }
  if (response.length() > 0) {
    DEBUG_PRINTLN("[AT] waitForPrompt timeout. Buffer:");
    DEBUG_PRINTLN("[AT] " + response);
  } else {
    DEBUG_PRINTLN("[AT] waitForPrompt timeout (no data).");
  }
  return false;
}

String getModemTime() {
  stopGPSStreaming();
  SerialAT.println("AT+CCLK?");
  unsigned long start = millis();
  String response = "";
  while (millis() - start < 2000) {
    while (SerialAT.available()) {
      char c = SerialAT.read();
      response += c;
    }
    if (response.indexOf("OK") > 0 || response.indexOf("ERROR") > 0) break;
  }
  int startIdx = response.indexOf("\"");
  int endIdx = response.lastIndexOf("\"");
  if (startIdx >= 0 && endIdx > startIdx) {
    String timeStr = response.substring(startIdx + 1, endIdx);
    if (timeStr.length() >= 17) {
      String year = "20" + timeStr.substring(0, 2);
      String month = timeStr.substring(3, 5);
      String day = timeStr.substring(6, 8);
      String time = timeStr.substring(9, 17);
      return year + "-" + month + "-" + day + " " + time;
    }
  }
  DEBUG_PRINTLN("Failed to parse modem time");
  return "TIME_ERROR_" + String(millis());
}

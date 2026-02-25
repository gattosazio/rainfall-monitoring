#include "modem.h"
#include "../helpers/helpers.h"
#include "../gps/gps.h" 

const char apn[] = "internet";

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
  sendATCommand("AT", 1000);
  sendATCommand("ATE0", 1000);
  sendATCommand("AT+CPIN?", 2000);
  String apnCmd = "AT+CGDCONT=1,\"IP\",\"" + String(apn) + "\"";
  sendATCommand(apnCmd, 2000);
  sendATCommand("AT+CGACT=1,1", 5000);
  sendATCommand("AT+CREG?", 2000);
  sendATCommand("AT+CGREG?", 2000);
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
    }
    delay(10);
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
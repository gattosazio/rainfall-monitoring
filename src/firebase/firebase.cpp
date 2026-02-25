#include "firebase.h"
#include "../helpers/helpers.h"
#include "../modem/modem.h"
#include "../gps/gps.h"
#include "../raingauge/raingauge.h"
#include "../ultrasonic/ultrasonic.h"
#include "../domain/config.h" 



bool sendToFirebase(String timestamp, float rainfall, float distance) {
  DEBUG_PRINTLN("[DEBUG] Starting Firebase send...");
  stopGPSStreaming();

  String label = getWaterLabel();
  String payload = "{\"Timestamp\":\"" + timestamp +
                   "\",\"Rainfall\":" + String(rainfall, 2) +
                   ",\"RainRate\":" + String(rainRate, 2) +
                   ",\"WaterLevel\":" + String(distance, 1) +
                   ",\"LevelLabel\":\"" + label + "\"" +
                   ",\"Latitude\":" + String(gpsLat, 6) +
                   ",\"Longitude\":" + String(gpsLon, 6) +
                   ",\"Altitude\":" + String(gpsAlt, 2) +
                  "}";

  DEBUG_PRINTLN("[DEBUG] Payload: " + payload);

  sendATCommand("AT+CGACT=0,1", 5000);
  delay(1000);
  sendATCommand("AT+CGACT=1,1", 5000);
  delay(1000);
  sendATCommand("AT+HTTPTERM", 2000);
  delay(1000);
  sendATCommand("AT+HTTPINIT", 5000);
  delay(1000);

  sendATCommand("AT+HTTPPARA=\"CID\",1", 2000);
  sendATCommand("AT+HTTPSSL=1", 2000);
    String urlCmd = "AT+HTTPPARA=\"URL\",\"" + String(FIREBASE_URL) + "\"";
  sendATCommand(urlCmd, 2000);
  sendATCommand("AT+HTTPPARA=\"CONTENT\",\"application/json\"", 2000);

  String dataCmd = "AT+HTTPDATA=" + String(payload.length()) + ",30000";
  SerialAT.println(dataCmd);
  delay(2000);

  if (!waitForPrompt("DOWNLOAD", 10000)) {
    DEBUG_PRINTLN("[ERROR] DOWNLOAD prompt timeout");
    sendATCommand("AT+HTTPTERM", 2000);
    return false;
  }

  SerialAT.println(payload);
  delay(3000);
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
  DEBUG_PRINT("[DEBUG] Firebase response check: ");
  DEBUG_PRINTLN(success ? "SUCCESS" : "FAILED");
  return success;
}
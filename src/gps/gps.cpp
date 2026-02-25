#include "gps.h"
#include "../helpers/helpers.h"
#include "../modem/modem.h"

#include "gps.h"

TinyGPSPlus gps;
float gpsLat = 0.0;
float gpsLon = 0.0;
float gpsAlt = 0.0;
bool gpsEnabled = false;
bool gpsStreaming = false;


void stopGPSStreaming() {
  if (gpsStreaming) {
    DEBUG_PRINTLN(" Stopping GPS streaming...");
    SerialAT.println("AT+CGNSSTST=0");
    delay(1000);
    while (SerialAT.available()) { SerialAT.read(); }
    gpsStreaming = false;
    DEBUG_PRINTLN("GPS streaming stopped");
  }
}

void startGPSStreaming() {
  if (gpsEnabled && !gpsStreaming) {
    DEBUG_PRINTLN("Starting GPS streaming...");
    SerialAT.println("AT+CGNSSTST=1");
    delay(500);
    gpsStreaming = true;
    DEBUG_PRINTLN("GPS streaming started");
  }
}

bool enableGPS() {
  DEBUG_PRINTLN("📡 Enabling GPS (A7670E method)...");
  SerialAT.println("AT+CGNSSPWR=1");
  unsigned long start = millis();
  String response = "";
  bool ready = false;
  
  while (millis() - start < 15000) {
    while (SerialAT.available()) {
      char c = SerialAT.read();
      response += c;
    }
    if (response.indexOf("+CGNSSPWR: READY!") >= 0) {
      ready = true;
      break;
    }
    delay(100);
  }
  
  if (!ready) {
    DEBUG_PRINTLN("GNSS power on timeout!");
    return false;
  }
  
  DEBUG_PRINTLN("GNSS powered on and ready");
  delay(500);
  
  SerialAT.println("AT+CGNSSPORTSWITCH=1,1");
  start = millis();
  response = "";
  while (millis() - start < 2000) {
    while (SerialAT.available()) {
      response += SerialAT.read();
    }
    if (response.indexOf("OK") >= 0) break;
    delay(100);
  }
  delay(500);
  
  gpsEnabled = true;
  DEBUG_PRINTLN("GPS enabled (streaming will be controlled)");
  return true;
}

bool updateGPSLocation() {
  if (!gpsEnabled) {
    DEBUG_PRINTLN(" GPS not enabled, enabling now...");
    if (!enableGPS()) return false;
  }
  
  startGPSStreaming();
  unsigned long start = millis();
  bool newData = false;
  
  while (millis() - start < 8000) {
    while (SerialAT.available()) {
      char c = SerialAT.read();
      if (gps.encode(c)) { newData = true; }
    }
    if (newData && gps.location.isValid()) { break; }
    delay(10);
  }
  
  stopGPSStreaming();
  
  if (gps.location.isValid()) {
    gpsLat = gps.location.lat();
    gpsLon = gps.location.lng();
    gpsAlt = gps.altitude.meters();
    
    DEBUG_PRINT(" GPS Fix -> Lat: "); DEBUG_PRINT_F(gpsLat, 6);
    DEBUG_PRINT(", Lon: "); DEBUG_PRINT_F(gpsLon, 6);
    DEBUG_PRINT(", Alt: "); DEBUG_PRINT_F(gpsAlt, 2);
    DEBUG_PRINT(" m, Satellites: "); DEBUG_PRINTLN(gps.satellites.value());
    return true;
  } else {
    DEBUG_PRINTLN("No valid GPS fix yet");
    return false;
  }
}

void printGPSDiagnostics() {
  DEBUG_PRINTLN("\n--- GPS Diagnostics ---");
  DEBUG_PRINT("GPS Enabled: "); DEBUG_PRINTLN(gpsEnabled ? "Yes" : "No");
  DEBUG_PRINT("GPS Streaming: "); DEBUG_PRINTLN(gpsStreaming ? "Yes" : "No");
  DEBUG_PRINT("Characters processed: "); DEBUG_PRINTLN(gps.charsProcessed());
  DEBUG_PRINT("Sentences with fix: "); DEBUG_PRINTLN(gps.sentencesWithFix());
  DEBUG_PRINT("Failed checksum: "); DEBUG_PRINTLN(gps.failedChecksum());
  DEBUG_PRINT("Satellites: "); DEBUG_PRINTLN(gps.satellites.value());
  DEBUG_PRINT("HDOP: "); DEBUG_PRINTLN(gps.hdop.hdop());
  
  if (gps.satellites.value() == 0) {
    DEBUG_PRINTLN(" NO SATELLITES DETECTED!");
  }
  DEBUG_PRINTLN("----------------------\n");
}
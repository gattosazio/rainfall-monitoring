#include <Arduino.h>
#include "FS.h"
#include "SPIFFS.h"
#include "BluetoothSerial.h"
#include "esp_sleep.h"
#include "TinyGPSPlus.h"
#include "driver/rtc_io.h"
#include "esp_timer.h" 


BluetoothSerial SerialBT;
TinyGPSPlus gps;

// ============================================
//              UNIVERSAL DEBUG MACROS
// ============================================
#define DEBUG_PRINT(x)     { Serial.print(x); SerialBT.print(x); }
#define DEBUG_PRINTLN(x)   { Serial.println(x); SerialBT.println(x); }
#define DEBUG_PRINTF(...)  { Serial.printf(__VA_ARGS__); SerialBT.printf(__VA_ARGS__); }
#define DEBUG_PRINT_F(x,p) { Serial.print(x,p); SerialBT.print(x,p); }

// ============================================
//         MODEM PINS (LILYGO T-A7670E)
// ============================================
#define SerialAT Serial1
#define MODEM_TX 26
#define MODEM_RX 27
#define MODEM_PWRKEY 4
#define MODEM_POWER_ON 4  
#define MODEM_DTR 25

// ============================================
//                GPS SETTINGS
// ============================================
float gpsLat = 0.0;
float gpsLon = 0.0;
float gpsAlt = 0.0;
bool gpsEnabled = false;
bool gpsStreaming = false;
unsigned long lastGPSUpdate = 0;
const unsigned long GPS_UPDATE_INTERVAL = 30000;

// ============================================
//                SENSOR PINS
// ============================================
#define TIP_PIN 33 
#define TRIG_PIN 18
#define ECHO_PIN 32

// ============================================
//              ULTRASONIC SETTINGS
// ============================================
const float MAX_VALID_CM = 600.0;
float calOffsetCM = 0.0;
float ambientTempC = 25.0;
float lastValidDistance = 0.0;
const unsigned long PRINT_INTERVAL = 1000;
const int SAMPLES = 7;
const float MIN_VALID_CM = 20.0;

// ============================================
//                TIMING SETTINGS
// ============================================
const unsigned long SENSOR_READ_INTERVAL = 3000;
unsigned long lastSensorRead = 0;

// ✨ NEW: Periodic Firebase send interval
const unsigned long FIREBASE_SEND_INTERVAL = 300000UL;  // 5 minutes in milliseconds
unsigned long lastFirebaseSend = 0;

// ============================================
//                NETWORK SETTINGS
// ============================================
const char apn[] = "internet";
const char user[] = "";
const char pass[] = "";

// ============================================
//               FIREBASE SETTINGS
// ============================================
const char FIREBASE_URL[] = "https://lte-test2-default-rtdb.asia-southeast1.firebasedatabase.app/Node2.json";

// ============================================
//          HIBERNATION MODE FUNCTIONS 
// ============================================
const unsigned long ACTIVE_DURATION_MS = 7200000UL;  // 2 hours in milliseconds
unsigned long bootTime = 0;

void enterHibernation() {
  DEBUG_PRINTLN("\n❄️ Entering Hibernation Mode (Ultra Deep Sleep)...");
  delay(200);

  if (gpsStreaming) {
    SerialAT.println("AT+CGNSSTST=0");
    delay(500);
  }
  
  if (gpsEnabled) {
    SerialAT.println("AT+CGNSSPWR=0");
    delay(500);
  }

  digitalWrite(MODEM_POWER_ON, LOW);
  delay(100);

  rtc_gpio_init((gpio_num_t)TIP_PIN);
  rtc_gpio_set_direction((gpio_num_t)TIP_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en((gpio_num_t)TIP_PIN);
  rtc_gpio_pulldown_dis((gpio_num_t)TIP_PIN);

  esp_sleep_enable_ext0_wakeup((gpio_num_t)TIP_PIN, 0);

  DEBUG_PRINTLN("🌧️ Wake-up source: Rain Gauge tip (with pull-up)");
  DEBUG_PRINTLN("❄️ Entering hibernation...\n");
  
  delay(200);
  
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
  
  esp_deep_sleep_start();
}

// ============================================
//        RAIN GAUGE FUNCTION & SETTINGS
// ============================================
#define MM_PER_TIP 0.70f
#define DEBOUNCE_MS 500
#define RAIN_WINDOW_MS 600000UL // 10-minute rolling window (600k ms)

// Buffer to store tip timestamps
#define MAX_TIPS 300
volatile unsigned long tipTimes[MAX_TIPS];
volatile int headIndex = 0;

volatile unsigned long lastTipTime = 0;
volatile unsigned long lastTipGap = 0;
volatile int tipCount = 0;
volatile int ignoredTips = 0;
volatile bool gaugeInitialized = false;

int lastSentTipCount = 0; 
float totalRainfall = 0.0f;
float rainRate = 0.0f;

// ISR for rain gauge tip
void IRAM_ATTR tipISR() {
  unsigned long now = millis();

  if (now - lastTipTime > DEBOUNCE_MS) {
    if (!gaugeInitialized || ignoredTips < 2) {
      ignoredTips++;
    } else {
      tipCount++;

      tipTimes[headIndex] = now;
      headIndex = (headIndex + 1) % MAX_TIPS;
    }
    lastTipTime = now;
  }
}

void updateRainfall() {
  static int lastTips = 0;

  int newTips = tipCount - lastTips;
  if (newTips > 0) {
    totalRainfall += newTips * MM_PER_TIP;
    lastTips = tipCount;
  }

  // ===== RAIN RATE CALCULATION =====
  unsigned long now = millis();
  int tipsInWindow = 0;

  for (int i = 0; i < MAX_TIPS; i++) {
    if (tipTimes[i] > 0 && (now - tipTimes[i]) <= RAIN_WINDOW_MS) {
      tipsInWindow++;
    }
  }

  if (tipsInWindow == 0) {
    rainRate = 0.0f;
  } else {
    float hours = (float)RAIN_WINDOW_MS / 3600000.0f;
    rainRate = (tipsInWindow * MM_PER_TIP) / hours; // mm/hr
  }
}

// ============================================
//             ULTRASONIC FUNCTIONS
// ============================================
#define ULTRA_SAMPLES      21   // More samples for shallow water stability
#define ULTRA_SAMPLE_DELAY 100   // Slightly longer delay

#define SENSOR_HEIGHT_CM   225.0f  
#define CANAL_DEPTH_CM      55.0f

// Thresholds
#define PCT_SAFE     25.0f
#define PCT_MODERATE 50.0f
#define PCT_HIGH     75.0f

// ============================================
//           ULTRASONIC CORE FUNCTIONS
// ============================================
float speedCmPerUs(float tempC = 25.0f) {
  float speed_m_s = 331.4f + 0.606f * tempC;
  return speed_m_s * 0.0001f;
}

long singlePulse() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  // Extended timeout for full range
  return pulseIn(ECHO_PIN, HIGH, 80000);
}

float durationToCM(long dur_us, float speed) {
  if (dur_us <= 0) return -1.0f;
  return (dur_us * speed) / 2.0f;
}

// ============================================
//      IMPROVED FILTERING FOR RUNNING WATER
// ============================================
float getDistanceCM_refined() {
  float speed = speedCmPerUs(ambientTempC);
  float readings[ULTRA_SAMPLES];
  int count = 0;

  // Collect samples
  for (int i = 0; i < ULTRA_SAMPLES; ++i) {
    long dur = singlePulse();
    float cm = durationToCM(dur, speed);
    
    // Very lenient validation - don't restrict range
    if (cm > 5.0f && cm <= 450.0f) {
      readings[count++] = cm;
    }
    delay(ULTRA_SAMPLE_DELAY);
  }

  // Need at least 5 valid readings
  if (count < 5) {
    Serial.print("Warning: Only ");
    Serial.print(count);
    Serial.print("/");
    Serial.print(ULTRA_SAMPLES);
    Serial.println(" valid readings");
    
    if (count == 0) return -1.0f;
  }

  // Sort readings
  for (int i = 0; i < count - 1; ++i) {
    for (int j = i + 1; j < count; ++j) {
      if (readings[j] < readings[i]) {
        float t = readings[i];
        readings[i] = readings[j];
        readings[j] = t;
      }
    }
  }

  // Use median for running water stability
  float median = readings[count / 2];

  // Calculate MAD (Median Absolute Deviation) - more robust than AAD
  float deviations[ULTRA_SAMPLES];
  for (int i = 0; i < count; ++i) {
    deviations[i] = fabs(readings[i] - median);
  }
  
  // Sort deviations
  for (int i = 0; i < count - 1; ++i) {
    for (int j = i + 1; j < count; ++j) {
      if (deviations[j] < deviations[i]) {
        float t = deviations[i];
        deviations[i] = deviations[j];
        deviations[j] = t;
      }
    }
  }
  
  float MAD = deviations[count / 2];
  if (MAD < 0.5f) MAD = 0.5f;  // Minimum threshold

  // Stricter filtering for shallow water stability (1.5x MAD)
  float filteredSum = 0.0f;
  int filteredCount = 0;
  
  for (int i = 0; i < count; ++i) {
    if (fabs(readings[i] - median) <= (1.5f * MAD)) {
      filteredSum += readings[i];
      filteredCount++;
    }
  }

  float avg = (filteredCount == 0) ? median : filteredSum / filteredCount;

  // Adaptive smoothing for running water
  static float smooth = avg;
  static bool initialized = false;
  
  if (!initialized) {
    smooth = avg;
    initialized = true;
  }
  
  // Heavier smoothing for shallow water stability
  const float alpha = 0.25f;  // Lower = more smoothing
  smooth = alpha * avg + (1.0f - alpha) * smooth;

  // Debug output
  Serial.print("Valid readings: ");
  Serial.print(count);
  Serial.print(" | Median: ");
  Serial.print(median);
  Serial.print(" | MAD: ");
  Serial.print(MAD);
  Serial.print(" | Filtered avg: ");
  Serial.print(avg);
  Serial.print(" | Smoothed: ");
  Serial.println(smooth);

  return smooth;
}

// ============================================
//     WATER LEVEL (CM) AND PERCENTAGE
// ============================================
float getWaterLevelCM() {
  float dist = getDistanceCM_refined();
  if (dist <= 0) return -1.0f;

  float depth = SENSOR_HEIGHT_CM - dist;

  // Clamp to valid range
  if (depth < 0) depth = 0;
  if (depth > CANAL_DEPTH_CM) depth = CANAL_DEPTH_CM;

  return depth;
}

float getWaterPercent() {
  float depth = getWaterLevelCM();
  if (depth < 0) return -1.0f;
  return (depth / CANAL_DEPTH_CM) * 100.0f;
}

// ============================================
//       WATER LEVEL LABEL FUNCTION
// ============================================
String getWaterLabel() {
  float percent = getWaterPercent();
  if (percent < 0) return "Invalid";

  if (percent <= 25) return "Safe";
  if (percent <= 50) return "Caution";
  if (percent <= 75) return "Warning";
  return "Critical";
}

// ============================================
//              MODEM FUNCTIONS
// ============================================
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

// ============================================
//              GPS FUNCTIONS 
// ============================================
void stopGPSStreaming() {
  if (gpsStreaming) {
    DEBUG_PRINTLN(" Stopping GPS streaming...");
    SerialAT.println("AT+CGNSSTST=0");
    delay(1000);
    
    while (SerialAT.available()) {
      SerialAT.read();
    }
    
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
    DEBUG_PRINTLN("⚠️ GPS not enabled, enabling now...");
    if (!enableGPS()) return false;
  }
  
  startGPSStreaming();
  
  unsigned long start = millis();
  bool newData = false;
  
  while (millis() - start < 8000) {
    while (SerialAT.available()) {
      char c = SerialAT.read();
      if (gps.encode(c)) {
        newData = true;
      }
    }
    
    if (newData && gps.location.isValid()) {
      break;
    }
    delay(10);
  }
  
  stopGPSStreaming();
  
  if (gps.location.isValid()) {
    gpsLat = gps.location.lat();
    gpsLon = gps.location.lng();
    gpsAlt = gps.altitude.meters();
    
    DEBUG_PRINT("📍 GPS Fix -> Lat: ");
    DEBUG_PRINT_F(gpsLat, 6);
    DEBUG_PRINT(", Lon: ");
    DEBUG_PRINT_F(gpsLon, 6);
    DEBUG_PRINT(", Alt: ");
    DEBUG_PRINT_F(gpsAlt, 2);
    DEBUG_PRINT(" m, Satellites: ");
    DEBUG_PRINTLN(gps.satellites.value());
    
    return true;
  } else {
    DEBUG_PRINTLN("No valid GPS fix yet");
    DEBUG_PRINT("Satellites: ");
    DEBUG_PRINTLN(gps.satellites.value());
    DEBUG_PRINT("Characters processed: ");
    DEBUG_PRINTLN(gps.charsProcessed());
    return false;
  }
}

void printGPSDiagnostics() {
  DEBUG_PRINTLN("\n--- GPS Diagnostics ---");
  DEBUG_PRINT("GPS Enabled: ");
  DEBUG_PRINTLN(gpsEnabled ? "Yes" : "No");
  DEBUG_PRINT("GPS Streaming: ");
  DEBUG_PRINTLN(gpsStreaming ? "Yes" : "No");
  DEBUG_PRINT("Characters processed: ");
  DEBUG_PRINTLN(gps.charsProcessed());
  DEBUG_PRINT("Sentences with fix: ");
  DEBUG_PRINTLN(gps.sentencesWithFix());
  DEBUG_PRINT("Failed checksum: ");
  DEBUG_PRINTLN(gps.failedChecksum());
  DEBUG_PRINT("Satellites: ");
  DEBUG_PRINTLN(gps.satellites.value());
  DEBUG_PRINT("HDOP: ");
  DEBUG_PRINTLN(gps.hdop.hdop());
  
  if (gps.satellites.value() == 0) {
    DEBUG_PRINTLN(" NO SATELLITES DETECTED!");
    DEBUG_PRINTLN("   Check: 1) GPS antenna connected?");
    DEBUG_PRINTLN("          2) Antenna has clear sky view?");
    DEBUG_PRINTLN("          3) Not indoors?");
  }
  
  DEBUG_PRINTLN("----------------------\n");
}

// ============================================
//              TIME FUNCTIONS
// ============================================
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

// ============================================
//          HELPER: Wait for Prompt
// ============================================
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

// ============================================
//              FIREBASE FUNCTION
// ============================================
bool sendToFirebase(String timestamp, float rainfall, float distance) {
  DEBUG_PRINTLN("[DEBUG] Starting Firebase send...");
  
  stopGPSStreaming();

  String label = getWaterLabel();

  String payload = "{\"Timestamp\":\"" + timestamp +
                   "\",\"Rainfall\":" + String(totalRainfall, 2) +
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

// ============================================
//             SETUP FUNCTION
// ============================================
void setup() {
  Serial.begin(115200);
  SerialBT.begin("LILYGO_Debug");
  delay(1000);

  bootTime = millis();

  pinMode(TIP_PIN, INPUT_PULLUP);
  delay(500);

  // --- rain gauge init ---
  noInterrupts();
  tipCount = 0;
  lastTipTime = 0;
  lastTipGap = 0;
  totalRainfall = 0.0f;
  rainRate = 0.0f;
  interrupts();
  gaugeInitialized = false;
  attachInterrupt(digitalPinToInterrupt(TIP_PIN), tipISR, FALLING);
  delay(1500);
  gaugeInitialized = true;
  DEBUG_PRINTLN("Rain gauge initialized and counters reset.");

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  DEBUG_PRINTLN("Ultrasonic sensor initialized.");

  if (!SPIFFS.begin(true)) {
    DEBUG_PRINTLN("SPIFFS mount failed!");
    while (true);
  }
  if (!SPIFFS.exists("/sensor_log.csv")) {
    File file = SPIFFS.open("/sensor_log.csv", FILE_WRITE);
    if (file) {
      file.println("Timestamp,Rainfall_mm,WaterLevel_cm");
      file.close();
    }
  }
  DEBUG_PRINTLN("SPIFFS ready.");

  powerOnModem();
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(1500);
  connectNetwork();
  delay(1000);

  String currentTime = getModemTime();
  DEBUG_PRINTLN("Modem clock synchronized: " + currentTime);

  DEBUG_PRINTLN("📡 Initializing GPS...");
  if (enableGPS()) {
    DEBUG_PRINTLN("GPS initialization successful");
    DEBUG_PRINTLN("GPS requires clear sky view and 30-60s for first fix");
  } else {
    DEBUG_PRINTLN("GPS initialization failed - will retry in loop");
  }
  delay(2000);

  DEBUG_PRINTLN("System is now active for 2 hours before hibernation.\n");
}

// ============================================
//                  MAIN LOOP
// ============================================
void loop() {
  unsigned long now = millis();
  static volatile unsigned long localTipCount;

  updateRainfall();

  // ----------------------------------------
  //        SENSOR READ INTERVAL BLOCK
  // ----------------------------------------
  if (now - lastSensorRead >= SENSOR_READ_INTERVAL) {
    String timestamp = getModemTime();
    DEBUG_PRINTLN("\n--- Reading Sensors at " + timestamp + " ---");

    // 1️⃣ RAW DISTANCE (sensor → water surface)
    float rawDist = getDistanceCM_refined();
    if (rawDist > 0) {
      Serial.print("Raw Ultrasonic Distance: ");
      Serial.print(rawDist, 1);
      Serial.println(" cm");
    } else {
      Serial.println("Raw Ultrasonic Distance: INVALID");
    }

    // 2️⃣ COMPUTED WATER DEPTH (in cm)
    float distance = getWaterLevelCM();
    DEBUG_PRINT("Water Depth: ");
    DEBUG_PRINT_F(distance, 1);
    DEBUG_PRINTLN(" cm");

    // 3️⃣ RAINFALL DATA
    DEBUG_PRINT("Total Rainfall: ");
    DEBUG_PRINT_F(totalRainfall, 2);
    DEBUG_PRINTLN(" mm");

    DEBUG_PRINT("Rainfall Rate: ");
    DEBUG_PRINT_F(rainRate, 2);
    DEBUG_PRINTLN(" mm/hr");

    // 4️⃣ WATER LEVEL PERCENTAGE + CATEGORY LABEL
    float percent = getWaterPercent();
    String label = getWaterLabel();

    Serial.print("Water Level: ");
    Serial.print(percent, 1);
    Serial.print("% → ");
    Serial.println(label);
    lastSensorRead = now;
  }

  // ----------------------------------------
  //                GPS UPDATE
  // ----------------------------------------
  if (now - lastGPSUpdate >= GPS_UPDATE_INTERVAL) {
    DEBUG_PRINTLN("\n📡 Attempting GPS update...");
    if (updateGPSLocation()) {
      DEBUG_PRINTLN("GPS location updated");
    } else {
      DEBUG_PRINTLN("Waiting for GPS fix...");
      printGPSDiagnostics();
    }
    lastGPSUpdate = now;
  }

  // ----------------------------------------
  //             RAIN GAUGE EVENT
  // ----------------------------------------
  noInterrupts();
  localTipCount = tipCount;
  interrupts();

  if (localTipCount > lastSentTipCount) {
    updateRainfall();

    String eventTime = getModemTime();
    float waterDepth = getWaterLevelCM();
    if (waterDepth < 0) waterDepth = 0.0;

    bool sent = sendToFirebase(eventTime, totalRainfall, waterDepth);

    if (sent) {
      DEBUG_PRINTLN("Data sent to Firebase (rain gauge tipped)");
      lastSentTipCount = localTipCount;
      lastFirebaseSend = now;  //  Reset periodic timer after rain event
    } else {
      DEBUG_PRINTLN("Failed to send data to Firebase");
    }
  }

  // ----------------------------------------
  //     NEW: PERIODIC FIREBASE UPDATE
  // ----------------------------------------
  if (now - lastFirebaseSend >= FIREBASE_SEND_INTERVAL) {
    DEBUG_PRINTLN("\n⏰ Periodic Firebase update triggered (5 min interval)...");
    
    String timestamp = getModemTime();
    float waterDepth = getWaterLevelCM();
    if (waterDepth < 0) waterDepth = 0.0;
    
    bool sent = sendToFirebase(timestamp, totalRainfall, waterDepth);
    
    if (sent) {
      DEBUG_PRINTLN("Periodic data sent to Firebase");
      lastFirebaseSend = now;
    } else {
      DEBUG_PRINTLN("Failed to send periodic data");
      // Retry after 1 minute instead of full interval
      lastFirebaseSend = now - FIREBASE_SEND_INTERVAL + 60000UL;
    }
  }

  // ----------------------------------------
  //       SERIAL COMMAND INTERFACE
  // ----------------------------------------
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    // Add parsing here if needed
  }

  // ----------------------------------------
  //         AUTO-HIBERNATION HANDLER
  // ----------------------------------------
  if (millis() - bootTime >= ACTIVE_DURATION_MS) {
    DEBUG_PRINTLN("\n❄️ Active window elapsed (2 hours). Preparing for hibernation...");
    detachInterrupt(digitalPinToInterrupt(TIP_PIN));
    enterHibernation();
  }

  delay(100);
}
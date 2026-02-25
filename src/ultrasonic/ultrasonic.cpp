#include "ultrasonic.h"

#define TRIG_PIN 18
#define ECHO_PIN 32
#define ULTRA_SAMPLES      21 
#define ULTRA_SAMPLE_DELAY 100 
#define SENSOR_HEIGHT_CM   225.0f  
#define CANAL_DEPTH_CM      55.0f

extern float ambientTempC; // Defined in main if needed, or define here
float ambientTempC = 25.0;

void initUltrasonic() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
}

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
  return pulseIn(ECHO_PIN, HIGH, 80000);
}

float durationToCM(long dur_us, float speed) {
  if (dur_us <= 0) return -1.0f;
  return (dur_us * speed) / 2.0f;
}

float getDistanceCM_refined() {
  float speed = speedCmPerUs(ambientTempC);
  float readings[ULTRA_SAMPLES];
  int count = 0;

  for (int i = 0; i < ULTRA_SAMPLES; ++i) {
    long dur = singlePulse();
    float cm = durationToCM(dur, speed);
    if (cm > 5.0f && cm <= 450.0f) {
      readings[count++] = cm;
    }
    delay(ULTRA_SAMPLE_DELAY);
  }

  if (count < 5) return -1.0f;

  for (int i = 0; i < count - 1; ++i) {
    for (int j = i + 1; j < count; ++j) {
      if (readings[j] < readings[i]) {
        float t = readings[i]; readings[i] = readings[j]; readings[j] = t;
      }
    }
  }

  float median = readings[count / 2];
  float deviations[ULTRA_SAMPLES];
  for (int i = 0; i < count; ++i) { deviations[i] = fabs(readings[i] - median); }
  
  for (int i = 0; i < count - 1; ++i) {
    for (int j = i + 1; j < count; ++j) {
      if (deviations[j] < deviations[i]) {
        float t = deviations[i]; deviations[i] = deviations[j]; deviations[j] = t;
      }
    }
  }
  
  float MAD = deviations[count / 2];
  if (MAD < 0.5f) MAD = 0.5f;

  float filteredSum = 0.0f;
  int filteredCount = 0;
  for (int i = 0; i < count; ++i) {
    if (fabs(readings[i] - median) <= (1.5f * MAD)) {
      filteredSum += readings[i];
      filteredCount++;
    }
  }

  float avg = (filteredCount == 0) ? median : filteredSum / filteredCount;
  static float smooth = avg;
  static bool initialized = false;
  
  if (!initialized) { smooth = avg; initialized = true; }
  const float alpha = 0.25f;
  smooth = alpha * avg + (1.0f - alpha) * smooth;

  return smooth;
}

float getWaterLevelCM() {
  float dist = getDistanceCM_refined();
  if (dist <= 0) return -1.0f;
  float depth = SENSOR_HEIGHT_CM - dist;
  if (depth < 0) depth = 0;
  if (depth > CANAL_DEPTH_CM) depth = CANAL_DEPTH_CM;
  return depth;
}

float getWaterPercent() {
  float depth = getWaterLevelCM();
  if (depth < 0) return -1.0f;
  return (depth / CANAL_DEPTH_CM) * 100.0f;
}

String getWaterLabel() {
  float percent = getWaterPercent();
  if (percent < 0) return "Invalid";
  if (percent <= 25) return "Safe";
  if (percent <= 50) return "Caution";
  if (percent <= 75) return "Warning";
  return "Critical";
}
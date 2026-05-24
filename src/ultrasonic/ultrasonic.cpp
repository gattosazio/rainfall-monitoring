#include "ultrasonic.h"
#include "../domain/config.h"

float ambientTempC = 25.0f;

void initUltrasonic() {
  pinMode(Config::ULTRASONIC_TRIG_PIN, OUTPUT);
  pinMode(Config::ULTRASONIC_ECHO_PIN, INPUT);
}

float speedCmPerUs(float tempC = 25.0f) {
  float speed_m_s = 331.4f + 0.606f * tempC;
  return speed_m_s * 0.0001f;
}

long singlePulse() {
  digitalWrite(Config::ULTRASONIC_TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(Config::ULTRASONIC_TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(Config::ULTRASONIC_TRIG_PIN, LOW);
  return pulseIn(Config::ULTRASONIC_ECHO_PIN, HIGH, 80000);
}

float durationToCM(long dur_us, float speed) {
  if (dur_us <= 0) return -1.0f;
  return (dur_us * speed) / 2.0f;
}

UltrasonicReading readUltrasonic() {
  float speed = speedCmPerUs(ambientTempC);
  float readings[Config::ULTRASONIC_SAMPLES];
  int count = 0;
  long rawPulseUs = -1;
  float rawDistanceCm = -1.0f;

  for (int i = 0; i < Config::ULTRASONIC_SAMPLES; ++i) {
    long dur = singlePulse();
    float cm = durationToCM(dur, speed);
    if (rawPulseUs < 0 && dur > 0) {
      rawPulseUs = dur;
      rawDistanceCm = cm;
    }
    if (cm > Config::ULTRASONIC_MIN_VALID_CM &&
        cm <= Config::ULTRASONIC_MAX_VALID_CM) {
      readings[count++] = cm;
    }
    delay(Config::ULTRASONIC_SAMPLE_DELAY_MS);
  }

  UltrasonicReading reading;
  reading.rawPulseUs = rawPulseUs;
  reading.rawDistanceCm = rawDistanceCm;
  reading.medianRawDistanceCm = -1.0f;
  reading.filteredDistanceCm = -1.0f;
  reading.waterLevelCm = -1.0f;
  reading.validSampleCount = count;
  reading.isValid = false;

  if (count < 3) return reading;

  for (int i = 0; i < count - 1; ++i) {
    for (int j = i + 1; j < count; ++j) {
      if (readings[j] < readings[i]) {
        float t = readings[i];
        readings[i] = readings[j];
        readings[j] = t;
      }
    }
  }

  float median = readings[count / 2];
  reading.medianRawDistanceCm = median;
  float deviations[Config::ULTRASONIC_SAMPLES];
  for (int i = 0; i < count; ++i) {
    deviations[i] = fabs(readings[i] - median);
  }

  for (int i = 0; i < count - 1; ++i) {
    for (int j = i + 1; j < count; ++j) {
      if (deviations[j] < deviations[i]) {
        float t = deviations[i];
        deviations[i] = deviations[j];
        deviations[j] = t;
      }
    }
  }

  float mad = deviations[count / 2];
  if (mad < 0.5f) mad = 0.5f;

  float filteredSum = 0.0f;
  int filteredCount = 0;
  for (int i = 0; i < count; ++i) {
    if (fabs(readings[i] - median) <= (1.5f * mad)) {
      filteredSum += readings[i];
      filteredCount++;
    }
  }

  float avg = (filteredCount == 0) ? median : filteredSum / filteredCount;
  static float smooth = avg;
  static bool initialized = false;
  if (!initialized) {
    smooth = avg;
    initialized = true;
  }

  smooth = Config::ULTRASONIC_SMOOTHING_ALPHA * avg +
           (1.0f - Config::ULTRASONIC_SMOOTHING_ALPHA) * smooth;

  float depth = Config::ULTRASONIC_EMPTY_DISTANCE_CM - smooth;
  // Apply tolerance around "empty" so an empty canal reads as 0 cm.
  if (fabs(depth) <= Config::ULTRASONIC_ZERO_TOLERANCE_CM) depth = 0;
  if (depth < 0) depth = 0;

  reading.filteredDistanceCm = smooth;
  reading.waterLevelCm = depth;
  reading.isValid = true;
  return reading;
}

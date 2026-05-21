#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <Arduino.h>

struct RainGaugeReading {
  unsigned long totalTips;
  unsigned long ignoredTips;
  unsigned long tipsInWindow;
  unsigned long lastTipGapMs;
  float totalRainfallMm;
  float rainRateMmPerHour;
  float mmPerTip;
  bool isRaining;
};

struct UltrasonicReading {
  long rawPulseUs;
  float rawDistanceCm;
  float filteredDistanceCm;
  float waterLevelCm;
  unsigned int validSampleCount;
  bool isValid;
};

struct SensorSnapshot {
  String timestamp;
  String sendReason;
  RainGaugeReading rainGauge;
  UltrasonicReading ultrasonic;
  float gpsLat;
  float gpsLon;
  float gpsAlt;
};

#endif

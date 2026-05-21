#include "raingauge.h"
#include "../helpers/helpers.h"

volatile unsigned long tipTimes[Config::RAIN_MAX_TIPS];
volatile int headIndex = 0;

volatile unsigned long lastTipTime = 0;
volatile unsigned long lastTipGap = 0;
volatile int tipCount = 0;
volatile int ignoredTips = 0;
volatile bool gaugeInitialized = false;

float totalRainfall = 0.0f;
float rainRate = 0.0f;

void IRAM_ATTR tipISR() {
  unsigned long now = millis();
  if (now - lastTipTime > Config::RAIN_DEBOUNCE_MS) {
    lastTipGap = (lastTipTime == 0) ? 0 : (now - lastTipTime);
    if (!gaugeInitialized || ignoredTips < Config::RAIN_STARTUP_IGNORED_TIPS) {
      ignoredTips++;
    } else {
      tipCount++;
      tipTimes[headIndex] = now;
      headIndex = (headIndex + 1) % Config::RAIN_MAX_TIPS;
    }
    lastTipTime = now;
  }
}

void initRainGauge() {
  pinMode(Config::TIP_PIN, INPUT_PULLUP);
  delay(500);
  noInterrupts();
  tipCount = 0;
  lastTipTime = 0;
  lastTipGap = 0;
  ignoredTips = 0;
  totalRainfall = 0.0f;
  rainRate = 0.0f;
  interrupts();
  gaugeInitialized = false;
  attachInterrupt(digitalPinToInterrupt(Config::TIP_PIN), tipISR, FALLING);
  delay(1500);
  gaugeInitialized = true;
  DEBUG_PRINTLN("Rain gauge initialized and counters reset.");
}

void updateRainfall() {
  static int lastTips = 0;

  noInterrupts();
  int currentTipCount = tipCount;
  interrupts();

  int newTips = currentTipCount - lastTips;
  if (newTips > 0) {
    totalRainfall += newTips * Config::RAIN_MM_PER_TIP;
    lastTips = currentTipCount;
  }

  unsigned long now = millis();
  int tipsInWindow = 0;
  for (int i = 0; i < Config::RAIN_MAX_TIPS; i++) {
    if (tipTimes[i] > 0 && (now - tipTimes[i]) <= Config::RAIN_WINDOW_MS) {
      tipsInWindow++;
    }
  }

  if (tipsInWindow == 0) {
    rainRate = 0.0f;
  } else {
    float hours = (float)Config::RAIN_WINDOW_MS / 3600000.0f;
    rainRate = (tipsInWindow * Config::RAIN_MM_PER_TIP) / hours;
  }
}

RainGaugeReading getRainGaugeReading() {
  updateRainfall();

  noInterrupts();
  unsigned long totalTips = tipCount;
  unsigned long startupIgnoredTips = ignoredTips;
  unsigned long tipGap = lastTipGap;
  interrupts();

  unsigned long now = millis();
  unsigned long tipsInWindow = 0;
  for (int i = 0; i < Config::RAIN_MAX_TIPS; i++) {
    if (tipTimes[i] > 0 && (now - tipTimes[i]) <= Config::RAIN_WINDOW_MS) {
      tipsInWindow++;
    }
  }

  RainGaugeReading reading;
  reading.totalTips = totalTips;
  reading.ignoredTips = startupIgnoredTips;
  reading.tipsInWindow = tipsInWindow;
  reading.lastTipGapMs = tipGap;
  reading.totalRainfallMm = totalRainfall;
  reading.rainRateMmPerHour = rainRate;
  reading.mmPerTip = Config::RAIN_MM_PER_TIP;
  reading.isRaining = tipsInWindow > 0;
  return reading;
}

unsigned long getRainGaugeTipCount() {
  noInterrupts();
  unsigned long currentTipCount = tipCount;
  interrupts();
  return currentTipCount;
}

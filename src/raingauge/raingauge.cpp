#include "raingauge.h"
#include "../helpers/helpers.h"

#define MM_PER_TIP 0.70f
#define DEBOUNCE_MS 500
#define RAIN_WINDOW_MS 600000UL // 10-minute rolling window

#define MAX_TIPS 300
volatile unsigned long tipTimes[MAX_TIPS];
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

void initRainGauge() {
  pinMode(TIP_PIN, INPUT_PULLUP);
  delay(500);
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
}

void updateRainfall() {
  static int lastTips = 0;
  int newTips = tipCount - lastTips;
  if (newTips > 0) {
    totalRainfall += newTips * MM_PER_TIP;
    lastTips = tipCount;
  }

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
    rainRate = (tipsInWindow * MM_PER_TIP) / hours; 
  }
}
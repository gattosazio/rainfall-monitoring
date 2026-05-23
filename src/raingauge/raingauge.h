#ifndef RAINGAUGE_H
#define RAINGAUGE_H

#include <Arduino.h>
#include "../domain/config.h"
#include "../domain/telemetry.h"

extern volatile int tipCount;
extern float totalRainfall;
extern float rainRate;

void initRainGauge();
void updateRainfall();
RainGaugeReading getRainGaugeReading();
unsigned long getRainGaugeTipCount();

#endif

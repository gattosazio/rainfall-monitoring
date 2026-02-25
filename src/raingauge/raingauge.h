#ifndef RAINGAUGE_H
#define RAINGAUGE_H

#include <Arduino.h>

#define TIP_PIN 33 

extern volatile int tipCount;
extern float totalRainfall;
extern float rainRate;

void initRainGauge();
void updateRainfall();

#endif
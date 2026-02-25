#ifndef ULTRASONIC_H
#define ULTRASONIC_H

#include <Arduino.h>

void initUltrasonic();
float getDistanceCM_refined();
float getWaterLevelCM();
float getWaterPercent();
String getWaterLabel();

#endif
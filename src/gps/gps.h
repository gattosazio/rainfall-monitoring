#ifndef GPS_H
#define GPS_H

#include <Arduino.h>
#include "TinyGPSPlus.h"

extern TinyGPSPlus gps;
extern float gpsLat;
extern float gpsLon;
extern float gpsAlt;
extern bool gpsEnabled;
extern bool gpsStreaming;

bool enableGPS();
void printGPSDiagnostics();
bool updateGPSLocation();
void stopGPSStreaming();
void startGPSStreaming();

#endif
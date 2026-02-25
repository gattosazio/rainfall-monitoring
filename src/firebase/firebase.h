#ifndef FIREBASE_H
#define FIREBASE_H

#include <Arduino.h>

bool sendToFirebase(String timestamp, float rainfall, float distance);

#endif
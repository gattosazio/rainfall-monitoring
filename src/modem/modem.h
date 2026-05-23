#ifndef MODEM_H
#define MODEM_H

#include <Arduino.h>

#define SerialAT Serial1
#define MODEM_TX 26
#define MODEM_RX 27
#define MODEM_PWRKEY 4
#define MODEM_POWER_ON 4  
#define MODEM_DTR 25

void powerOnModem();
void sendATCommand(String cmd, unsigned long timeout);
String sendATCommandWithResponse(String cmd, unsigned long timeout);
bool connectNetwork();
bool waitForPrompt(const char* prompt, unsigned long timeout);
String getModemTime();

#endif
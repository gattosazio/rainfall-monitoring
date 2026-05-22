#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <Arduino.h>

struct PeriodicScheduleDecision {
  bool shouldSend;
  unsigned long intervalMs;
  const char* sendReason;
  const char* debugMessage;
};

PeriodicScheduleDecision getPeriodicScheduleDecision(bool rainingNow, bool dryWindowActive);

#endif

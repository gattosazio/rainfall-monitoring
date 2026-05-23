#include "scheduler.h"
#include "config.h"

PeriodicScheduleDecision getPeriodicScheduleDecision(bool rainingNow, bool dryWindowActive) {
  if (rainingNow) {
    return {true,
            Config::FIREBASE_WET_INTERVAL_MS,
            "periodic_wet",
            "\n Periodic Firebase update triggered (wet interval)..."};
  }

  if (dryWindowActive) {
    return {true,
            Config::FIREBASE_DRY_INTERVAL_MS,
            "periodic_dry_window",
            "\n Periodic Firebase update triggered (dry window interval)..."};
  }

  return {true,
          Config::FIREBASE_HEARTBEAT_INTERVAL_MS,
          "heartbeat_dry",
          "\n Periodic Firebase update triggered (dry heartbeat interval)..."};
}

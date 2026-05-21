# Rain Monitoring Refactor Journal

## Scope

This refactor reviewed the existing ESP32 + modem + GPS flood/rainfall monitor and changed the firmware to focus on:

- cleaner sensor acquisition flow
- raw + processed sensor telemetry
- removal of water status labels
- hourly dry-weather uploads
- GPS can be disabled for static-node deployments
- documented calibration constants

## Critique Of Old System

### Architecture

- `main.cpp` mixed scheduling, sensor reads, debug prints, and Firebase upload policy in one loop.
- `firebase.cpp` reached into global sensor and GPS state instead of accepting a full data snapshot.
- ultrasonic reads were repeated in different places, so one loop cycle could upload a different value from the value just printed.
- calibration constants were buried inside sensor `.cpp` files, not in one config location.

### Code Quality

- old Firebase payload was hand-built with string concatenation.
- old code sent derived water labels like `Safe`, `Caution`, `Warning`, `Critical`, which mixes telemetry with presentation.
- several values were magic numbers: `0.70`, `225`, `55`, `300000`.
- old loop used blocking sensor work and many modem delays. It still works, but timing is harder to reason about.

### Sensor Reliability

- rain gauge used `0.70 mm/tip` with no calibration note. That is risky if the bucket is a standard tipping bucket.
- ultrasonic sampling took 21 samples with 100 ms delay each, so one read blocked for about 2.1 s before extra processing.
- invalid ultrasonic readings were not clearly separated from valid processed data in uploads.
- raw sensor values were not stored in Firebase, so bad calibration or noise could not be diagnosed later.

### Data Handling

- Firebase only received top-level processed values.
- raw rainfall behavior was hidden behind accumulated rainfall only.
- raw ultrasonic pulse/distance were not preserved.
- old payload overwrote one node without documenting the schema.

### Firebase / Connectivity

- upload logic had no retry loop beyond a periodic retry trick in `main.cpp`.
- upload function only checked for one success string and had no structured payload generation.
- GPS streaming stop, HTTP init, HTTP send, and retry behavior were not isolated cleanly.

### Maintainability / Scale

- adding a new sensor would require touching many unrelated files.
- frontend/reporting consumers would be coupled to vague fields like `LevelLabel`.
- no README journal existed for calibration, setup, data contract, or maintenance.

## Refactor Plan

1. Centralize timing and calibration constants in `src/domain/config.h`.
2. Add telemetry structs in `src/domain/telemetry.h`.
3. Refactor rain gauge to expose raw tips, window tips, ignored startup tips, and processed totals.
4. Refactor ultrasonic module to return one structured reading with raw pulse, raw distance, filtered distance, and water level.
5. Change Firebase upload to accept one snapshot object and serialize nested JSON with ArduinoJson.
6. Remove all water level status labels and keep only raw + processed telemetry.
7. Keep periodic uploads while dry, but reduce them to hourly and keep rainy-period uploads at 3 minutes.
8. Document setup, calibration, Firebase schema, and maintenance in this README.

## What Changed

### Firmware

- added `src/domain/config.h`
- added `src/domain/telemetry.h`
- refactored `src/raingauge/*`
- refactored `src/ultrasonic/*`
- refactored `src/firebase/*`
- refactored `src/main.cpp`
- updated `src/hibernation/hibernation.cpp` to use centralized pin config

### Behavior

- rain events still upload immediately
- wet periods still upload every 3 minutes
- dry periods now upload every 1 hour
- every upload now contains raw and processed rain gauge data
- every upload now contains raw and processed ultrasonic data
- water warning labels are removed from code and payload
- GPS is currently disabled in firmware for static-node deployment, so uploaded coordinates remain `0`

## New System Flow

1. boot sensors, modem, GPS, SPIFFS
2. loop updates rainfall counters
3. periodic local sensor read prints debug values
4. rain tip event sends immediate snapshot
5. periodic uploader sends:
   - every 3 min when rain is active in the rolling window
   - every 1 hour when dry
6. hibernation still starts after 2 hours active time

## Calibration

### Rain Gauge

Current constant:

- `Config::RAIN_MM_PER_TIP = 0.70f`

Why:

- this value comes from the product description for the specific 3D-printed tipping bucket used here
- the product calibration method states:
  - collector area = `5.4 cm x 3.6 cm = 19.44 cm2`
  - poured volume = `100 mL`
  - observed tips = `70`
  - volume per tip = `100 / 70 = 1.42 mL`
  - rainfall height per tip = `1.42 / 19.44 = 0.073 cm = 0.73 mm`
- the seller rounds this to `0.70 mm/tip`, and the firmware now follows that product value

Verification / recalibration process:

1. level the rain gauge
2. slowly pour a measured water volume through the funnel
3. count bucket tips
4. compute:

```text
mm_per_tip = applied_rain_mm / tip_count
applied_rain_mm = poured_volume_ml / funnel_area_cm2 * 10
```

5. update `Config::RAIN_MM_PER_TIP` only if your measured result differs enough to justify replacing the vendor value
6. repeat at least 3 times and average

Notes:

- keep `Config::RAIN_DEBOUNCE_MS = 500` unless you confirm missed tips
- first 2 startup tips are ignored with `Config::RAIN_STARTUP_IGNORED_TIPS = 2` to avoid boot noise
- treat `0.70 mm/tip` as the documented vendor baseline for this exact product, not a universal tipping-bucket value

### Ultrasonic

New calibration constants:

- `Config::ULTRASONIC_EMPTY_DISTANCE_CM = 225.0f`
- `Config::ULTRASONIC_MAX_WATER_LEVEL_CM = 55.0f`
- `Config::ULTRASONIC_SAMPLES = 9`
- `Config::ULTRASONIC_SAMPLE_DELAY_MS = 60`

Why:

- old code mixed mounting geometry and water-depth clamp directly in the sensor file
- new code makes the empty reference and max water depth explicit
- sample count was reduced from 21 to 9 to cut blocking time while keeping median/MAD filtering

Field calibration process:

1. measure sensor-to-channel-bottom distance when channel is empty
2. set that value as `Config::ULTRASONIC_EMPTY_DISTANCE_CM`
3. measure physical maximum water depth of the site
4. set that value as `Config::ULTRASONIC_MAX_WATER_LEVEL_CM`
5. compare tape-measure depth vs reported depth at several water heights
6. if needed, fine-tune the empty distance constant

Depth formula:

```text
water_level_cm = ultrasonic_empty_distance_cm - filtered_distance_cm
```

## Firebase Data Structure

Current endpoint:

- `Node1.json`

Current payload shape:

```json
{
  "timestamp": "2026-05-21 14:22:11",
  "send_reason": "rain_event",
  "gps": {
    "latitude": 14.123456,
    "longitude": 121.123456,
    "altitude_m": 12.3
  },
  "rain_gauge": {
    "raw": {
      "tip_count": 42,
      "ignored_tip_count": 2,
      "tips_in_window": 3,
      "last_tip_gap_ms": 18452,
      "is_raining": true
    },
    "processed": {
      "total_mm": 11.73,
      "rate_mm_per_hr": 5.03,
      "mm_per_tip": 0.70
    }
  },
  "ultrasonic": {
    "raw": {
      "pulse_us": 13240,
      "distance_cm": 227.12,
      "valid_samples": 8,
      "valid": true
    },
    "processed": {
      "filtered_distance_cm": 225.84,
      "water_level_cm": 0.0,
      "empty_distance_cm": 225.0,
      "max_water_level_cm": 55.0
    }
  }
}
```

Send reasons:

- `rain_event`
- `periodic_wet`
- `periodic_dry_hourly`

Raw vs processed split:

- raw values keep sensor-side truth for debugging
- processed values keep calibrated engineering values for charts/analytics

## Setup

### PlatformIO

Build:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
```

Upload:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -t upload
```

Monitor:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" device monitor -b 115200
```

### Hardware Pins

- rain gauge tip: `GPIO33`
- ultrasonic trig: `GPIO18`
- ultrasonic echo: `GPIO32`
- modem TX: `GPIO26`
- modem RX: `GPIO27`

### Network / Firebase

- APN is set in `src/modem/modem.cpp`
- Firebase REST target is set in `src/domain/config.h`

## Maintenance Guidelines

- if site geometry changes, update ultrasonic calibration constants first
- if bucket or funnel changes, re-run rain gauge bench calibration
- do not mix UI labels into firmware payloads
- keep raw sensor fields untouched so later recalibration stays possible
- if Firebase schema changes, update README and payload builder together
- after modem changes, re-test:
  - timestamp parsing
  - GPS stop/start
  - HTTP upload
  - retry behavior

## Verification

Build checked:

```text
platformio run -> SUCCESS
RAM  : 12.8%
Flash: 89.0%
```

Note:

- flash usage is high. New work should avoid adding large libraries unless needed.

## Future Improvements

- replace blocking `delay()`-heavy modem flow with a non-blocking state machine
- store failed uploads in SPIFFS queue and flush later when network returns
- add modem response parsing with explicit HTTP status extraction
- add sensor fault flags for stuck rain gauge and repeated invalid ultrasonic reads
- move APN, Firebase URL, node id, and intervals into a config file or secure provisioning flow
- add battery voltage, RSSI, and reset-reason telemetry
- add rolling history paths in Firebase instead of only overwriting one node
- remove unused Firebase client library if modem HTTP mode remains the final design
- clean GPS module strings and duplicate includes
- add unit tests for rainfall accumulation and water-level conversion

## File Map

- `src/main.cpp`: scheduler and send policy
- `src/domain/config.h`: timing, calibration, and pin constants
- `src/domain/telemetry.h`: raw/processed snapshot structs
- `src/raingauge/`: rain gauge ISR and rainfall calculations
- `src/ultrasonic/`: ultrasonic acquisition and water depth conversion
- `src/firebase/`: JSON payload and HTTP upload

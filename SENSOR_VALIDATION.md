# Sensor Validation Guide

## Purpose

This file defines a simple validation method for each sensor used in this project:

- rain gauge
- ultrasonic sensor

Goal:

- check if sensor is working
- check if calibration is still acceptable
- detect drift, noise, or wiring problems before field deployment

## 1. Rain Gauge Validation

### What To Validate

- tip interrupt is detected correctly
- debounce is not blocking real tips
- `mm_per_tip` is still acceptable
- accumulated rainfall matches a controlled water input

### Current Firmware Value

- `Config::RAIN_MM_PER_TIP = 0.70f`
- `Config::RAIN_DEBOUNCE_MS = 500`

### Tools Needed

- measuring cup or syringe
- level surface
- stopwatch or phone timer
- small container for slow pouring

### Pre-Check

1. mount the rain gauge level
2. check bucket moves freely
3. check no debris blocks the mechanism
4. confirm wiring is secure

### Functional Test

1. power the board
2. open serial monitor
3. manually tip the bucket once
4. confirm tip count increases by 1
5. repeat slowly 5 to 10 times

Expected result:

- every real tip should register once
- no double counts from one tip
- no missed tips during slow manual validation

### Calibration Validation

1. measure collector area
   - current product basis: `5.4 cm x 3.6 cm = 19.44 cm2`
2. pour a known volume slowly into the collector
   - example: `100 mL`
3. count total recorded tips
4. compute:

```text
volume_per_tip_ml = poured_volume_ml / tip_count
rainfall_per_tip_cm = volume_per_tip_ml / collector_area_cm2
rainfall_per_tip_mm = rainfall_per_tip_cm x 10
```

### Acceptance Check

Compare measured `rainfall_per_tip_mm` against firmware value `0.70 mm/tip`.

Suggested acceptance band:

- acceptable: within about `+/-10%`
- recalibrate firmware: if repeated tests stay outside that band

Example:

- if measured value is around `0.63 mm` to `0.77 mm`, keep current value
- if measured value is consistently outside that range, update `Config::RAIN_MM_PER_TIP`

### Repeatability Test

1. repeat the pour test at least 3 times
2. compute average `mm/tip`
3. compare spread between trials

Good sign:

- low variation across trials

Bad sign:

- large variation means unstable mechanics, tilt issue, or uneven pouring

### Failure Signs

- double count on one tip
- no count on real tip
- count changes when sensor is touched or cable moves
- bucket sticks
- very different results across repeated trials

## 2. Ultrasonic Sensor Validation

### What To Validate

- sensor returns stable raw distance
- filtered distance is consistent
- water level conversion matches physical measurement
- sensor still works across dry and wet conditions

### Current Firmware Values

- `Config::ULTRASONIC_EMPTY_DISTANCE_CM = 225.0f`
- `Config::ULTRASONIC_MAX_WATER_LEVEL_CM = 55.0f`
- `Config::ULTRASONIC_SAMPLES = 9`
- `Config::ULTRASONIC_SAMPLE_DELAY_MS = 60`

### Tools Needed

- tape measure or ruler
- flat target surface
- notebook or spreadsheet for readings

### Pre-Check

1. verify sensor is rigidly mounted
2. confirm sensor faces straight downward
3. check no object blocks the beam
4. check wiring is secure

### Dry Reference Validation

1. empty the channel or test area
2. measure actual sensor-to-bottom distance with tape measure
3. compare it against firmware empty reference
4. open serial monitor and read raw ultrasonic distance

Expected result:

- raw distance should stay near actual measured distance
- filtered distance should be smoother than raw distance

### Static Distance Test

1. place a flat target at known distances
   - example: `50 cm`, `100 cm`, `150 cm`
2. record multiple raw readings at each position
3. record filtered value
4. compute error:

```text
error_cm = measured_by_sensor_cm - actual_distance_cm
```

### Acceptance Check

Suggested target:

- good: within `1 to 3 cm` depending on environment
- investigate: if error grows with distance or jumps randomly

### Water Level Validation

1. measure actual water depth manually
2. get firmware filtered distance
3. compute expected level:

```text
water_level_cm = empty_distance_cm - filtered_distance_cm
```

4. compare firmware water level against manual measurement

Suggested acceptance:

- keep current calibration if repeated checks are close
- adjust `Config::ULTRASONIC_EMPTY_DISTANCE_CM` if there is a steady offset

### Stability Test

1. keep target fixed
2. collect readings for several minutes
3. inspect variation in raw and filtered data

Good sign:

- raw may move slightly
- filtered value stays stable

Bad sign:

- large jumps
- many invalid readings
- unstable values with no physical movement

### Failure Signs

- frequent invalid raw distance
- very noisy readings
- stable offset from actual measured distance
- false changes caused by vibration, splash, or beam obstruction

## 3. Recommended Validation Schedule

### Before Deployment

- validate rain gauge tip detection
- validate rain gauge `mm/tip`
- validate ultrasonic empty distance
- validate ultrasonic depth conversion

### Routine Maintenance

- rain gauge: every cleaning or hardware adjustment
- ultrasonic: after remounting, flood damage, or mechanical movement
- both sensors: after firmware changes affecting timing or conversion

## 4. Recording Template

Use a simple table like this.

### Rain Gauge

| Trial | Poured Volume (mL) | Tip Count | Computed mm/tip | Pass/Fail | Notes |
|---|---:|---:|---:|---|---|
| 1 | 100 | 70 | 0.73 | Pass | close to vendor value |

### Ultrasonic

| Trial | Actual Distance (cm) | Raw Distance (cm) | Filtered Distance (cm) | Error (cm) | Pass/Fail | Notes |
|---|---:|---:|---:|---:|---|---|
| 1 | 100 | 101.4 | 100.8 | 0.8 | Pass | stable |

## 5. Pass Rule For This Project

Treat sensor as validated when:

- rain gauge counts tips reliably
- computed rain gauge `mm/tip` stays close to `0.70`
- ultrasonic filtered distance stays close to tape-measure value
- water level calculation matches manual depth within acceptable field error
- repeated trials are stable

## 6. If Validation Fails

Check in this order:

1. wiring
2. mechanical alignment
3. debris / obstruction
4. mount stability
5. calibration constants
6. sensor replacement if instability remains

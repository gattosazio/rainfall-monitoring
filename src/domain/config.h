#ifndef CONFIG_H
#define CONFIG_H

namespace Config {
// ---- Firebase (Realtime Database) ----
// CI/CD can override these at build time:
// -DFIREBASE_URL_STR="https://.../Node2.json"
// -DFIREBASE_COMMAND_OTA_URL_STR="https://.../commands/Node2/ota_check.json"

#ifdef FIREBASE_URL_STR
constexpr char FIREBASE_URL[] = FIREBASE_URL_STR;
#else
constexpr char FIREBASE_URL[] =
    "https://lte-test2-default-rtdb.asia-southeast1.firebasedatabase.app/Node2.json";
#endif

#ifdef FIREBASE_COMMAND_OTA_URL_STR
constexpr char FIREBASE_COMMAND_OTA_URL[] = FIREBASE_COMMAND_OTA_URL_STR;
#else
constexpr char FIREBASE_COMMAND_OTA_URL[] =
    "https://lte-test2-default-rtdb.asia-southeast1.firebasedatabase.app/commands/Node2/ota_check.json";
#endif

// ---- OTA (Cellular) ----
// When enabled, the device will fetch a small JSON manifest over the modem and
// (if newer) download a firmware .bin and flash it using ESP32's OTA slots.
//
// Manifest format (example):
// {
//   "version": "2026.05.23-1",
//   "url": "https://example.com/firmware.bin",
//   "size": 1160000,
//   "sha256": "hexstring..."
// }
// CI/CD can override these at build time.
// -DOTA_ENABLED_VALUE=1
// -DOTA_MANIFEST_URL_STR="https://.../ota/manifest.json"
#ifdef OTA_ENABLED_VALUE
constexpr bool OTA_ENABLED = (OTA_ENABLED_VALUE != 0);
#else
constexpr bool OTA_ENABLED = true;
#endif

#ifdef OTA_MANIFEST_URL_STR
constexpr char OTA_MANIFEST_URL[] = OTA_MANIFEST_URL_STR;
#else
constexpr char OTA_MANIFEST_URL[] = "https://gattosazio.github.io/rainfall-monitoring/ota/manifest.json";
#endif

// CI/CD can override this at build time via -DFIRMWARE_VERSION_STR="..."
#ifdef FIRMWARE_VERSION_STR
constexpr char FIRMWARE_VERSION[] = FIRMWARE_VERSION_STR;
#else
constexpr char FIRMWARE_VERSION[] = "0.0.3";
#endif

constexpr unsigned long OTA_CHECK_DELAY_MS = 5000UL;
constexpr unsigned long OTA_HTTPREAD_CHUNK_BYTES = 1024UL;

// How often the device checks Firebase for OTA commands (remote "button").
constexpr unsigned long OTA_COMMAND_POLL_INTERVAL_MS = 300000UL;  // 5 minutes

constexpr unsigned long SENSOR_READ_INTERVAL_MS = 3000UL;
constexpr unsigned long FIREBASE_WET_INTERVAL_MS = 300000UL;
constexpr unsigned long FIREBASE_DRY_INTERVAL_MS = 300000UL;
constexpr unsigned long FIREBASE_DRY_UPLOAD_WINDOW_MS = 7200000UL;
constexpr unsigned long FIREBASE_HEARTBEAT_INTERVAL_MS = 10800000UL;
constexpr unsigned long FIREBASE_RAIN_EVENT_RETRY_MS = 60000UL;
constexpr unsigned long FIREBASE_RETRY_DELAY_MS = 60000UL;
constexpr unsigned long ACTIVE_DURATION_MS = 7200000UL;
constexpr bool HIBERNATION_ENABLED = false;

constexpr unsigned int FIREBASE_HTTP_RETRIES = 3;

constexpr float RAIN_MM_PER_TIP = 0.70f;
constexpr unsigned long RAIN_DEBOUNCE_MS = 500UL;
constexpr unsigned long RAIN_WINDOW_MS = 600000UL;
constexpr int RAIN_STARTUP_IGNORED_TIPS = 2;
constexpr int RAIN_MAX_TIPS = 300;

constexpr unsigned int TIP_PIN = 33;

constexpr unsigned int ULTRASONIC_TRIG_PIN = 18;
constexpr unsigned int ULTRASONIC_ECHO_PIN = 32;
constexpr int ULTRASONIC_SAMPLES = 9;
constexpr unsigned long ULTRASONIC_SAMPLE_DELAY_MS = 60UL;
constexpr float ULTRASONIC_EMPTY_DISTANCE_CM = 225.0f;
constexpr float ULTRASONIC_MAX_WATER_LEVEL_CM = 55.0f;
constexpr float ULTRASONIC_MIN_VALID_CM = 5.0f;
constexpr float ULTRASONIC_MAX_VALID_CM = 450.0f;
constexpr float ULTRASONIC_SMOOTHING_ALPHA = 0.25f;
}

#endif

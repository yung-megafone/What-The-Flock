#pragma once
// XIAO ESP32-C5 prototype defaults. Verify onboard LED GPIO/polarity on hardware.
#define WTF_LED_GPIO 27 // Onboard LED is GPIO27
#define WTF_ENABLE_BUZZER 0 // Not hooked up for testing
#define WTF_BUZZER_ON_DETECTION 1 // Beeo when a new device is detected; requires WTF_ENABLE_BUZZER=1
#define WTF_BUZZER_ON_FAULT 1 // Beep when a fault is detected; requires WTF_ENABLE_BUZZER=1 (i forgot what sound this is supposed to make)
#define WTF_ENABLE_DEBUG_CONSOLE 0 // DO NOT USE -- BROKEN, CAUSES CRASHES
#define WTF_ANIMATION_SPEED 1 // 1 fast (70 ms), 10 slow (200 ms)
#define WTF_SELFTEST_INTERVAL_MS 4000 // FIXED: not affected by speed
#define WTF_SCANNING_INTERVAL_MS 5000 // FIXED: not affected by speed
#define WTF_TRACK_TIMEOUT_MS 5000 // FIXED: not affected by speed
#define WTF_UNIQUE_COOLDOWN_MS 30000 // FIXED: not affected by speed

// Boot-only offline diagnostics: XIAO ESP32-C5 D10 is GPIO10.
// Connect D10 -> GND BEFORE reset to enable; leave open for normal operation.
// Never jumper D10 to 5V; the board uses 3.3V logic.
#define WTF_ENABLE_BOOT_DIAGNOSTICS 1
#define WTF_DIAGNOSTIC_GPIO 10

// Momentary button D5(GPIO24)->GND. Selects P1/P2/P3, persists in NVS.
#define WTF_PROFILE_BUTTON_GPIO 24

// proximity display (these are NOT calibrated distances).
// LED full-scale is independently configurable and does NOT modify raw RSSI,
// scan locking, match confidence, NVS data or stored detection records.
// Provisional on-road display calibration: -70 dBm shows maximum LED
// strength. Adjust after reviewing logged encounter peaks. This affects
// ONLY the LED mapping, never the received/stored RSSI or matching logic.
// Restore (-45) for the legacy display scale.
// Current value is a guess. -45 is the legacy threshold, but signal strength seems weaker in the field. (20261118)
#define WTF_LED_PEAK_RSSI_DBM (-70) // Needs to be calibrated based on real encounters. 


// Strongest normalized RSSI reaches solid LED; pulse speed is continuous.
// Retain a strong peak briefly, then visibly decelerate when readings weaken.
#define WTF_RSSI_PEAK_HOLD_MS 900
#define WTF_RSSI_FALL_DB_PER_S 18
#define WTF_RSSI_SILENCE_GRACE_MS 1000
#define WTF_RSSI_SILENCE_FADE_DB_PER_S 9
#define WTF_RSSI_TREND_STALE_MS 2000
#define WTF_RSSI_SOURCE_HOLD_MS 1250
#define WTF_RSSI_SOURCE_SWITCH_DB 5

// LAB BUILD: real OTA WTF-LAB-01 beacon reception for bench verification.
// Set both flags to 0 before a field-only firmware release.
#define WTF_ACCEPT_LAB_BEACONS 0 // (0=ignore, 1=accept) LAB beacons are not real devices; they are for bench testing only.
// Records lab source metadata and encounter RSSI via the SAME SPIFFS queue.
// Clear [LAB WIFI] labels prevent confusion with real device detections.
#define WTF_LOG_LAB_ENCOUNTERS 0 // (0=ignore, 1=log) LAB encounters are not real devices; they are for bench testing only.
#if WTF_LOG_LAB_ENCOUNTERS && !WTF_ACCEPT_LAB_BEACONS
#error "Lab encounter logging requires WTF_ACCEPT_LAB_BEACONS=1"
#endif

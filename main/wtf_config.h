#pragma once
// XIAO ESP32-C5 prototype defaults. Verify onboard LED GPIO/polarity on hardware.
// Onboard GPIO27: heartbeat, boot/profile confirmations, and fault status.
#define WTF_STATUS_LED_GPIO 27
#define WTF_STATUS_LED_ACTIVE_LEVEL 0
// External XIAO D3 / GPIO7: RF reception cadence and new detection alerts.
#define WTF_RECEPTION_LED_GPIO 7
#define WTF_RECEPTION_LED_ACTIVE_LEVEL 1
// Independently adjustable outputs. External LED: D3 -> resistor -> LED -> GND.
// Seeed XIAO C5 pins are 3.3 V logic; do not drive a bare LED without resistor.
// XIAO ESP32-C5 D2 = GPIO25. Passive buzzer via an external transistor driver.
// The LEDC peripheral generates the audible tone; the RSSI controls chirp cadence.
// Do not attach a low-impedance speaker directly to a GPIO.
#define WTF_ENABLE_BUZZER 1
#define WTF_BUZZER_GPIO 25
#define WTF_BUZZER_ACTIVE_LEVEL 1 // 1 = active-high transistor base drive
#define WTF_BUZZER_TONE_HZ 2000 // audible pitch, independent of chirp cadence
#define WTF_BUZZER_DUTY_PERCENT 50 // 1..99, PWM duty while sounding (whatever you do, do NOT google "Sounding")
#define WTF_BUZZER_PWM_TIMER LEDC_TIMER_0
#define WTF_BUZZER_PWM_CHANNEL LEDC_CHANNEL_0
#define WTF_BUZZER_CHIRP_MS 60
#define WTF_BUZZER_FASTEST_INTERVAL_MS 220
#define WTF_BUZZER_ON_DETECTION 1
#define WTF_BUZZER_ON_FAULT 1
#define WTF_ENABLE_DEBUG_CONSOLE 0
#define WTF_ANIMATION_SPEED 1 // 1 fast (70 ms), 10 slow (200 ms)
#define WTF_SELFTEST_INTERVAL_MS 4000 // FIXED: not affected by speed
#define WTF_SCANNING_INTERVAL_MS 5000 // FIXED: not affected by speed
#define WTF_TRACK_TIMEOUT_MS 5000
#define WTF_UNIQUE_COOLDOWN_MS 30000

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
// ONLY the indicator mapping, never the received/stored RSSI or matching logic.
// Restore (-45) for the legacy display scale.
#define WTF_LED_PEAK_RSSI_DBM (-70)
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
#define WTF_ACCEPT_LAB_BEACONS 1
// Records lab source metadata and encounter RSSI via the SAME SPIFFS queue.
// Clear [LAB WIFI] labels prevent confusion with real device detections.
#define WTF_LOG_LAB_ENCOUNTERS 1
#if WTF_LOG_LAB_ENCOUNTERS && !WTF_ACCEPT_LAB_BEACONS
#error "Lab encounter logging requires WTF_ACCEPT_LAB_BEACONS=1"
#endif

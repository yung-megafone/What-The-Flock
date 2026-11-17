#pragma once
// XIAO ESP32-C5 prototype defaults. Verify onboard LED GPIO/polarity on hardware.
#define WTF_LED_GPIO 27
#define WTF_ENABLE_BUZZER 0
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
// Strongest RSSI reaches solid LED at -45 dBm; pulse speed is continuous.
// Retain a strong peak briefly, then visibly decelerate when readings weaken.
#define WTF_RSSI_PEAK_HOLD_MS 900
#define WTF_RSSI_FALL_DB_PER_S 18
#define WTF_RSSI_SILENCE_GRACE_MS 1000
#define WTF_RSSI_SILENCE_FADE_DB_PER_S 9
#define WTF_RSSI_TREND_STALE_MS 2000
#define WTF_RSSI_SOURCE_HOLD_MS 1250
#define WTF_RSSI_SOURCE_SWITCH_DB 5

// Lab-only signature acceptance for controlled WTF-LAB-01 AP beacons.
// Keep OFF for field firmware to avoid false positives.
#define WTF_ACCEPT_LAB_BEACONS 0

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
#define WTF_ENABLE_BOOT_DIAGNOSTICS 1
#define WTF_DIAGNOSTIC_GPIO 10

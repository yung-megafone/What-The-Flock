#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "wtf_config.h"
#include "wtf_signal_math.h"
#include "wtf_scan_math.h"
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"

// BLE Headers
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"

// SPIFFS
#include "esp_spiffs.h"
#include "esp_timer.h"

// Hardware Definitions - Conditional on Target Chip
#if CONFIG_IDF_TARGET_ESP32C5
    #define USER_LED_GPIO WTF_LED_GPIO
    #define HAS_LED 1
    #define ENABLE_EXT_ANTENNA 0
#elif CONFIG_IDF_TARGET_ESP32C6
    #define USER_LED_GPIO 15
    #define USER_BUZZER_GPIO 0
    #define HAS_LED 1

    // External antenna setup for C6
    #define ENABLE_EXT_ANTENNA 1
    #define EXT_ANT_PIN1 3
    #define EXT_ANT_PIN2 14

#elif CONFIG_IDF_TARGET_ESP32S3
    #define USER_LED_GPIO 21
    #define USER_BUZZER_GPIO 1
    #define HAS_LED 1
    #define ENABLE_EXT_ANTENNA 0

#elif CONFIG_IDF_TARGET_ESP32C3
    #define USER_BUZZER_GPIO 2
    #define HAS_LED 0
    #define ENABLE_EXT_ANTENNA 0

#else
    #error "Unsupported target chip"
#endif

// Common definitions
#define LED_ON  0
#define LED_OFF 1
#define BUZZER_ON  1
#define BUZZER_OFF 0

static const char *TAG = "WHAT_THE_FLOCK";

static bool spiffs_ready = false;
static volatile bool wifi_ready = false, ble_ready = false;
static volatile bool wifi_enabled = true, ble_enabled = true;
static QueueHandle_t log_queue;
static portMUX_TYPE detection_lock = portMUX_INITIALIZER_UNLOCKED;
static int last_rssi = -127;     // filtered value for the selected transmitter
static int64_t last_seen_ms = 0;
static int tracked_kind = 0;   // 1=Wi-Fi, 2=BLE, 3=synthetic test
static uint8_t tracked_mac[6];
static int last_raw_rssi = -127;
static int peak_rssi = -127;
static int64_t peak_hold_until_ms = 0;
static int trend_score = 0;    // -2/-3=receding, +2/+3=approaching

// Centralized fault reporting. The first fatal error is retained for diagnosis.
typedef enum {
    WTF_F_NONE = 0,
    WTF_F_NVS = 1,
    WTF_F_SPIFFS = 2,
    WTF_F_WIFI = 3,
    WTF_F_BLE = 4,
    WTF_F_LOGGING = 5,
    WTF_F_SELFTEST = 6,
    WTF_F_TASK = 7,
    WTF_F_GPIO = 8,
    WTF_F_WIFI_HOP = 9,
    WTF_F_LOG_QUEUE_FULL = 10,
    WTF_F_TEST_SOS = 11
} wtf_fault_code_t;

typedef enum { WTF_DEGRADED = 0, WTF_FATAL = 1 } wtf_fault_severity_t;
static portMUX_TYPE fault_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool fault_latched = false;
static volatile wtf_fault_code_t fatal_fault_code = WTF_F_NONE;
static volatile wtf_fault_code_t last_fault_code = WTF_F_NONE;
static volatile int last_fault_detail = 0;
static volatile unsigned fault_events = 0;
static bool sos_preview = false;
static bool diagnostic_mode = false; // sampled once at boot; never polled afterward
static volatile uint32_t wifi_rx_packets = 0;
static volatile uint32_t ble_rx_adverts = 0;

// Scan profile and lock state. Callback writes are short and protected;
// the scheduler performs all Wi-Fi driver channel changes in its own task.
static portMUX_TYPE scan_lock_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t selected_scan_profile = WTF_SCAN_P2_BALANCED;
static uint8_t active_wifi_channel = 0;
static uint8_t match_channel = 0;
static uint8_t match_mac[6];
static unsigned match_count = 0;
static int64_t match_first_ms = 0, match_last_ms = 0;
static int64_t channel_lock_started_ms = 0, lock_cooldown_until_ms = 0;
static bool channel_locked = false;
static volatile uint32_t channel_lock_count = 0;
static volatile uint32_t channel_unlock_count = 0;
static volatile uint32_t matched_wifi_packets = 0;
static volatile uint8_t profile_ack_value = 0;
static volatile int64_t profile_ack_start_ms = 0;

static uint32_t diagnostic_wifi_rx = 0, diagnostic_ble_rx = 0;
static int64_t preview_until_ms = 0;
static int64_t alert_until_ms = 0;
static int64_t selftest_until_ms = 0;
static int64_t boot_until_ms = 0;
static uint8_t recent_macs[24][7]; // [radio kind, 6 MAC bytes]
static int64_t recent_times[24];
static unsigned recent_next = 0;
static unsigned detection_count = 0;
static int64_t now_ms(void) { return esp_timer_get_time()/1000; }

// Safe for use from FreeRTOS tasks and radio callbacks (not from ISRs).
// All genuine failures go through this entry point; no USB client is required.
static void wtf_report_fault(wtf_fault_code_t code, wtf_fault_severity_t severity, int detail) {
    taskENTER_CRITICAL(&fault_lock);
    ++fault_events;
    last_fault_code = code;
    last_fault_detail = detail;
    if (severity == WTF_FATAL) {
        if (!fault_latched) fatal_fault_code = code;
        fault_latched = true;
    }
    taskEXIT_CRITICAL(&fault_lock);
    ESP_LOGE(TAG, "FAULT F%02u %s detail=%d", (unsigned)code,
             severity == WTF_FATAL ? "FATAL" : "DEGRADED", detail);
}

// XIAO ESP32-C5 D10 = GPIO10, a non-strapping GPIO. Jumper D10 to GND
// before reset to enter diagnostics. The internal pull-up selects normal mode.
static bool wtf_diagnostic_jumper_present(void) {
#if CONFIG_IDF_TARGET_ESP32C5 && WTF_ENABLE_BOOT_DIAGNOSTICS
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << WTF_DIAGNOSTIC_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        wtf_report_fault(WTF_F_GPIO, WTF_FATAL, err);
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    int low_samples = 0;
    for (int n = 0; n < 5; ++n) {
        if (gpio_get_level((gpio_num_t)WTF_DIAGNOSTIC_GPIO) == 0) ++low_samples;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return low_samples >= 4;
#else
    return false;
#endif
}

// All radio callbacks and diagnostic inputs use this same signal path.
// Select one transmitter at a time so unrelated radios/MACs cannot fabricate
// an approach or recession. A new source takes over if the old one went quiet
// for >1.25s or the newcomer is distinctly stronger (+5 dBm).
// A single selected source is deliberately simple; this is not a full
// multi-device tracking database.
static void wtf_reset_rssi_tracking(void) {
    taskENTER_CRITICAL(&detection_lock);
    last_rssi = -127;
    last_seen_ms = 0;
    tracked_kind = 0;
    memset(tracked_mac, 0, sizeof(tracked_mac));
    last_raw_rssi = -127;
    peak_rssi = -127;
    peak_hold_until_ms = 0;
    trend_score = 0;
    taskEXIT_CRITICAL(&detection_lock);
}

static void wtf_update_rssi(int kind, const uint8_t mac[6], int rssi) {
    const int64_t t = now_ms();
    if (rssi < -127 || rssi > 20 || mac == NULL) return;
    taskENTER_CRITICAL(&detection_lock);
    const bool expired = !last_seen_ms || t - last_seen_ms > WTF_TRACK_TIMEOUT_MS;
    const bool same = tracked_kind == kind && memcmp(mac, tracked_mac, 6) == 0;
    if (!same && !expired && t-last_seen_ms < WTF_RSSI_SOURCE_HOLD_MS &&
        rssi < last_rssi + WTF_RSSI_SOURCE_SWITCH_DB) {
        taskEXIT_CRITICAL(&detection_lock);
        return; // weaker unrelated radio does not distort LED/trend
    }
    const bool fresh = expired || !same;
    if (fresh) {
        tracked_kind = kind;
        memcpy(tracked_mac, mac, 6);
        trend_score = 0;
        peak_rssi = rssi;
    } else {
        trend_score = wtf_trend_vote(trend_score, last_raw_rssi, rssi);
    }
    last_rssi = wtf_filter_rssi(last_rssi, rssi, fresh);
    last_raw_rssi = rssi;
    if (fresh || rssi >= peak_rssi) {
        peak_rssi = rssi;
        peak_hold_until_ms = t + WTF_RSSI_PEAK_HOLD_MS;
    }
    last_seen_ms = t;
    taskEXIT_CRITICAL(&detection_lock);
}

// One animation-speed control. Fixed heartbeat periods are NOT scaled.
#define ANIM_UNIT_MS (70 + ((WTF_ANIMATION_SPEED - 1) * 130 / 9))
#if WTF_ANIMATION_SPEED < 1 || WTF_ANIMATION_SPEED > 10
#error "WTF_ANIMATION_SPEED must be 1..10"
#endif

static bool unique_transmitter(int kind, const uint8_t mac[6]) {
    int64_t t = now_ms(); bool fresh = true;
    taskENTER_CRITICAL(&detection_lock);
    for (int i=0;i<24;i++) {
        if (recent_times[i] && recent_macs[i][0] == kind && memcmp(recent_macs[i]+1, mac, 6)==0) {
            fresh = (t-recent_times[i] > WTF_UNIQUE_COOLDOWN_MS);
            recent_times[i] = t;
            break;
        }
    }
    if (fresh) {
        unsigned idx = recent_next++ % 24;
        recent_macs[idx][0] = kind;
        memcpy(recent_macs[idx]+1,mac,6);
        recent_times[idx]=t;
        detection_count++;
        alert_until_ms = t + 5 * ANIM_UNIT_MS;
    }
    taskEXIT_CRITICAL(&detection_lock);
    return fresh;
}
static void observe_detection(int kind, const uint8_t mac[6], int rssi, const char *line) {
    if (diagnostic_mode) return; // test mode never creates real detection records
    wtf_update_rssi(kind, mac, rssi);
    if (unique_transmitter(kind,mac)) {
        ESP_LOGW("DETECTION", "%s",line);
        if (log_queue) {
            char record[180]={0}; snprintf(record,sizeof(record),"%s",line);
            if (xQueueSend(log_queue,record,0)!=pdTRUE) wtf_report_fault(WTF_F_LOG_QUEUE_FULL,WTF_DEGRADED,0);
        }
    }
}

// Snapshot a fresh matching Wi-Fi transmission. Never lock from raw traffic.
// A credible SSID match (confidence 2) immediately qualifies; an OUI-only
// match (confidence 1) requires two observations from the same MAC and channel.
static void wtf_scan_note_match(const uint8_t mac[6], uint8_t channel, int confidence) {
    if (diagnostic_mode || !mac || channel < 1 || channel > 13) return;
    const int64_t t = now_ms();
    __atomic_fetch_add(&matched_wifi_packets, 1, __ATOMIC_RELAXED);
    taskENTER_CRITICAL(&scan_lock_mux);
    if (!channel_locked && t >= lock_cooldown_until_ms) {
        bool same = match_channel == channel &&
                    memcmp(match_mac, mac, sizeof(match_mac)) == 0 &&
                    t >= match_first_ms &&
                    t - match_first_ms <= WTF_SCAN_CANDIDATE_WINDOW_MS;
        if (!same) {
            memcpy(match_mac, mac, sizeof(match_mac));
            match_channel = channel;
            match_first_ms = t;
            match_count = 0;
        }
        if (match_count < 255) ++match_count;
        if (wtf_scan_candidate_ready(confidence, match_count, t, match_first_ms)) {
            channel_locked = true;
            channel_lock_started_ms = t;
            match_last_ms = t;
            ++channel_lock_count;
        }
    } else if (channel_locked && channel == match_channel &&
               memcmp(match_mac, mac, sizeof(match_mac)) == 0) {
        match_last_ms = t;
    }
    taskEXIT_CRITICAL(&scan_lock_mux);
}

static void wtf_scan_reset_lock(void) {
    taskENTER_CRITICAL(&scan_lock_mux);
    channel_locked = false;
    match_channel = 0;
    match_count = 0;
    match_first_ms = match_last_ms = 0;
    lock_cooldown_until_ms = 0;
    taskEXIT_CRITICAL(&scan_lock_mux);
}

static bool wtf_profile_ack_on(int64_t t) {
    const int64_t start = __atomic_load_n(&profile_ack_start_ms, __ATOMIC_RELAXED);
    const unsigned profile = __atomic_load_n(&profile_ack_value, __ATOMIC_RELAXED);
    if (!profile || t < start) return false;
    const int64_t elapsed = t - start;
    const int64_t base = 10 * 240;
    if (elapsed < base) return (elapsed % 240) < 120;
    if (elapsed < base + 300) return false;
    const int64_t rest = elapsed - base - 300;
    return rest < (int64_t)profile * 240 && (rest % 240) < 120;
}

static bool wtf_profile_ack_active(int64_t t) {
    const int64_t start = __atomic_load_n(&profile_ack_start_ms, __ATOMIC_RELAXED);
    const unsigned profile = __atomic_load_n(&profile_ack_value, __ATOMIC_RELAXED);
    return profile >= 1 && profile <= 3 && t >= start &&
           t - start < (int64_t)(10 + profile)*240 + 300;
}

static bool pattern_on(int64_t elapsed, const int *dur, int n) {
    for(int i=0;i<n;i++) { if(elapsed < dur[i]) return (i%2)==0; elapsed-=dur[i]; }
    return false;
}
static void indicator_task(void *arg) {
    // Sole owner of indicator pins. Retain fractional pulse phase when RSSI
    // changes, so the flash cadence accelerates smoothly without phase jumps.
    int prev_led=-1, prev_buzzer=-1;
    bool had_signal = false;
    int32_t display_q8 = -127 * 256;
    uint32_t pulse_phase_q16 = 0;
    int64_t prior_tick = now_ms();
    while(1) {
        int64_t t=now_ms();
        int64_t dt=t-prior_tick;
        if (dt < 0) dt = 0;
        if (dt > 100) dt = 100; // don't fast-forward animations after stalls
        prior_tick=t;
        int u=ANIM_UNIT_MS;
        bool on=false, beep=false;
        if (fault_latched || (sos_preview && t < preview_until_ms)) {
            // Morse SOS (unchanged from v0.1.2). Fatal faults always win.
            int d[18]; int n=0;
            for(int j=0;j<9;j++) { d[n++]=(j>=3&&j<6)?3*u:u; if(j<8) d[n++]=(j==2||j==5)?3*u:u; }
            int64_t active=27*u;
            int64_t phase=t%(active+1000);
            on=pattern_on(phase,d,n);
            beep=on && WTF_BUZZER_ON_FAULT;
        } else if (t<boot_until_ms) {
            int d[]={u,u,u,u,u}; on=pattern_on(t-(boot_until_ms-5*u),d,5);
        } else if (!diagnostic_mode && wtf_profile_ack_active(t)) {
            on=wtf_profile_ack_on(t);
        } else if (t<selftest_until_ms) {
            int d[]={u,u,u}; on=pattern_on(t%WTF_SELFTEST_INTERVAL_MS,d,3);
        } else if (t<alert_until_ms) {
            int d[]={u,u,u,u,u}; on=pattern_on(t-(alert_until_ms-5*u),d,5);
            beep=on && WTF_BUZZER_ON_DETECTION;
        } else {
            int r=-127, peak=-127, votes=0;
            int64_t seen=0, peak_until=0;
            taskENTER_CRITICAL(&detection_lock);
            r=last_rssi; seen=last_seen_ms;
            peak=peak_rssi; peak_until=peak_hold_until_ms;
            votes=trend_score;
            taskEXIT_CRITICAL(&detection_lock);
            const int64_t age = t-seen;
            if(seen && age >= 0 && age < WTF_TRACK_TIMEOUT_MS) {
                int target = r;
                // Hold recent strong measurements briefly, then decay the
                // visual indication. Silence is not a measured falling RSSI.
                if (t < peak_until && peak > target) target = peak;
                if (age > WTF_RSSI_SILENCE_GRACE_MS)
                    target -= (int)((age-WTF_RSSI_SILENCE_GRACE_MS) *
                                    WTF_RSSI_SILENCE_FADE_DB_PER_S / 1000);
                if (target < -95) target=-95;
                if (!had_signal) {
                    display_q8 = target * 256;
                    pulse_phase_q16 = 0;
                    had_signal=true;
                } else if (target * 256 > display_q8) {
                    display_q8=target * 256; // stronger: quick response
                } else {
                    // Smooth decline at ~18 dBm/s, regardless of packet rate.
                    int32_t drop=(int32_t)((dt * WTF_RSSI_FALL_DB_PER_S * 256 + 999)/1000);
                    int32_t floor_q8=target*256;
                    display_q8 = display_q8-drop > floor_q8 ? display_q8-drop : floor_q8;
                }
                const int shown = (int)(display_q8 / 256);
                const int interval=wtf_rssi_interval_ms(shown);
                if (interval == 0) {
                    on=true; // solid = strong RSSI, NOT a distance guarantee
                } else {
                    pulse_phase_q16 = wtf_phase_advance_q16(pulse_phase_q16,
                                                                 dt,interval);
                    // Two ticks when the selected MAC's RSSI has *measurably*
                    // weakened; packet silence alone does not assert recession.
                    on = wtf_strength_led_on(pulse_phase_q16, interval, u,
                          votes <= -2 && age < WTF_RSSI_TREND_STALE_MS);
                }
            } else {
                had_signal=false;
                pulse_phase_q16=0;
                on=(t%WTF_SCANNING_INTERVAL_MS)<u;
            }
        }
#if HAS_LED
        if((int)on!=prev_led) { gpio_set_level(USER_LED_GPIO,on?LED_ON:LED_OFF);prev_led=on; }
#endif
#if WTF_ENABLE_BUZZER
        if((int)beep!=prev_buzzer) {gpio_set_level(USER_BUZZER_GPIO,beep?BUZZER_ON:BUZZER_OFF);prev_buzzer=beep;}
#else
        (void)beep;(void)prev_buzzer;
#endif
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}


#if WTF_ENABLE_DEBUG_CONSOLE
static void dump_logs_to_console(void) {
    ESP_LOGI(TAG, "--- START OF FLASH LOGS ---");

    // Open the file for reading
    FILE* f = fopen("/spiffs/detections.txt", "r");
    if (f == NULL) {
        ESP_LOGW(TAG, "No detection log found on flash.");
        ESP_LOGW(TAG, "--- END OF FLASH LOGS ---");
        return;
    }

    char line[160];
    // Read line by line until end of file
    while (fgets(line, sizeof(line), f)) {
        // Print the line; printf is used to avoid TAG prefixes for raw data
        printf("%s", line);
    }

    fclose(f);
    ESP_LOGI(TAG, "--- END OF FLASH LOGS ---");
}
#endif

static void init_spiffs(void) {
    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = "storage",
      .max_files = 5,
      .format_if_mount_failed = false
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        wtf_report_fault(WTF_F_SPIFFS, diagnostic_mode ? WTF_FATAL : WTF_DEGRADED, ret);
        return;
    }

    spiffs_ready = true;
    size_t total = 0, used = 0;
    ret = esp_spiffs_info("storage", &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Partition size: total: %zu, used: %zu", total, used);
    } else {
        wtf_report_fault(WTF_F_SPIFFS, WTF_DEGRADED, ret);
    }
}

static void logger_task(void *arg) {
    char record[180];
    bool write_error_reported = false;
    while(1) {
        if(xQueueReceive(log_queue,record,portMAX_DELAY)==pdTRUE && spiffs_ready) {
            FILE *f=fopen("/spiffs/detections.txt","a");
            if (!f) {
                if (!write_error_reported)
                    wtf_report_fault(WTF_F_LOGGING, WTF_DEGRADED, -1);
                write_error_reported = true;
            } else {
                int wrote = fprintf(f, "%s\n", record);
                int closed = fclose(f);
                if (wrote < 0 || closed != 0) {
                    if (!write_error_reported)
                        wtf_report_fault(WTF_F_LOGGING, WTF_DEGRADED, -2);
                    write_error_reported = true;
                } else write_error_reported = false;
            }
        }
    }
}

// WiFi SSID patterns to detect (case-insensitive)
static const char* wifi_ssid_patterns[] = {
    "flock", "Flock", "FLOCK", "FS Ext Battery", "Penguin", "Pigvision"
};

// Device name patterns for BLE advertisement detection
static const char* device_name_patterns[] = {
    "FS Ext Battery", "Penguin", "Flock", "Pigvision"
};

/**
 * @brief Structure to define MAC patterns (OUIs) to filter for.
 */
typedef struct {
    uint8_t oui[3];     /* The first 3 bytes of the MAC address */
    const char *label;  /* Manufacturer or device label */
} mac_filter_t;

/**
 * @brief List of MAC address patterns to match against.
 */
static const mac_filter_t TARGET_VENDORS[] = {
    {{0x70, 0xC9, 0x4E}, "Flock WiFi"},
    {{0x3C, 0x91, 0x80}, "Flock WiFi"},
    {{0xD8, 0xF3, 0xBC}, "Flock WiFi"},
    {{0x80, 0x30, 0x49}, "Flock WiFi"},
    {{0x14, 0x5A, 0xFC}, "Flock WiFi"},
    {{0x74, 0x4C, 0xA1}, "Flock WiFi"},
    {{0x08, 0x3A, 0x88}, "Flock WiFi"},
    {{0x9C, 0x2F, 0x9D}, "Flock WiFi"},
    {{0x94, 0x08, 0x53}, "Flock WiFi"},
    {{0xE4, 0xAA, 0xEA}, "Flock WiFi"},
    {{0x58, 0x8E, 0x81}, "Flock Battery"},
    {{0xCC, 0xCC, 0xCC}, "Flock Battery"},
    {{0xEC, 0x1B, 0xBD}, "Flock Battery"},
    {{0x90, 0x35, 0xEA}, "Flock Battery"},
    {{0x04, 0x0D, 0x84}, "Flock Battery"},
    {{0xF0, 0x82, 0xC0}, "Flock Battery"},
    {{0x1C, 0x34, 0xF1}, "Flock Battery"},
    {{0x38, 0x5B, 0x44}, "Flock Battery"},
    {{0x94, 0x34, 0x69}, "Flock Battery"},
    {{0xB4, 0xE3, 0xF9}, "Flock Battery"}
};

/**
 * @brief Searches for a MAC match in the TARGET_VENDORS array.
 * @param src_mac The 6-byte source MAC address.
 * @return Pointer to the matching mac_filter_t entry, or NULL if no match found.
 */
static const mac_filter_t* get_mac_filter(const uint8_t *src_mac)
{
    size_t count = sizeof(TARGET_VENDORS) / sizeof(TARGET_VENDORS[0]);

    for (size_t i = 0; i < count; i++) {
        // Compare only the first 3 bytes (the OUI)
        if (memcmp(src_mac, TARGET_VENDORS[i].oui, 3) == 0) {
            return &TARGET_VENDORS[i];
        }
    }
    return NULL;
}

/**
 * @brief Extract the source MAC address from an 802.11 frame.
 */
static bool extract_source_mac(const uint8_t *frame, uint8_t *src_mac)
{
    // Frame Control field (2 bytes)
    uint16_t fc = frame[0] | (frame[1] << 8);

    // ToDS / FromDS flags
    bool to_ds   = fc & (1 << 8);
    bool from_ds = fc & (1 << 9);

    if (!to_ds && !from_ds) {
        memcpy(src_mac, frame + 10, 6);
        return true;
    }

    if (to_ds && !from_ds) {
        memcpy(src_mac, frame + 10, 6);
        return true;
    }

    if (!to_ds && from_ds) {
        memcpy(src_mac, frame + 16, 6);
        return true;
    }

    return false;
}

static int ble_monitor_event(struct ble_gap_event *event, void *arg) {
    if (!ble_enabled) return 0;
    if (event->type == BLE_GAP_EVENT_DISC) {
        __atomic_fetch_add(&ble_rx_adverts, 1, __ATOMIC_RELAXED);
        struct ble_hs_adv_fields fields;
        int rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
        if (rc != 0) return 0;

        bool is_target = false;
        char name[33] = {0};
        const char* match_type = "None";

        // Verbose Logging when any BLE is discovered.
        ESP_LOGV(TAG, "BLE Discovered: %02X:%02X:%02X:%02X:%02X:%02X | RSSI: %d",
                 event->disc.addr.val[5], event->disc.addr.val[4], event->disc.addr.val[3],
                 event->disc.addr.val[2], event->disc.addr.val[1], event->disc.addr.val[0],
                 event->disc.rssi);

        // --- SIGNATURE 1: MAC PREFIX CHECK ---
        for (int i = 0; i < sizeof(TARGET_VENDORS)/sizeof(mac_filter_t); i++) {
            if (memcmp(TARGET_VENDORS[i].oui, event->disc.addr.val + 3, 3) == 0) {
                // PRESERVED LOG
                ESP_LOGW(TAG, "MATCH: MAC Prefix (%s)", TARGET_VENDORS[i].label);
                is_target = true;
                match_type = TARGET_VENDORS[i].label;
            }
        }

        // --- SIGNATURE 2: ADVANCED NAME CHECK ---
        if (fields.name != NULL) {
            size_t name_len = fields.name_len > 32 ? 32 : fields.name_len;
            memcpy(name, fields.name, name_len);
            name[name_len] = '\0'; 

            // Debug Log All BLE Devices with names to know it is working
            ESP_LOGD(TAG, "BLE Device Name: %s, %d", name, event->disc.rssi);

            for (int i = 0; i < sizeof(device_name_patterns)/sizeof(char*); i++) {
                if (strcasestr(name, device_name_patterns[i]) != NULL) {
                    ESP_LOGW(TAG, "MATCH: Name Pattern (%s)", name);
                    is_target = true;
                    match_type = "Name Match";
                }
            }

            if (name_len == 10) {
                bool all_digits = true;
                for(int i=0; i<10; i++) if(name[i] < '0' || name[i] > '9') all_digits = false;
                if (all_digits) {
                    ESP_LOGW(TAG, "MATCH: 10-Digit Serial (%s)", name);
                    is_target = true;
                    match_type = "Serial Number";
                }
            }
        }

        // --- SIGNATURE 3: MANUFACTURER ID (0x09C8) ---
        if (fields.mfg_data != NULL && fields.mfg_data_len >= 2) {
            uint16_t company_id = fields.mfg_data[0] | (fields.mfg_data[1] << 8);
            if (company_id == 0x09C8) {
                ESP_LOGW(TAG, "MATCH: Hardware ID 0x09C8 (Xuntong/Flock)");
                is_target = true;
                match_type = "Hardware ID";
            }
        }

        if (is_target) {
            char log_buf[150];
            snprintf(log_buf, sizeof(log_buf), "[BLE %s] MAC: %02X:%02X:%02X:%02X:%02X:%02X Name: %s RSSI: %d",
                     match_type,
                     event->disc.addr.val[5], event->disc.addr.val[4], event->disc.addr.val[3],
                     event->disc.addr.val[2], event->disc.addr.val[1], event->disc.addr.val[0],
                     name[0] ? name : "N/A", event->disc.rssi);

            ESP_LOGW("DETECTION", ">>> %s <<<", log_buf);
            uint8_t id[6]; for(int i=0;i<6;i++) id[i]=event->disc.addr.val[5-i];
            observe_detection(2,id,event->disc.rssi,log_buf);
        }
    }
    return 0;
}

static void wifi_sniffer_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    const wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    if (!wifi_enabled || !buf) return;
    __atomic_fetch_add(&wifi_rx_packets, 1, __ATOMIC_RELAXED);
    if (pkt->rx_ctrl.sig_len < 38) return;
    const uint8_t *payload = pkt->payload;
    uint8_t src_mac[6];
    bool has_mac = extract_source_mac(payload, src_mac);

    // 1. SSID SEARCHING (Beacons and Probe Responses)
    if (type == WIFI_PKT_MGMT && (payload[0] == 0x80 || payload[0] == 0x50)) {
        uint8_t tag_type = payload[36];
        uint8_t tag_len = payload[37];

        if (tag_type == 0 && tag_len > 0 && pkt->rx_ctrl.sig_len >= 38 + tag_len) {
            char ssid[33] = {0};
            memcpy(ssid, &payload[38], tag_len > 32 ? 32 : tag_len);

            // Debug logging to verify WIFI Scanning
            ESP_LOGD(TAG, "WiFi SSID Seen: %s", ssid);

#if WTF_ACCEPT_LAB_BEACONS
            // Only enabled for a development detector. A test beacon exercises
            // real Wi-Fi reception, scan locking and RSSI, but is NOT logged as
            // a genuine target detection and does not touch SPIFFS.
            if (has_mac && strcmp(ssid, "WTF-LAB-01") == 0) {
                wtf_scan_note_match(src_mac, pkt->rx_ctrl.channel, 2);
                wtf_update_rssi(1,src_mac,pkt->rx_ctrl.rssi);
                if (unique_transmitter(1,src_mac))
                    ESP_LOGI(TAG,"LAB beacon accepted on channel %u",pkt->rx_ctrl.channel);
            }
#endif

            for (int i = 0; i < sizeof(wifi_ssid_patterns)/sizeof(char*); i++) {
                if (strstr(ssid, wifi_ssid_patterns[i]) != NULL) {
                    ESP_LOGI(TAG, "WIFI SSID MATCH: %s", ssid);

                    // Prepare the detailed string for both Log and Flash
                    char log_buf[160];
                    snprintf(log_buf, sizeof(log_buf), 
                             "[WIFI SSID] %s | MAC: %02X:%02X:%02X:%02X:%02X:%02X | RSSI: %d",
                             ssid,
                             has_mac ? src_mac[0]:0, has_mac ? src_mac[1]:0, has_mac ? src_mac[2]:0,
                             has_mac ? src_mac[3]:0, has_mac ? src_mac[4]:0, has_mac ? src_mac[5]:0,
                             pkt->rx_ctrl.rssi);

                    ESP_LOGW("DETECTION", ">>> %s <<<", log_buf);

                    if(has_mac) {
                        wtf_scan_note_match(src_mac, pkt->rx_ctrl.channel, 2);
                        observe_detection(1,src_mac,pkt->rx_ctrl.rssi,log_buf);
                    }
                }
            }
        }
    }

    // 2. MAC MATCHING
    if (has_mac) {
        // Verbose logging to catch all WIFI Packets
        ESP_LOGV(TAG, "WiFi Packet from: %02X:%02X:%02X:%02X:%02X:%02X", 
                 src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5]);

        const mac_filter_t *match = get_mac_filter(src_mac);
        if (match != NULL) {
            // Prepare the detailed string for both Log and Flash
            char log_buf[160];
            snprintf(log_buf, sizeof(log_buf), 
                     "[WIFI MAC] %s | MAC: %02X:%02X:%02X:%02X:%02X:%02X | RSSI: %d",
                     match->label,
                     src_mac[0], src_mac[1], src_mac[2],
                     src_mac[3], src_mac[4], src_mac[5],
                     pkt->rx_ctrl.rssi);

            ESP_LOGW("DETECTION", ">>> %s <<<", log_buf);

            wtf_scan_note_match(src_mac, pkt->rx_ctrl.channel, 1);
            observe_detection(1,src_mac,pkt->rx_ctrl.rssi,log_buf);
        }
    }
}

static void wifi_init_sniffer(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) { wtf_report_fault(WTF_F_WIFI,WTF_FATAL,err); return; }
    err = esp_event_loop_create_default();
    if (err != ESP_OK) { wtf_report_fault(WTF_F_WIFI,WTF_FATAL,err); return; }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    // ESP_ERROR_CHECK would abort before SOS could display the fault.
#define WTF_WIFI_CHECK(call) do { \
        err = (call); \
        if (err != ESP_OK) { wtf_report_fault(WTF_F_WIFI,WTF_FATAL,err); return; } \
    } while (0)
    WTF_WIFI_CHECK(esp_wifi_init(&cfg));
    WTF_WIFI_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    WTF_WIFI_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    WTF_WIFI_CHECK(esp_wifi_start());
    WTF_WIFI_CHECK(esp_wifi_set_promiscuous(false));
    WTF_WIFI_CHECK(esp_wifi_set_promiscuous_rx_cb(wifi_sniffer_cb));
    WTF_WIFI_CHECK(esp_wifi_set_promiscuous(true));
#undef WTF_WIFI_CHECK

    wifi_ready = true;
    ESP_LOGI(TAG, "WiFi promiscuous mode enabled with MAC filtering");
}

static void ble_host_task(void *param) {
    ESP_LOGI(TAG, "BLE host task running on core %d", xPortGetCoreID());
    (void)param;
    nimble_port_run(); // returns only when the host is stopped
    // The scanner is expected to run continuously in this firmware.
    if (ble_enabled && !fault_latched)
        wtf_report_fault(WTF_F_BLE, WTF_FATAL, -1);
    nimble_port_freertos_deinit();
}

static int ble_start_scan(void) {
    struct ble_gap_disc_params disc_params={0};
    disc_params.filter_duplicates=1;
    disc_params.passive=0;
    disc_params.itvl=128;
    disc_params.window=40;
    return ble_gap_disc(BLE_OWN_ADDR_PUBLIC,BLE_HS_FOREVER,&disc_params,ble_monitor_event,NULL);
}
static void ble_on_sync(void) {
    if(!ble_enabled) return;
    int rc=ble_start_scan();
    ble_ready=(rc==0);
    if (rc) wtf_report_fault(WTF_F_BLE, WTF_FATAL, rc);
    else ESP_LOGI(TAG,"BLE scanning started");
}
static void ble_init_monitor(void) {
    esp_err_t err=nimble_port_init();
    if (err != ESP_OK) {wtf_report_fault(WTF_F_BLE,WTF_FATAL,err);return;}
    ble_hs_cfg.sync_cb=ble_on_sync;
    nimble_port_freertos_init(ble_host_task);
}

// NVS holds the last user-selected profile across power loss and resets.
static void wtf_profile_load(void) {
    nvs_handle_t h;
    esp_err_t err = nvs_open("wtf", NVS_READONLY, &h);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) wtf_report_fault(WTF_F_NVS,WTF_DEGRADED,err);
        selected_scan_profile = WTF_SCAN_P2_BALANCED;
        return;
    }
    uint8_t saved = WTF_SCAN_P2_BALANCED;
    err = nvs_get_u8(h,"scan_profile", &saved);
    nvs_close(h);
    if (err == ESP_OK && saved >= 1 && saved <= 3) selected_scan_profile = saved;
    else selected_scan_profile = WTF_SCAN_P2_BALANCED;
}

static void wtf_profile_save(uint8_t profile) {
    nvs_handle_t h;
    esp_err_t err = nvs_open("wtf",NVS_READWRITE,&h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h,"scan_profile",profile);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) wtf_report_fault(WTF_F_NVS,WTF_DEGRADED,err);
}

static void wtf_profile_button_task(void *arg) {
    (void)arg;
    int stable = 1, previous_sample = 1;
    int64_t changed_at = now_ms();
    while (1) {
        const int sample = gpio_get_level((gpio_num_t)WTF_PROFILE_BUTTON_GPIO);
        const int64_t t=now_ms();
        if (sample != previous_sample) { previous_sample = sample; changed_at=t; }
        if (sample != stable && t - changed_at >= 45) {
            stable=sample;
            if (stable==0 && !diagnostic_mode) {
                uint8_t next=(uint8_t)(selected_scan_profile % 3 + 1);
                __atomic_store_n(&selected_scan_profile,next,__ATOMIC_RELAXED);
                wtf_scan_reset_lock();
                wtf_profile_save(next); // task context, never the RF callback
                __atomic_store_n(&profile_ack_value,next,__ATOMIC_RELAXED);
                __atomic_store_n(&profile_ack_start_ms,now_ms(),__ATOMIC_RELAXED);
                ESP_LOGI(TAG,"Scan profile P%u selected", (unsigned)next);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

static void wtf_profile_button_init(void) {
    const gpio_config_t cfg = {
        .pin_bit_mask=(1ULL << WTF_PROFILE_BUTTON_GPIO),
        .mode=GPIO_MODE_INPUT,
        .pull_up_en=GPIO_PULLUP_ENABLE,
        .pull_down_en=GPIO_PULLDOWN_DISABLE,
        .intr_type=GPIO_INTR_DISABLE
    };
    esp_err_t err=gpio_config(&cfg);
    if(err != ESP_OK) {wtf_report_fault(WTF_F_GPIO,WTF_DEGRADED,err);return;}
    if(xTaskCreate(wtf_profile_button_task,"profile_button",2048,NULL,2,NULL)!=pdPASS)
        wtf_report_fault(WTF_F_TASK,WTF_DEGRADED,6);
}

static void channel_hop_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG,"Adaptive channel scheduler started");
    unsigned consecutive_errors=0;
    bool reported_error=false;
    const int64_t ble_sync_deadline=now_ms()+7000;
    bool ble_sync_checked=false;
    uint32_t step=0;
    uint8_t last_profile=0, previous_channel=0;
    int64_t next_hop_ms=0;
    while (1) {
        const int64_t t=now_ms();
        if (!ble_sync_checked && t >= ble_sync_deadline) {
            ble_sync_checked=true;
            if (!ble_ready && ble_enabled && !fault_latched)
                wtf_report_fault(WTF_F_BLE,WTF_FATAL,-2);
        }
        const uint8_t profile=__atomic_load_n(&selected_scan_profile,__ATOMIC_RELAXED);
        if (profile != last_profile) {
            last_profile=profile;
            step=0;
            next_hop_ms=0;
            previous_channel=0;
        }
        if (wifi_enabled && wifi_ready && t >= next_hop_ms) {
            uint8_t channel=0;
            bool locked=false;
            taskENTER_CRITICAL(&scan_lock_mux);
            if (channel_locked && !wtf_scan_lock_valid(t,match_last_ms,channel_lock_started_ms)) {
                channel_locked=false;
                lock_cooldown_until_ms=t+WTF_SCAN_LOCK_COOLDOWN_MS;
                match_count=0;
                ++channel_unlock_count;
            }
            if (channel_locked) {
                locked=true;
                channel=match_channel;
            }
            taskEXIT_CRITICAL(&scan_lock_mux);
            if (!locked) channel=wtf_scan_next_channel(profile,step++);
            if (channel != previous_channel) {
                const esp_err_t err=esp_wifi_set_channel(channel,WIFI_SECOND_CHAN_NONE);
                if (err != ESP_OK) {
                    if (++consecutive_errors >= 3 && !reported_error) {
                        wtf_report_fault(WTF_F_WIFI_HOP,WTF_DEGRADED,err);
                        reported_error=true;
                    }
                } else {
                    consecutive_errors=0;
                    reported_error=false;
                    previous_channel=channel;
                    __atomic_store_n(&active_wifi_channel,channel,__ATOMIC_RELAXED);
                }
            }
            next_hop_ms=t+(locked ? 60 : wtf_scan_dwell_ms(profile,channel));
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

#if WTF_ENABLE_DEBUG_CONSOLE
static void console_task(void *arg) {
    (void)arg;
    char line[128];
    printf("\nWTF v0.1.4 | type help\n"); fflush(stdout);
    while(1) {
        printf("wtf> "); fflush(stdout);
        if(!fgets(line,sizeof(line),stdin)) {clearerr(stdin);vTaskDelay(pdMS_TO_TICKS(150));continue;}
        line[strcspn(line,"\r\n")]=0;
        if(!strcmp(line,"help")) {
            puts("help | status | version | reset | reset reason | logs | logs stats | logs clear CONFIRM");
            puts("wifi on/off | ble on/off | test selftest | test sos | test fault");
            puts("test detect <rssi> | test rssi <rssi> | test clear");
        } else if(!strcmp(line,"version")) {
            printf("WTF v0.1.4 / %s %s\n",__DATE__,__TIME__);
        } else if(!strcmp(line,"status")) {
            int64_t t=now_ms();
            printf("uptime=%lldms mode=%s wifi=%s ble=%s spiffs=%s fatal=%d fatal_code=F%02u last_code=F%02u detail=%d events=%u rssi=%d detections=%u wifi_rx=%" PRIu32 " ble_rx=%" PRIu32 " trend_votes=%d speed=%d profile=P%u channel=%u locks=%" PRIu32 " matches=%" PRIu32 "\n",
                (long long)t,diagnostic_mode?"diagnostic":"normal",
                wifi_enabled?"on":"off",ble_enabled?"on":"off",spiffs_ready?"ok":"unavailable",
                fault_latched,(unsigned)fatal_fault_code,(unsigned)last_fault_code,
                last_fault_detail,fault_events,last_rssi,detection_count,
                __atomic_load_n(&wifi_rx_packets,__ATOMIC_RELAXED),
                __atomic_load_n(&ble_rx_adverts,__ATOMIC_RELAXED),trend_score,WTF_ANIMATION_SPEED,
                (unsigned)selected_scan_profile,(unsigned)active_wifi_channel,
                __atomic_load_n(&channel_lock_count,__ATOMIC_RELAXED),
                __atomic_load_n(&matched_wifi_packets,__ATOMIC_RELAXED));
        } else if(!strcmp(line,"reset reason")) {
            printf("reset_reason=%d (ESP-IDF esp_reset_reason enum)\n",esp_reset_reason());
        } else if(!strcmp(line,"reset")) {
            puts("Rebooting...");fflush(stdout);vTaskDelay(pdMS_TO_TICKS(100));esp_restart();
        } else if(!strcmp(line,"logs")) {
            if(spiffs_ready) dump_logs_to_console();else puts("SPIFFS unavailable");
        } else if(!strcmp(line,"logs stats")) {
            size_t total=0,used=0;
            if(spiffs_ready && esp_spiffs_info("storage",&total,&used)==ESP_OK) {
                struct stat st; long bytes=stat("/spiffs/detections.txt",&st)==0?(long)st.st_size:0;
                printf("partition=%zu used=%zu log_bytes=%ld\n",total,used,bytes);
            } else puts("SPIFFS unavailable");
        } else if(!strcmp(line,"logs clear CONFIRM")) {
            if(!spiffs_ready) puts("SPIFFS unavailable");
            else if(remove("/spiffs/detections.txt")==0) puts("Detection log deleted");
            else puts("No log deleted (missing or error)");
        } else if(!strcmp(line,"wifi off") || !strcmp(line,"wifi on")) {
            bool enable=!strcmp(line,"wifi on");
            if(!wifi_ready) puts("Wi-Fi not initialized");
            else {esp_err_t err=esp_wifi_set_promiscuous(enable);if(err==ESP_OK){wifi_enabled=enable;puts("Wi-Fi scan state updated");}else {wtf_report_fault(WTF_F_WIFI,WTF_DEGRADED,err);printf("Wi-Fi error: %s\n",esp_err_to_name(err));}}
        } else if(!strcmp(line,"ble off") || !strcmp(line,"ble on")) {
            bool enable=!strcmp(line,"ble on");
            if(!ble_ready) puts("BLE not initialized");
            else if(!enable) {ble_enabled=false;int rc=ble_gap_disc_cancel();printf("BLE disabled (cancel rc=%d)\n",rc);}
            else {int rc=ble_start_scan();
                if(rc==0){ble_enabled=true;puts("BLE scanning enabled");}else {wtf_report_fault(WTF_F_BLE,WTF_DEGRADED,rc);printf("BLE start error %d\n",rc);}}
        } else if(!strcmp(line,"test selftest")) {
            if(fault_latched) puts("Fault latched; reset required");
            else {selftest_until_ms=now_ms()+WTF_SELFTEST_INTERVAL_MS*2;puts("Self-test replay");}
        } else if(!strcmp(line,"test sos")) {
            if(fault_latched) puts("Fault already latched");
            else {sos_preview=true;preview_until_ms=now_ms()+2*(27*ANIM_UNIT_MS+1000);puts("SOS preview (2 cycles)");}
        } else if(!strcmp(line,"test fault")) {
            wtf_report_fault(WTF_F_TEST_SOS,WTF_FATAL,0);puts("Fault latched. Use reset to recover.");
        } else if(!strcmp(line,"test clear")) {
            sos_preview=false;alert_until_ms=0;
            wtf_reset_rssi_tracking();
            puts("Synthetic inputs cleared; latched faults unaffected");
        } else if(!strncmp(line,"test rssi ",10) || !strncmp(line,"test detect ",12)) {
            bool detect=!strncmp(line,"test detect ",12);
            char *p=line+(detect?12:10),*end=NULL;long v=strtol(p,&end,10);
            if(p==end || *end || v<-127 || v>20) puts("Expected RSSI -127..20");
            else {static const uint8_t test_mac[6]={0x57,0x54,0x46,0,0,1};
                wtf_update_rssi(3,test_mac,(int)v);
                if(detect){alert_until_ms=now_ms()+5*ANIM_UNIT_MS;puts("Synthetic new-detection event; not logged");}
                else puts("Synthetic RSSI sample processed by live filter (expires in 5 seconds)");}
        } else if(*line) puts("Unknown command; type help");
    }
}
#endif

// A real, opt-in boot self-test. SPIFFS write/read is checked; Wi-Fi and BLE
// initialization is checked by their ordinary startup paths; real RF RX is
// counted during two idle heartbeats (silence is inconclusive, not a fault).
// The RSSI sweep goes through wtf_update_rssi(), the live measurement filter.
// It ends by latching the same SOS mechanism used for genuine fatal errors.
static bool wtf_check_spiffs_rw(void) {
    if (!spiffs_ready) return false;
    static const char expected[] = "WTF_DIAG_STORAGE_V1\n";
    const char *path = "/spiffs/.wtf_diagnostic_probe";
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(expected, 1, sizeof(expected)-1, f) == sizeof(expected)-1;
    if (fclose(f) != 0) ok = false;
    if (!ok) { remove(path); return false; }
    f = fopen(path, "rb");
    if (!f) { remove(path); return false; }
    char buf[sizeof(expected)] = {0};
    size_t got = fread(buf, 1, sizeof(expected), f);
    if (ferror(f)) ok = false;
    if (fclose(f) != 0) ok = false;
    if (remove(path) != 0) ok = false;
    return ok && got == sizeof(expected)-1 &&
           memcmp(buf, expected, sizeof(expected)-1) == 0;
}

// Verify actual source-aware update path, smoothing, trend and mapping math.
// The same pure functions are independently compiled/run in tests/ on Linux.
static bool wtf_check_rssi_math(void) {
    static const uint8_t test_mac[6]={0x57,0x54,0x46,0,0,1};
    wtf_reset_rssi_tracking();
    wtf_update_rssi(3,test_mac,-50);
    wtf_update_rssi(3,test_mac,-90);
    taskENTER_CRITICAL(&detection_lock);
    int result = last_rssi;
    taskEXIT_CRITICAL(&detection_lock);
    if (result != -60) return false;
    wtf_update_rssi(3,test_mac,-35);
    taskENTER_CRITICAL(&detection_lock);
    result = last_rssi;
    last_seen_ms = now_ms() - WTF_TRACK_TIMEOUT_MS - 1;
    taskEXIT_CRITICAL(&detection_lock);
    if (result != -35) return false;
    wtf_update_rssi(3,test_mac,-75);
    taskENTER_CRITICAL(&detection_lock);
    result = last_rssi;
    taskEXIT_CRITICAL(&detection_lock);
    if (result != -75) return false;
    wtf_reset_rssi_tracking();
    static const int levels[]  = {-95,-90,-80,-70,-60,-50,-46,-45};
    static const int periods[] = {3000,2600,1800,1100,565,263,125,0};
    for (unsigned i = 0; i < sizeof(levels)/sizeof(levels[0]); ++i)
        if (wtf_rssi_interval_ms(levels[i]) != periods[i]) return false;
    for (int d=-95;d < -45; ++d)
        if (wtf_rssi_interval_ms(d+1) >= wtf_rssi_interval_ms(d)) return false;
    int vote=wtf_trend_vote(0,-80,-75);
    vote=wtf_trend_vote(vote,-75,-70);
    if (vote < 2) return false;
    vote=wtf_trend_vote(vote,-70,-75);
    vote=wtf_trend_vote(vote,-75,-80);
    if (vote > -2) return false;
    if (!wtf_strength_led_on(0, 700, ANIM_UNIT_MS, false)) return false;
    if (wtf_strength_led_on((uint32_t)(80*65536/700),700,70,false)) return false;
    if (!wtf_strength_led_on((uint32_t)(150*65536/700),700,70,true)) return false;
    if (wtf_strength_led_on((uint32_t)(150*65536/700),700,70,false)) return false;
    if (wtf_phase_advance_q16(0,100,1000) == 0) return false;
    wtf_reset_rssi_tracking();
    return true;
}

static void wtf_diagnostic_task(void *arg) {
    (void)arg;
    if (!wtf_check_spiffs_rw()) {
        wtf_report_fault(WTF_F_SELFTEST, WTF_FATAL, 1); // persistent storage test
        vTaskDelete(NULL);
        return;
    }
    // NimBLE starts asynchronously; let its host sync callback report ready.
    int64_t sync_deadline = now_ms() + 7000;
    while (!ble_ready && !fault_latched && now_ms() < sync_deadline)
        vTaskDelay(pdMS_TO_TICKS(50));
    if (fault_latched) {vTaskDelete(NULL);return;}
    if (!wifi_ready || !ble_ready) {
        wtf_report_fault(WTF_F_SELFTEST, WTF_FATAL, 2); // radio init/host sync failed
        vTaskDelete(NULL);
        return;
    }
    if (!wtf_check_rssi_math()) {
        wtf_report_fault(WTF_F_SELFTEST, WTF_FATAL, 3); // filter or LED thresholds
        vTaskDelete(NULL);
        return;
    }
    // Two full idle heartbeats, with real Wi-Fi/BLE reception counters active.
    uint32_t wifi_before = __atomic_load_n(&wifi_rx_packets,__ATOMIC_RELAXED);
    uint32_t ble_before  = __atomic_load_n(&ble_rx_adverts,__ATOMIC_RELAXED);
    int64_t deadline = now_ms() + 2 * WTF_SCANNING_INTERVAL_MS;
    while (now_ms() < deadline && !fault_latched)
        vTaskDelay(pdMS_TO_TICKS(50));
    diagnostic_wifi_rx = __atomic_load_n(&wifi_rx_packets,__ATOMIC_RELAXED) - wifi_before;
    diagnostic_ble_rx = __atomic_load_n(&ble_rx_adverts,__ATOMIC_RELAXED) - ble_before;
    // Zero RF traffic alone is NOT proof that the receiver failed.
    ESP_LOGI(TAG, "DIAG radio window: wifi_packets=%" PRIu32 " ble_adverts=%" PRIu32,
             diagnostic_wifi_rx, diagnostic_ble_rx);
    if (fault_latched) {vTaskDelete(NULL);return;}

    // Exercise the SAME per-source filter as real radio packets.
    // 5 dBm increments, with 2s per step rising and 1.5s falling.
    static const uint8_t test_mac[6]={0x57,0x54,0x46,0,0,1};
    wtf_reset_rssi_tracking();
    for (int dbm=-95; dbm <= -45 && !fault_latched; dbm+=5) {
        for (int n=0; n<8 && !fault_latched; ++n) {
            wtf_update_rssi(3,test_mac,dbm);
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }
    // Maximum strength is a relative indication; hold solid for two seconds.
    for (int n=0; n<8 && !fault_latched; ++n) {
        wtf_update_rssi(3,test_mac,-45);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    // Deliberately weaken the SAME source so the receding double-tick
    // path runs before the final SOS. No synthetic entries are logged.
    for (int dbm=-50; dbm >= -95 && !fault_latched; dbm-=5) {
        for (int n=0; n<6 && !fault_latched; ++n) {
            wtf_update_rssi(3,test_mac,dbm);
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }
    // Allow the fading-on-silence behavior to run without new samples.
    if (!fault_latched) vTaskDelay(pdMS_TO_TICKS(2000));
    if (!fault_latched) {
        // Diagnostic ONLY: deliberate SOS latch, not a real system failure.
        wtf_report_fault(WTF_F_TEST_SOS, WTF_FATAL, 0);
    }
    vTaskDelete(NULL);
}

void app_main(void) {
#if ENABLE_EXT_ANTENNA
    gpio_reset_pin(EXT_ANT_PIN1);gpio_reset_pin(EXT_ANT_PIN2);
    gpio_set_direction(EXT_ANT_PIN1,GPIO_MODE_OUTPUT);gpio_set_direction(EXT_ANT_PIN2,GPIO_MODE_OUTPUT);
    gpio_set_level(EXT_ANT_PIN1,0);gpio_set_level(EXT_ANT_PIN2,1);
#endif
#if HAS_LED
    esp_err_t led_err = gpio_reset_pin(USER_LED_GPIO);
    if (led_err == ESP_OK) led_err = gpio_set_level(USER_LED_GPIO,LED_OFF);
    if (led_err == ESP_OK) led_err = gpio_set_direction(USER_LED_GPIO,GPIO_MODE_OUTPUT);
    if (led_err != ESP_OK) wtf_report_fault(WTF_F_GPIO,WTF_FATAL,led_err);
#endif
#if WTF_ENABLE_BUZZER
    gpio_reset_pin(USER_BUZZER_GPIO);
    gpio_set_level(USER_BUZZER_GPIO,BUZZER_OFF);
    gpio_set_direction(USER_BUZZER_GPIO,GPIO_MODE_OUTPUT);
#endif
    diagnostic_mode = wtf_diagnostic_jumper_present();
    int64_t t = now_ms();
    boot_until_ms = t + 5 * ANIM_UNIT_MS;
    selftest_until_ms = diagnostic_mode ? boot_until_ms : boot_until_ms + 2*WTF_SELFTEST_INTERVAL_MS;
    if (xTaskCreate(indicator_task,"indicator",3072,NULL,5,NULL) != pdPASS)
        wtf_report_fault(WTF_F_TASK,WTF_FATAL,1); // no LED indication if task cannot start

    esp_err_t ret = nvs_flash_init();
    if (ret != ESP_OK) wtf_report_fault(WTF_F_NVS,WTF_FATAL,ret);
    if (!fault_latched) {
        wtf_profile_load();
        if (!diagnostic_mode) {
            __atomic_store_n(&profile_ack_value,selected_scan_profile,__ATOMIC_RELAXED);
            __atomic_store_n(&profile_ack_start_ms,boot_until_ms,__ATOMIC_RELAXED);
        }
        wtf_profile_button_init();
        init_spiffs();
    }
    // Headless by default: SPIFFS history is retained, not dumped on boot.
    log_queue = xQueueCreate(32,180);
    if (log_queue) {
        if (xTaskCreate(logger_task,"logger",4096,NULL,4,NULL) != pdPASS)
            wtf_report_fault(WTF_F_TASK,WTF_FATAL,2);
    } else {
        wtf_report_fault(WTF_F_LOGGING,WTF_FATAL,0);
    }
    if (!fault_latched) {
        wifi_init_sniffer();
        if (wifi_ready) ble_init_monitor();
        if (wifi_ready && !fault_latched &&
            xTaskCreate(channel_hop_task,"channel_hop",2048,NULL,5,NULL) != pdPASS)
            wtf_report_fault(WTF_F_TASK,WTF_FATAL,3);
    }
#if WTF_ENABLE_DEBUG_CONSOLE
    if (xTaskCreate(console_task,"console",4096,NULL,3,NULL) != pdPASS)
        wtf_report_fault(WTF_F_TASK,WTF_DEGRADED,4);
#endif
    if (diagnostic_mode && !fault_latched) {
        if (xTaskCreate(wtf_diagnostic_task,"diagnostic",4096,NULL,3,NULL) != pdPASS)
            wtf_report_fault(WTF_F_TASK,WTF_FATAL,5);
    }
    ESP_LOGI(TAG,"v0.1.4 ready; boot_mode=%s LED_GPIO=%d speed=%d",
             diagnostic_mode?"diagnostic":"normal",USER_LED_GPIO,WTF_ANIMATION_SPEED);
}

#pragma once
#include <stdint.h>
#include <stdbool.h>

// Channel scheduler helpers: standard C so host tests exercise production logic.
// P1 = 1/6/11 only; P2 = priority weighted + rotating secondary;
// P3 = equal 1..13 coverage. Only configured 2.4GHz channels are considered.
typedef enum {
    WTF_SCAN_P1_PRIORITY = 1,
    WTF_SCAN_P2_BALANCED = 2,
    WTF_SCAN_P3_WIDE = 3
} wtf_scan_profile_t;

static inline uint8_t wtf_scan_next_channel(uint8_t profile, uint32_t step) {
    static const uint8_t priority[3] = {1, 6, 11};
    static const uint8_t secondary[10] = {2, 3, 4, 5, 7, 8, 9, 10, 12, 13};
    if (profile == WTF_SCAN_P1_PRIORITY) return priority[step % 3u];
    if (profile == WTF_SCAN_P3_WIDE) return (uint8_t)(1u + (step % 13u));
    // Six priority slots plus one rotating secondary slot per 7-step cycle.
    const uint32_t slot = step % 7u;
    return slot < 6u ? priority[slot % 3u] : secondary[(step / 7u) % 10u];
}

static inline int wtf_scan_dwell_ms(uint8_t profile, uint8_t channel) {
    (void)channel;
    return profile == WTF_SCAN_P3_WIDE ? 110 : 125;
}

// A lock is temporary even if the transmitter keeps talking, so a nearby
// transmitter cannot monopolize a single receiver indefinitely.
#define WTF_SCAN_LOCK_IDLE_MS 1800
#define WTF_SCAN_LOCK_MAX_MS 4500
#define WTF_SCAN_LOCK_COOLDOWN_MS 500
#define WTF_SCAN_CANDIDATE_WINDOW_MS 1200

static inline bool wtf_scan_lock_valid(int64_t now_ms, int64_t last_ms,
                                        int64_t began_ms) {
    return now_ms >= last_ms && now_ms >= began_ms &&
           now_ms - last_ms < WTF_SCAN_LOCK_IDLE_MS &&
           now_ms - began_ms < WTF_SCAN_LOCK_MAX_MS;
}

// Single high-confidence signature, or two matching observations from one
// address/channel within the candidate window. Never lock to unmatched traffic.
static inline bool wtf_scan_candidate_ready(int confidence, unsigned count,
                                             int64_t now_ms, int64_t first_ms) {
    return confidence >= 2 ||
        (count >= 2 && now_ms >= first_ms &&
         now_ms - first_ms <= WTF_SCAN_CANDIDATE_WINDOW_MS);
}

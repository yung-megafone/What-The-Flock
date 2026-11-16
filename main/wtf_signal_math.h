#pragma once
#include <stdint.h>
// What-The-Flock v0.1.3 RSSI math. Standard C: can be tested without ESP-IDF.
// These values are *relative RF indicators*, not calibrated distance estimates.

// RSSI in dBm (higher = stronger). 0 interval means solid ON.
// Piecewise linear interpolation gives a distinct interval for nearly every
// 1 dBm change, rather than the v0.1.2 five coarse speed steps.
static inline int wtf_rssi_interval_ms(int rssi) {
    typedef struct { int dbm, ms; } point_t;
    static const point_t curve[] = {
        {-95,3000}, {-90,2600}, {-80,1800}, {-70,1100},
        {-60,565}, {-50,263}, {-46,125}, {-45,0}
    };
    if (rssi <= curve[0].dbm) return curve[0].ms;
    if (rssi >= curve[7].dbm) return 0;
    for (unsigned i = 1; i < sizeof(curve)/sizeof(curve[0]); ++i) {
        if (rssi <= curve[i].dbm) {
            const point_t lo = curve[i-1], hi = curve[i];
            return lo.ms + (rssi - lo.dbm) * (hi.ms - lo.ms)
                   / (hi.dbm - lo.dbm);
        }
    }
    return 0;
}

// Asymmetric smoothing: promptly accept a stronger measurement; blend a
// weaker one 25% toward its new value. Fresh tracking starts at raw RSSI.
static inline int wtf_filter_rssi(int previous, int sample, int fresh) {
    return fresh || sample > previous ? sample : (previous * 3 + sample) / 4;
}

// +/- 3 dBm dead band rejects small fluctuations. Two successive meaningful
// movements in the same direction confirm a trend; opposite movement resets.
static inline int wtf_trend_vote(int score, int previous_raw, int sample) {
    if (sample >= previous_raw + 3)
        return score <= 0 ? 1 : (score < 3 ? score + 1 : 3);
    if (sample <= previous_raw - 3)
        return score >= 0 ? -1 : (score > -3 ? score - 1 : -3);
    return score;
}

// Fractional pulse phase: preserved across interval changes (no modulo(t,ms)
// jumps when RSSI accelerates). Q16 represents one complete cycle.
static inline uint32_t wtf_phase_advance_q16(uint32_t phase, int64_t dt_ms,
                                             int interval_ms) {
    if (interval_ms <= 0 || dt_ms <= 0) return phase & 0xffffu;
    return (phase + (uint32_t)((dt_ms * 65536) / interval_ms)) & 0xffffu;
}

// Full-period duty pulse, with optional second tick when a *measured* weak
// trend is confirmed. Interval 0 = solid. Width shrinks to preserve OFF time.
static inline int wtf_strength_led_on(uint32_t phase, int interval_ms,
                                      int unit_ms, int receding) {
    if (interval_ms <= 0) return 1;
    int elapsed_ms = (int)(((uint64_t)phase * (uint32_t)interval_ms) >> 16);
    int width = unit_ms < (interval_ms*3/5) ? unit_ms : interval_ms*3/5;
    if (elapsed_ms < width) return 1;
    const int second_start=width+55;
    const int second_width=(width/2 > 35) ? width/2 : 35;
    return receding && interval_ms > second_start+second_width+45 &&
           elapsed_ms >= second_start && elapsed_ms < second_start+second_width;
}

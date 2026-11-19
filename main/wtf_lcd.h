#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *label;
    int rssi;
    int64_t seen_ms;
} wtf_lcd_sample_t;

// The label points to static storage. All fields describe the same observation.
wtf_lcd_sample_t wtf_lcd_get_sample(void);
bool wtf_lcd_start(void);

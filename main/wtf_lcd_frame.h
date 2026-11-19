#pragma once

#include <stdbool.h>
#include <stdint.h>

enum { WTF_LCD_ROWS = 2, WTF_LCD_COLS = 16, WTF_LCD_BAR_CELL_PIXELS = 5 };

typedef struct {
    char rows[WTF_LCD_ROWS][WTF_LCD_COLS];
} wtf_lcd_frame_t;

typedef struct {
    wtf_lcd_frame_t frame;
    bool valid;
} wtf_lcd_cache_t;

typedef bool (*wtf_lcd_write_span_t)(void *context, unsigned row, unsigned col,
                                     const char *characters, unsigned length);

// Glyphs 1 through 5 represent partial through full bar cells.
void wtf_lcd_render(wtf_lcd_frame_t *frame, const char *label, int rssi,
                    int64_t seen_ms, int64_t now_ms);
bool wtf_lcd_present(wtf_lcd_cache_t *cache, const wtf_lcd_frame_t *frame,
                     wtf_lcd_write_span_t write_span, void *context);

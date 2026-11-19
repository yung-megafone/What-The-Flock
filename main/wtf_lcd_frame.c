#include <stdio.h>
#include <string.h>

#include "wtf_config.h"
#include "wtf_lcd_frame.h"

_Static_assert(WTF_LCD_BAR_MAX_DBM > WTF_LCD_BAR_MIN_DBM, "LCD signal range must be positive");

void wtf_lcd_render(wtf_lcd_frame_t *frame, const char *label, int rssi,
                    int64_t seen_ms, int64_t now_ms)
{
    memset(frame->rows, ' ', sizeof(frame->rows));
    frame->rows[1][0] = '[';
    frame->rows[1][WTF_LCD_COLS - 1] = ']';
    if (!label || !seen_ms || now_ms < seen_ms || now_ms - seen_ms >= WTF_TRACK_TIMEOUT_MS) {
        memcpy(frame->rows[0], "Scanning...", sizeof("Scanning...") - 1);
        return;
    }

    if (strcmp(label, "Flock Battery") == 0)
        label = "Flk Batt";
    else if (strcmp(label, "Flock WiFi") == 0)
        label = "Flk WiFi";
    char text[32];
    snprintf(text, sizeof(text), "%-8.8s%5ddBm", label, rssi);
    memcpy(frame->rows[0], text, WTF_LCD_COLS);

    const int range = WTF_LCD_BAR_MAX_DBM - WTF_LCD_BAR_MIN_DBM;
    const int bar_pixels = (WTF_LCD_COLS - 2) * WTF_LCD_BAR_CELL_PIXELS;
    if (rssi < WTF_LCD_BAR_MIN_DBM) rssi = WTF_LCD_BAR_MIN_DBM;
    if (rssi > WTF_LCD_BAR_MAX_DBM) rssi = WTF_LCD_BAR_MAX_DBM;
    int filled = ((rssi - WTF_LCD_BAR_MIN_DBM) * bar_pixels + range / 2) / range;
    for (unsigned col = 1; filled > 0; ++col) {
        const int cell = filled < WTF_LCD_BAR_CELL_PIXELS ? filled : WTF_LCD_BAR_CELL_PIXELS;
        frame->rows[1][col] = (char)cell;
        filled -= cell;
    }
}

bool wtf_lcd_present(wtf_lcd_cache_t *cache, const wtf_lcd_frame_t *frame,
                     wtf_lcd_write_span_t write_span, void *context)
{
    for (unsigned row = 0; row < WTF_LCD_ROWS; ++row) {
        const char *next = frame->rows[row];
        const char *previous = cache->frame.rows[row];
        unsigned col = 0;
        while (col < WTF_LCD_COLS) {
            if (cache->valid && next[col] == previous[col]) {
                ++col;
                continue;
            }
            const unsigned start = col;
            do {
                ++col;
            } while (col < WTF_LCD_COLS && (!cache->valid || next[col] != previous[col]));
            if (!write_span(context, row, start, next + start, col - start)) {
                cache->valid = false;
                return false;
            }
        }
    }
    cache->frame = *frame;
    cache->valid = true;
    return true;
}

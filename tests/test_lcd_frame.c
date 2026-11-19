#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "wtf_config.h"
#include "wtf_lcd_frame.h"

typedef struct {
    wtf_lcd_frame_t frame;
    unsigned writes;
    unsigned characters;
    unsigned fail_on_write;
} panel_t;

static bool write_span(void *context, unsigned row, unsigned col,
                       const char *characters, unsigned length)
{
    panel_t *panel = context;
    assert(row < WTF_LCD_ROWS && col + length <= WTF_LCD_COLS);
    panel->writes++;
    memcpy(&panel->frame.rows[row][col], characters, length);
    panel->characters += length;
    return panel->writes != panel->fail_on_write;
}

static void test_labels_and_bar(void)
{
    wtf_lcd_frame_t frame;
    wtf_lcd_render(&frame, "Flock Battery", -78, 1000, 1100);
    assert(memcmp(frame.rows[0], "Flk Batt  -78dBm", WTF_LCD_COLS) == 0);
    assert(memcmp(frame.rows[1], "[\5\5\5\5\5\5\5\5\5\3    ]", WTF_LCD_COLS) == 0);

    wtf_lcd_render(&frame, "Flock WiFi", -100, 1000, 1100);
    assert(memcmp(frame.rows[0], "Flk WiFi -100dBm", WTF_LCD_COLS) == 0);
    assert(memcmp(frame.rows[1], "[              ]", WTF_LCD_COLS) == 0);

    wtf_lcd_render(&frame, "Long signature", 20, 1000, 1100);
    assert(memcmp(frame.rows[0], "Long sig   20dBm", WTF_LCD_COLS) == 0);
    for (unsigned col = 1; col < WTF_LCD_COLS - 1; ++col)
        assert(frame.rows[1][col] == 5);
}

static void test_stale_signal(void)
{
    wtf_lcd_frame_t frame;
    wtf_lcd_render(&frame, "Lab WiFi", -78, 1000, 1000 + WTF_TRACK_TIMEOUT_MS - 1);
    assert(memcmp(frame.rows[0], "Lab WiFi  -78dBm", WTF_LCD_COLS) == 0);
    wtf_lcd_render(&frame, "Lab WiFi", -78, 1000, 1000 + WTF_TRACK_TIMEOUT_MS);
    assert(memcmp(frame.rows[0], "Scanning...     ", WTF_LCD_COLS) == 0);
    assert(memcmp(frame.rows[1], "[              ]", WTF_LCD_COLS) == 0);
}

static void test_incremental_refresh_and_recovery(void)
{
    wtf_lcd_cache_t cache = {0};
    panel_t panel = {0};
    wtf_lcd_frame_t frame;
    wtf_lcd_render(&frame, "Flock Battery", -78, 1000, 1100);
    assert(wtf_lcd_present(&cache, &frame, write_span, &panel));
    assert(panel.writes == 2 && panel.characters == 32);
    assert(wtf_lcd_present(&cache, &frame, write_span, &panel));
    assert(panel.writes == 2 && panel.characters == 32);

    wtf_lcd_render(&frame, "Flock Battery", -79, 1100, 1200);
    assert(wtf_lcd_present(&cache, &frame, write_span, &panel));
    assert(panel.writes == 4 && panel.characters == 34);
    assert(memcmp(&panel.frame, &frame, sizeof(frame)) == 0);

    wtf_lcd_render(&frame, "Flock WiFi", -64, 1200, 1300);
    panel.fail_on_write = 5;
    assert(!wtf_lcd_present(&cache, &frame, write_span, &panel));
    assert(panel.writes == 5);
    panel.characters = 0;
    assert(wtf_lcd_present(&cache, &frame, write_span, &panel));
    assert(panel.writes == 7 && panel.characters == 32);
    assert(memcmp(&panel.frame, &frame, sizeof(frame)) == 0);
}

int main(void)
{
    test_labels_and_bar();
    test_stale_signal();
    test_incremental_refresh_and_recovery();
    puts("PASS: LCD layout, RSSI bar, tracking timeout, changed spans, and repaint after failure");
    return 0;
}

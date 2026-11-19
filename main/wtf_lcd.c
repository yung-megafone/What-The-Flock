#include "wtf_config.h"

#if WTF_ENABLE_LCD

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wtf_lcd.h"
#include "wtf_lcd_frame.h"

// Common PCF8574 backpack: P0=RS, P1=R/W (held low), P2=E, P3=backlight.
// P4 through P7 carry the four HD44780 data bits.
enum {
    LCD_RS = 1 << 0,
    LCD_ENABLE = 1 << 2,
    LCD_BACKLIGHT = 1 << 3,
    LCD_I2C_HZ = 100000,
    LCD_TRANSFER_TIMEOUT_MS = 40,
    LCD_RETRY_MS = 3000,
};

static const char *const TAG = "WTF_LCD";

static void lcd_pack_nibble(uint8_t output[3], uint8_t nibble, uint8_t flags)
{
    const uint8_t pins = (nibble << 4) | LCD_BACKLIGHT | flags;
    output[0] = pins;
    output[1] = pins | LCD_ENABLE;
    output[2] = pins;
}

static void lcd_pack_byte(uint8_t output[6], uint8_t byte, uint8_t flags)
{
    lcd_pack_nibble(output, byte >> 4, flags);
    lcd_pack_nibble(output + 3, byte & 0x0f, flags);
}

static bool lcd_send(i2c_master_dev_handle_t device, const uint8_t *bytes, size_t count)
{
    return i2c_master_transmit(device, bytes, count, LCD_TRANSFER_TIMEOUT_MS) == ESP_OK;
}

static bool lcd_write_span(void *context, unsigned row, unsigned col,
                           const char *characters, unsigned length)
{
    static const uint8_t row_address[WTF_LCD_ROWS] = {0x80, 0xc0};
    uint8_t bytes[6 * (1 + WTF_LCD_COLS)];
    lcd_pack_byte(bytes, row_address[row] + col, 0);
    for (unsigned i = 0; i < length; ++i)
        lcd_pack_byte(bytes + 6 * (1 + i), (uint8_t)characters[i], LCD_RS);
    // At 100 kHz, the three expander writes per nibble provide ample setup,
    // hold, and execution time for ordinary LCD commands and data.
    return lcd_send(context, bytes, 6 * (1 + length));
}

static bool lcd_initialize(i2c_master_dev_handle_t device)
{
    // Include a full tick for the delay call's position within the current tick.
    const TickType_t long_wait = pdMS_TO_TICKS(5) + 2;
    const uint8_t idle = LCD_BACKLIGHT;
    if (!lcd_send(device, &idle, 1)) return false;
    // Allow any command latched by dropping E after a partial write to finish.
    vTaskDelay(long_wait);

    const uint8_t reset[] = {3, 3, 3, 2};
    for (unsigned i = 0; i < sizeof(reset); ++i) {
        uint8_t bytes[3];
        lcd_pack_nibble(bytes, reset[i], 0);
        if (!lcd_send(device, bytes, sizeof(bytes))) return false;
        if (i == 0)
            vTaskDelay(long_wait);
        else
            esp_rom_delay_us(150);
    }

    uint8_t setup[18];
    lcd_pack_byte(setup, 0x28, 0);      // 4-bit, two rows, 5x8 glyphs
    lcd_pack_byte(setup + 6, 0x08, 0);  // Keep the display off for the first paint
    lcd_pack_byte(setup + 12, 0x01, 0); // Clear once when initializing the panel
    if (!lcd_send(device, setup, sizeof(setup))) return false;
    vTaskDelay(long_wait);

    // Load five horizontally filled glyphs so the bar can move by part of a cell.
    uint8_t glyphs[6 * (1 + 8 * WTF_LCD_BAR_CELL_PIXELS)];
    lcd_pack_byte(glyphs, 0x48, 0); // Start with custom character slot 1
    unsigned offset = 6;
    for (unsigned width = 1; width <= WTF_LCD_BAR_CELL_PIXELS; ++width) {
        const uint8_t pixels = 0x1f & (0x1f << (WTF_LCD_BAR_CELL_PIXELS - width));
        for (unsigned row = 0; row < 8; ++row) {
            lcd_pack_byte(glyphs + offset, pixels, LCD_RS);
            offset += 6;
        }
    }

    uint8_t entry_mode[6];
    lcd_pack_byte(entry_mode, 0x06, 0); // Increment addresses, keep the screen fixed
    return lcd_send(device, entry_mode, sizeof(entry_mode)) &&
           lcd_send(device, glyphs, sizeof(glyphs));
}

static void lcd_task(void *arg)
{
    (void)arg;
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = WTF_LCD_SDA_GPIO,
        .scl_io_num = WTF_LCD_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = WTF_LCD_I2C_ADDR,
        .scl_speed_hz = LCD_I2C_HZ,
    };
    i2c_master_bus_handle_t bus = NULL;
    i2c_master_dev_handle_t device;
    esp_err_t err = i2c_new_master_bus(&bus_config, &bus);
    if (err == ESP_OK) {
        err = i2c_master_bus_add_device(bus, &device_config, &device);
        if (err != ESP_OK) i2c_del_master_bus(bus);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LCD bus setup failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(50) + 1); // HD44780 power-up time
    wtf_lcd_cache_t cache = {0};
    bool ready = false;
    bool reported_unavailable = false;
    for (;;) {
        // Use a normal write even on retry. The ESP-IDF driver can clear a
        // stuck bus before a write after a timeout or busy-bus indication.
        if (ready || lcd_initialize(device)) {
            const wtf_lcd_sample_t sample = wtf_lcd_get_sample();
            wtf_lcd_frame_t frame;
            wtf_lcd_render(&frame, sample.label, sample.rssi, sample.seen_ms,
                           esp_timer_get_time() / 1000);
            if (wtf_lcd_present(&cache, &frame, lcd_write_span, device)) {
                if (!ready) {
                    uint8_t display_on[6];
                    lcd_pack_byte(display_on, 0x0c, 0);
                    ready = lcd_send(device, display_on, sizeof(display_on));
                    if (ready) ESP_LOGI(TAG, "LCD ready at 0x%02X", WTF_LCD_I2C_ADDR);
                }
                if (ready) {
                    reported_unavailable = false;
                    vTaskDelay(pdMS_TO_TICKS(WTF_LCD_REFRESH_MS));
                    continue;
                }
            }
        }

        // A partial transfer can leave the LCD between nibbles. Start again
        // from the reset sequence and repaint both rows after a bus error.
        cache.valid = false;
        ready = false;
        if (!reported_unavailable) {
            ESP_LOGW(TAG, "LCD at 0x%02X unavailable; retrying", WTF_LCD_I2C_ADDR);
            reported_unavailable = true;
        }
        vTaskDelay(pdMS_TO_TICKS(LCD_RETRY_MS));
    }
}

bool wtf_lcd_start(void)
{
    return xTaskCreate(lcd_task, "lcd", 4096, NULL, tskIDLE_PRIORITY + 1, NULL) == pdPASS;
}

#endif

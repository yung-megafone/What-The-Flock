set(sources "main.c")
if(CONFIG_IDF_TARGET_ESP32C5)
    list(APPEND sources "wtf_lcd.c" "wtf_lcd_frame.c")
endif()

idf_component_register(SRCS ${sources} INCLUDE_DIRS "."
                       REQUIRES bt nvs_flash esp_driver_gpio esp_driver_ledc spiffs esp_wifi esp_event esp_netif esp_timer
                       PRIV_REQUIRES esp_driver_i2c)

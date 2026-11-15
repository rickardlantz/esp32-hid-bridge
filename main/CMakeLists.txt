idf_component_register(SRCS "bridge.c" "keyboard_layout.c" "esp_hid_gap.c" "display.c" "screen.c" "orientation.c" "accelerometer.c"
    INCLUDE_DIRS "." REQUIRES esp_hid bt nvs_flash esp_lcd esp_driver_spi esp_driver_gpio esp_driver_i2c)

idf_component_register(SRCS
    "bridge.c" "diagnostic_log.c" "mouse_report.c" "keyboard_layout.c" "esp_hid_gap.c" "display.c" "screen.c"
    "orientation.c" "accelerometer.c" "status_led.c" "status_led_pattern.c"
    "network_config.c" "fat_snapshot.c" "config_storage.c" "wifi_manager.c"
    "deskflow_client.c" "deskflow_transport.c" "deskflow_protocol.c" "deskflow_keys.c" "server_auth.c"
    INCLUDE_DIRS "." REQUIRES esp_hid bt nvs_flash esp_lcd esp_driver_spi
    esp_driver_gpio esp_driver_i2c esp_wifi esp_netif esp_event esp_timer
    esp_blockdev wear_levelling fatfs mbedtls lwip)
target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=tud_msc_scsi_cb")

#pragma once
#include "sdkconfig.h"

// Shared peripherals; keep board wiring out of application and display policy.
#define BOARD_LCD_MOSI 45
#define BOARD_LCD_CLK 40
#define BOARD_LCD_CS 42
#define BOARD_LCD_DC 41
#define BOARD_LCD_RST 39
#define BOARD_STATUS_LED_GPIO 38
#if CONFIG_BRIDGE_BOARD_LCD_147B
#define BOARD_NAME "ESP32-S3-LCD-1.47B"
#define BOARD_LCD_BL 46
#else
#define BOARD_NAME "ESP32-S3-LCD-1.47 (USB-A)"
#define BOARD_LCD_BL 48
#endif

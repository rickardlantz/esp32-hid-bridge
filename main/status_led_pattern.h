#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    LED_LINK_IDLE, LED_LINK_SCANNING, LED_LINK_AUTHENTICATING, LED_LINK_CONNECTED
} led_link_state_t;
typedef enum {
    LED_PATTERN_RED, LED_PATTERN_BLUE_FAST, LED_PATTERN_BLUE_SLOW, LED_PATTERN_GREEN,
    LED_PATTERN_ORANGE_BLINK, LED_PATTERN_ORANGE, LED_PATTERN_TURQUOISE_BLINK, LED_PATTERN_TURQUOISE
} led_pattern_t;
typedef struct { uint8_t red, green, blue; } led_color_t;

led_pattern_t status_led_pattern(led_link_state_t link, bool usb_ready);
// Network progress takes precedence; without an active Wi-Fi profile, show BLE.
led_pattern_t status_led_resolve(led_link_state_t ble, led_link_state_t wifi,
    led_link_state_t deskflow, bool usb_ready);
led_color_t status_led_color(led_pattern_t pattern, uint32_t elapsed_ms);

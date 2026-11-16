#include "status_led_pattern.h"

led_pattern_t status_led_resolve(led_link_state_t ble, led_link_state_t wifi,
    led_link_state_t deskflow, bool usb_ready)
{
    if (wifi == LED_LINK_CONNECTED) {
        if (deskflow == LED_LINK_CONNECTED) return LED_PATTERN_TURQUOISE;
        if (deskflow == LED_LINK_AUTHENTICATING || deskflow == LED_LINK_SCANNING)
            return LED_PATTERN_TURQUOISE_BLINK;
        return LED_PATTERN_ORANGE;
    }
    if (wifi == LED_LINK_SCANNING || wifi == LED_LINK_AUTHENTICATING) return LED_PATTERN_ORANGE_BLINK;
    return status_led_pattern(ble, usb_ready);
}

led_pattern_t status_led_pattern(led_link_state_t link, bool usb_ready)
{
    switch (link) {
    case LED_LINK_SCANNING: return LED_PATTERN_BLUE_FAST;
    case LED_LINK_AUTHENTICATING: return LED_PATTERN_BLUE_SLOW;
    case LED_LINK_CONNECTED: return usb_ready ? LED_PATTERN_GREEN : LED_PATTERN_RED;
    default: return LED_PATTERN_RED;
    }
}

led_color_t status_led_color(led_pattern_t pattern, uint32_t elapsed_ms)
{
    const uint8_t brightness = 24;
    switch (pattern) {
    case LED_PATTERN_BLUE_FAST:
        return (led_color_t){.blue = elapsed_ms % 200 < 100 ? brightness : 0};
    case LED_PATTERN_BLUE_SLOW:
        return (led_color_t){.blue = elapsed_ms % 1000 < 500 ? brightness : 0};
    case LED_PATTERN_ORANGE_BLINK:
        return elapsed_ms % 200 < 100 ? (led_color_t){brightness, 8, 0} : (led_color_t){0};
    case LED_PATTERN_ORANGE:
        return (led_color_t){brightness, 8, 0};
    case LED_PATTERN_TURQUOISE_BLINK:
        return elapsed_ms % 1000 < 500 ? (led_color_t){0, brightness, 18} : (led_color_t){0};
    case LED_PATTERN_TURQUOISE:
        return (led_color_t){0, brightness, 18};
    case LED_PATTERN_GREEN:
        return (led_color_t){.green = brightness};
    default:
        return (led_color_t){.red = brightness};
    }
}

#include "status_led_pattern.h"
#include <assert.h>
#include <stdio.h>

static void expect(led_pattern_t pattern, uint32_t time, bool red, bool green, bool blue)
{
    led_color_t color = status_led_color(pattern, time);
    assert((color.red > 0) == red);
    assert((color.green > 0) == green);
    assert((color.blue > 0) == blue);
}

int main(void)
{
    assert(status_led_resolve(LED_LINK_CONNECTED, LED_LINK_SCANNING, LED_LINK_IDLE, true) == LED_PATTERN_ORANGE_BLINK);
    assert(status_led_resolve(LED_LINK_SCANNING, LED_LINK_CONNECTED, LED_LINK_IDLE, true) == LED_PATTERN_ORANGE);
    assert(status_led_resolve(LED_LINK_CONNECTED, LED_LINK_CONNECTED, LED_LINK_AUTHENTICATING, true) == LED_PATTERN_TURQUOISE_BLINK);
    assert(status_led_resolve(LED_LINK_SCANNING, LED_LINK_CONNECTED, LED_LINK_CONNECTED, true) == LED_PATTERN_TURQUOISE);
    assert(status_led_resolve(LED_LINK_IDLE, LED_LINK_CONNECTED, LED_LINK_CONNECTED, false) == LED_PATTERN_TURQUOISE);
    assert(status_led_resolve(LED_LINK_CONNECTED, LED_LINK_IDLE, LED_LINK_CONNECTED, true) == LED_PATTERN_GREEN);
    assert(status_led_resolve(LED_LINK_SCANNING, LED_LINK_IDLE, LED_LINK_IDLE, true) == LED_PATTERN_BLUE_FAST);
    assert(status_led_resolve(LED_LINK_IDLE, LED_LINK_IDLE, LED_LINK_IDLE, false) == LED_PATTERN_RED);
    assert(status_led_pattern(LED_LINK_IDLE, false) == LED_PATTERN_RED);
    assert(status_led_pattern(LED_LINK_SCANNING, true) == LED_PATTERN_BLUE_FAST);
    assert(status_led_pattern(LED_LINK_AUTHENTICATING, true) == LED_PATTERN_BLUE_SLOW);
    assert(status_led_pattern(LED_LINK_CONNECTED, true) == LED_PATTERN_GREEN);
    assert(status_led_pattern(LED_LINK_CONNECTED, false) == LED_PATTERN_RED);
    expect(LED_PATTERN_RED, 0, true, false, false);
    expect(LED_PATTERN_RED, 5000, true, false, false);
    expect(LED_PATTERN_GREEN, 0, false, true, false);
    expect(LED_PATTERN_GREEN, 5000, false, true, false);
    expect(LED_PATTERN_BLUE_FAST, 0, false, false, true);
    expect(LED_PATTERN_BLUE_FAST, 99, false, false, true);
    expect(LED_PATTERN_BLUE_FAST, 100, false, false, false);
    expect(LED_PATTERN_BLUE_FAST, 199, false, false, false);
    expect(LED_PATTERN_BLUE_FAST, 200, false, false, true);
    expect(LED_PATTERN_BLUE_SLOW, 499, false, false, true);
    expect(LED_PATTERN_BLUE_SLOW, 500, false, false, false);
    expect(LED_PATTERN_BLUE_SLOW, 999, false, false, false);
    expect(LED_PATTERN_BLUE_SLOW, 1000, false, false, true);
    expect(LED_PATTERN_ORANGE_BLINK, 99, true, true, false);
    expect(LED_PATTERN_ORANGE_BLINK, 100, false, false, false);
    expect(LED_PATTERN_ORANGE_BLINK, 200, true, true, false);
    expect(LED_PATTERN_ORANGE, 900, true, true, false);
    expect(LED_PATTERN_TURQUOISE_BLINK, 499, false, true, true);
    expect(LED_PATTERN_TURQUOISE_BLINK, 500, false, false, false);
    expect(LED_PATTERN_TURQUOISE_BLINK, 1000, false, true, true);
    expect(LED_PATTERN_TURQUOISE, 900, false, true, true);
    led_color_t orange = status_led_color(LED_PATTERN_ORANGE, 0);
    assert(orange.red > orange.green && !orange.blue);
    led_color_t turquoise = status_led_color(LED_PATTERN_TURQUOISE, 0);
    assert(!turquoise.red && turquoise.green && turquoise.blue);
    puts("status LED state and timing tests passed");
}

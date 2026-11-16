#include "status_led.h"
#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "led_strip.h"

static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static led_link_state_t link_state = LED_LINK_IDLE;
static led_link_state_t network_state = LED_LINK_IDLE, wifi_state = LED_LINK_IDLE;
static bool usb_ready;
static led_strip_handle_t strip;

void status_led_set_link(led_link_state_t state)
{
    portENTER_CRITICAL(&state_lock);
    link_state = state;
    portEXIT_CRITICAL(&state_lock);
}

void status_led_set_usb(bool ready)
{
    portENTER_CRITICAL(&state_lock);
    usb_ready = ready;
    portEXIT_CRITICAL(&state_lock);
}
void status_led_set_network(led_link_state_t state)
{
    portENTER_CRITICAL(&state_lock); network_state = state; portEXIT_CRITICAL(&state_lock);
}
void status_led_set_wifi(led_link_state_t state)
{
    portENTER_CRITICAL(&state_lock); wifi_state = state; portEXIT_CRITICAL(&state_lock);
}

static void led_task(void *arg)
{
    led_pattern_t previous = -1;
    led_color_t sent = {255, 255, 255};
    uint32_t started = 0;
    static const char *names[] = {"red", "blue fast", "blue slow", "green",
        "orange fast", "orange", "turquoise slow", "turquoise"};
    for (;;) {
        portENTER_CRITICAL(&state_lock);
        led_pattern_t pattern = status_led_resolve(link_state, wifi_state, network_state, usb_ready);
        portEXIT_CRITICAL(&state_lock);
        uint32_t now = esp_timer_get_time() / 1000;
        if (pattern != previous) {
            started = now;
            previous = pattern;
            ESP_LOGI("status_led", "Indicator: %s", names[pattern]);
        }
        led_color_t color = status_led_color(pattern, now - started);
        if (color.red != sent.red || color.green != sent.green || color.blue != sent.blue) {
            ESP_ERROR_CHECK(led_strip_set_pixel(strip, 0, color.red, color.green, color.blue));
            ESP_ERROR_CHECK(led_strip_refresh(strip));
            sent = color;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void status_led_init(void)
{
    led_strip_config_t config = {
        .strip_gpio_num = BOARD_STATUS_LED_GPIO, .max_leds = 1, .led_model = LED_MODEL_WS2812,
        // This board's LED uses RGB order; GRB swaps red and green physically.
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB,
    };
    led_strip_rmt_config_t rmt = {
        .resolution_hz = 10000000, .mem_block_symbols = 64, .flags.with_dma = true,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&config, &rmt, &strip));
    ESP_ERROR_CHECK(xTaskCreate(led_task, "status_led", 3072, NULL, 1, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

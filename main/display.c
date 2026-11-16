#include "display.h"
#include "board.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "accelerometer.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"

#define STRIP_ROWS 16
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static screen_status_t status;
static unsigned revision;
static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t transfer_done;
static uint16_t *strip;
static orientation_t orientation = ORIENTATION_PORTRAIT;
static int orientation_mode = DISPLAY_ORIENTATION_AUTO;
static bool imu_available;
static nvs_handle_t display_settings;

void display_set_orientation(int mode)
{
    if (mode < 0 || mode > DISPLAY_ORIENTATION_AUTO) return;
    portENTER_CRITICAL(&state_lock);
    orientation_mode = mode;
    orientation = mode == DISPLAY_ORIENTATION_AUTO ? ORIENTATION_PORTRAIT : (orientation_t)mode;
    revision++;
    portEXIT_CRITICAL(&state_lock);
    ESP_ERROR_CHECK(nvs_set_u8(display_settings, "orientation", mode));
    ESP_ERROR_CHECK(nvs_commit(display_settings));
}
orientation_t display_get_orientation(void)
{
    portENTER_CRITICAL(&state_lock);
    orientation_t result = orientation;
    portEXIT_CRITICAL(&state_lock);
    return result;
}
void display_rotate(void)
{
    display_set_orientation((display_get_orientation() + 1) % ORIENTATION_COUNT);
}
bool display_accelerometer_available(void)
{
    portENTER_CRITICAL(&state_lock);
    bool available = imu_available;
    portEXIT_CRITICAL(&state_lock);
    return available;
}

void display_status(screen_state_t state, const char *detail)
{
    portENTER_CRITICAL(&state_lock);
    status.state = state;
    status.pin = 0;
    snprintf(status.detail, sizeof(status.detail), "%s", detail ? detail : "");
    revision++;
    portEXIT_CRITICAL(&state_lock);
}
void display_pairing_pin(uint32_t pin)
{
    portENTER_CRITICAL(&state_lock);
    status.state = SCREEN_PAIRING;
    status.pin = pin;
    revision++;
    portEXIT_CRITICAL(&state_lock);
}
void display_auth_complete(bool success)
{
    portENTER_CRITICAL(&state_lock);
    if (!success || status.state == SCREEN_PAIRING) {
        status.state = success ? SCREEN_CONNECTING : SCREEN_AUTH_FAILED;
        status.pin = 0;
        snprintf(status.detail, sizeof(status.detail), "%s", success ? "SECURING LINK" : "PAIRING REJECTED");
        revision++;
    }
    portEXIT_CRITICAL(&state_lock);
}
void display_usb(bool connected)
{
    portENTER_CRITICAL(&state_lock);
    if (status.usb_connected != connected) { status.usb_connected = connected; revision++; }
    portEXIT_CRITICAL(&state_lock);
}
void display_network(const char *message)
{
    char value[25]; snprintf(value, sizeof(value), "%s", message ? message : "");
    portENTER_CRITICAL(&state_lock);
    if (strcmp(status.network, value)) { strcpy(status.network, value); revision++; }
    portEXIT_CRITICAL(&state_lock);
}
static bool transfer_complete(esp_lcd_panel_io_handle_t io,
    esp_lcd_panel_io_event_data_t *event, void *arg)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(transfer_done, &woken);
    return woken == pdTRUE;
}
static void display_task(void *arg)
{
    unsigned drawn = ~0u;
    orientation_t applied = ORIENTATION_COUNT;
    orientation_tracker_t tracker = { .current = ORIENTATION_PORTRAIT };
    bool probed = false, sensor_ok = false;
    uint32_t last_sample = 0;
    for (;;) {
        uint32_t now = esp_timer_get_time() / 1000;
        if (!probed && now >= 2000) {
            sensor_ok = accelerometer_init();
            probed = true;
            last_sample = now;
            portENTER_CRITICAL(&state_lock);
            imu_available = sensor_ok;
            portEXIT_CRITICAL(&state_lock);
            ESP_LOGI("display", "Accelerometer %s", sensor_ok ? "available" : "disabled or absent; auto uses portrait");
        }
        acceleration_t sample;
        acceleration_result_t result = sensor_ok ? accelerometer_read(&sample) : ACCELEROMETER_WAIT;
        if (result == ACCELEROMETER_SAMPLE) last_sample = now;
        if (sensor_ok && (uint32_t)(now - last_sample) >= 2000) {
            sensor_ok = false;
            tracker.tracking = false;
            portENTER_CRITICAL(&state_lock);
            imu_available = false;
            if (orientation_mode == DISPLAY_ORIENTATION_AUTO) {
                orientation = ORIENTATION_PORTRAIT;
                revision++;
            }
            portEXIT_CRITICAL(&state_lock);
            ESP_LOGW("display", "Accelerometer stopped responding; auto uses portrait");
        }
        screen_status_t snapshot;
        portENTER_CRITICAL(&state_lock);
        if (orientation_mode == DISPLAY_ORIENTATION_AUTO && result == ACCELEROMETER_SAMPLE) {
            if (tracker.current != orientation) {
                tracker.current = orientation;
                tracker.tracking = false;
            }
            if (orientation_sample(&tracker, sample.x, sample.y, sample.z, now)) {
                orientation = tracker.current;
                revision++;
            }
        } else if (orientation_mode != DISPLAY_ORIENTATION_AUTO || result != ACCELEROMETER_WAIT) {
            tracker.tracking = false;
        }
        orientation_t rotation = orientation;
        unsigned current = revision;
        snapshot = status;
        portEXIT_CRITICAL(&state_lock);
        if (current != drawn) {
            display_geometry_t geometry = orientation_geometry(rotation);
            if (rotation != applied) {
                gpio_set_level(BOARD_LCD_BL, 0);
                ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, geometry.swap_xy));
                ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, geometry.mirror_x, geometry.mirror_y));
                ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel, geometry.x_gap, geometry.y_gap));
                applied = rotation;
                ESP_LOGI("display", "Orientation %s (%dx%d)", orientation_name(rotation), geometry.width, geometry.height);
            }
            for (int y = 0; y < geometry.height; y += STRIP_ROWS) {
                int rows = geometry.height - y;
                if (rows > STRIP_ROWS) rows = STRIP_ROWS;
                screen_render_strip(strip, y, rows, &geometry, &snapshot);
                ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, y, geometry.width, y + rows, strip));
                ESP_ERROR_CHECK(xSemaphoreTake(transfer_done, pdMS_TO_TICKS(1000)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT);
            }
            drawn = current;
            gpio_set_level(BOARD_LCD_BL, 1);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
void display_init(void)
{
    ESP_ERROR_CHECK(nvs_open("display", NVS_READWRITE, &display_settings));
    uint8_t saved_mode = DISPLAY_ORIENTATION_AUTO;
    if (nvs_get_u8(display_settings, "orientation", &saved_mode) == ESP_OK && saved_mode <= DISPLAY_ORIENTATION_AUTO) {
        orientation_mode = saved_mode;
        if (saved_mode != DISPLAY_ORIENTATION_AUTO) orientation = saved_mode;
    }
    gpio_config_t output = { .pin_bit_mask = 1ULL << BOARD_LCD_BL, .mode = GPIO_MODE_OUTPUT };
    ESP_ERROR_CHECK(gpio_config(&output));
    gpio_set_level(BOARD_LCD_BL, 0);
    spi_bus_config_t bus = {
        .mosi_io_num = BOARD_LCD_MOSI, .miso_io_num = -1, .sclk_io_num = BOARD_LCD_CLK,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = 320 * STRIP_ROWS * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    transfer_done = xSemaphoreCreateBinary();
    strip = heap_caps_malloc(320 * STRIP_ROWS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(transfer_done && strip);
    esp_lcd_panel_io_spi_config_t io = {
        .cs_gpio_num = BOARD_LCD_CS, .dc_gpio_num = BOARD_LCD_DC, .spi_mode = 0,
        .pclk_hz = 40000000, .trans_queue_depth = 1,
        .on_color_trans_done = transfer_complete, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
    };
    esp_lcd_panel_io_handle_t handle;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io, &handle));
    esp_lcd_panel_dev_config_t config = {
        .reset_gpio_num = BOARD_LCD_RST, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(handle, &config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel, 34, 0));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    ESP_ERROR_CHECK(xTaskCreate(display_task, "display", 4096, NULL, 2, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

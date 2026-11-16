#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdatomic.h>
#include "device/dcd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_cdc_acm.h"
#include "class/hid/hid_device.h"
#include "esp_hidh.h"
#include "esp_hid_gap.h"
#include "keyboard_layout.h"
#include "display.h"
#include "board.h"
#include "status_led.h"
#include "input_bridge.h"
#include "mouse_report.h"
#include "diagnostic_log.h"
#include "config_storage.h"
#include "wifi_manager.h"
#include "deskflow_client.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_private/usb_phy.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

static const char *TAG = "bridge";
static QueueHandle_t reports;
static QueueHandle_t mouse_reports;
static SemaphoreHandle_t source_lock;
static bool network_focused;
static uint8_t ble_report[8], network_report[8];
static bridge_mouse_t last_mouse = {.id = 2};
typedef struct { char text[256]; } log_line_t;
static QueueHandle_t console_logs;
static EventGroupHandle_t link_events;
#define LINK_DONE BIT0
#define LINK_UP BIT1
typedef struct { uint8_t bytes[8]; } keyboard_report_t;
static nvs_handle_t settings;
static uint8_t peer[6];
static bool have_peer;
static uint8_t supported[256];
static uint8_t keyboard_map;
static portMUX_TYPE report_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t current_report[8];
static TaskHandle_t usb_task_handle, log_task_handle;

static void force_download_on_restart(void)
{
    REG_SET_BIT(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
}

static void enter_bootloader(void)
{
    status_led_set_link(LED_LINK_IDLE);
    display_status(SCREEN_STARTING, "ENTERING FLASH MODE");
    ESP_LOGI(TAG, "Switching to ROM USB Serial/JTAG bootloader");
    keyboard_report_t release = {0};
    xQueueReset(reports);
    xQueueSend(reports, &release, 0);
    vTaskDelay(pdMS_TO_TICKS(300));
    vTaskSuspend(usb_task_handle);
    vTaskSuspend(log_task_handle);
    ESP_ERROR_CHECK(tinyusb_driver_uninstall());
    // RTC USB routing survives a CPU reset: explicitly return the internal
    // PHY to Serial/JTAG before asking the ROM to enter download mode.
    usb_phy_config_t phy = {
        .controller = USB_PHY_CTRL_SERIAL_JTAG, .target = USB_PHY_TARGET_INT,
    };
    usb_phy_handle_t handle;
    ESP_ERROR_CHECK(usb_new_phy(&phy, &handle));
    ESP_ERROR_CHECK(esp_register_shutdown_handler(force_download_on_restart));
    esp_restart();
}

static int console_log(const char *format, va_list args)
{
    log_line_t line;
    int n = vsnprintf(line.text, sizeof(line.text), format, args);
    if (n > 0) {
        diagnostic_log_capture(line.text);
        xQueueSend(console_logs, &line, 0);
    }
    return n;
}

static void log_task(void *arg)
{
    log_line_t line;
    for (;;) {
        if (xQueueReceive(console_logs, &line, portMAX_DELAY) != pdTRUE) continue;
        size_t sent = 0, length = strlen(line.text);
        while (sent < length && tud_cdc_connected()) {
            sent += tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0,
                (const uint8_t *)line.text + sent, length - sent);
            tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
            vTaskDelay(1);
        }
    }
}

static atomic_uint usb_resets, usb_setups;
void tud_event_hook_cb(uint8_t rhport, uint32_t event, bool in_isr)
{
    if (event == DCD_EVENT_BUS_RESET) atomic_fetch_add(&usb_resets, 1);
    if (event == DCD_EVENT_SETUP_RECEIVED) atomic_fetch_add(&usb_setups, 1);
}

static const uint8_t report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(1)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(3)),
};
static const uint8_t config_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 4, 0,
        TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_DESC_LEN + TUD_MSC_DESC_LEN, 0, 250),
    TUD_CDC_DESCRIPTOR(0, 4, 0x81, 8, 0x02, 0x82, 64),
    TUD_HID_DESCRIPTOR(2, 5, HID_ITF_PROTOCOL_NONE,
        sizeof(report_descriptor), 0x83, 16, 1),
    TUD_MSC_DESCRIPTOR(3, 7, 0x04, 0x84, 64),
};
static char serial[13];
static const char *strings[] = {
    (const char[]){0x09, 0x04}, "ESP Bridge", "ESP Input Bridge", serial,
    "Bridge console", "Keyboard", "Mouse", "Configuration storage"
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    return report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t id,
    hid_report_type_t type, uint8_t *buffer, uint16_t length)
{
    if (instance || type != HID_REPORT_TYPE_INPUT) return 0;
    if (id == 3) {
        uint16_t n = length < 5 ? length : 5;
        memset(buffer, 0, n);
        return n;
    }
    if (id != 1 && id != 0) return 0;
    uint16_t n = length < 8 ? length : 8;
    portENTER_CRITICAL(&report_lock);
    memcpy(buffer, current_report, n);
    portEXIT_CRITICAL(&report_lock);
    return n;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t id,
    hid_report_type_t type, uint8_t const *buffer, uint16_t length)
{
    // Host LED output (Caps/Num/Scroll Lock) is currently not forwarded.
}

static void queue_keyboard(const uint8_t *data)
{
    keyboard_report_t r = {0};
    if (data) memcpy(r.bytes, data, sizeof(r.bytes));
    if (xQueueSend(reports, &r, 0) != pdTRUE) {
        // Preserve a release even if a slow/suspended USB host overflows the queue.
        xQueueReset(reports);
        keyboard_report_t release = {0};
        xQueueSend(reports, &release, 0);
        xQueueSend(reports, &r, 0);
        ESP_LOGW(TAG, "USB queue overflow; released keys");
    }
}

static void submit(const uint8_t *data)
{
    xSemaphoreTake(source_lock, portMAX_DELAY);
    if (data) memcpy(ble_report, data, 8); else memset(ble_report, 0, 8);
    if (!network_focused) queue_keyboard(ble_report);
    xSemaphoreGive(source_lock);
}
void input_bridge_network_keyboard(const uint8_t report[8])
{
    xSemaphoreTake(source_lock, portMAX_DELAY);
    memcpy(network_report, report, 8);
    if (network_focused) queue_keyboard(report);
    xSemaphoreGive(source_lock);
}
static void queue_mouse(const bridge_mouse_t *report)
{
    if (xQueueSend(mouse_reports, report, 0) != pdTRUE) {
        xQueueReset(mouse_reports);
        bridge_mouse_t release = *report; release.buttons = 0; release.wheel = release.pan = 0;
        if (release.id == 2) release.x = release.y = 0;
        xQueueSend(mouse_reports, &release, 0);
        xQueueSend(mouse_reports, report, 0);
        ESP_LOGW(TAG, "Mouse queue overflow; released buttons");
    }
    last_mouse = *report;
}
void input_bridge_network_mouse(const bridge_mouse_t *report)
{
    xSemaphoreTake(source_lock, portMAX_DELAY);
    if (network_focused) queue_mouse(report);
    xSemaphoreGive(source_lock);
}
void input_bridge_network_focus(bool active)
{
    xSemaphoreTake(source_lock, portMAX_DELAY);
    if (network_focused != active) {
        network_focused = active;
        xQueueReset(reports); queue_keyboard(NULL);
        queue_keyboard(active ? network_report : ble_report);
        xQueueReset(mouse_reports);
        bridge_mouse_t release = last_mouse; release.buttons = 0; release.wheel = release.pan = 0;
        if (release.id == 2) release.x = release.y = 0;
        queue_mouse(&release);
    }
    xSemaphoreGive(source_lock);
}
void input_bridge_network_ready(bool ready)
{
    if (!ready) input_bridge_network_focus(false);
    status_led_set_network(ready ? LED_LINK_CONNECTED : LED_LINK_IDLE);
}

static void hid_event(void *arg, esp_event_base_t base, int32_t event, void *data)
{
    esp_hidh_event_data_t *p = data;
    switch (event) {
    case ESP_HIDH_OPEN_EVENT:
        if (p->open.status == ESP_OK) {
            memset(supported, 0, sizeof(supported));
            esp_hid_raw_report_map_t *maps = NULL;
            size_t map_count = 0;
            if (esp_hidh_dev_report_maps_get(p->open.dev, &map_count, &maps) == ESP_OK) {
                // Choose a single keyboard map to avoid conflicting key states.
                for (size_t m = 0; m < map_count; m++) {
                    bool found = false;
                    for (unsigned id = 0; id < 256; id++) {
                        supported[id] = keyboard_layout_size(maps[m].data, maps[m].len, id);
                        found |= supported[id];
                    }
                    if (found) { keyboard_map = m; break; }
                }
            }
            bool usable = false;
            for (unsigned id = 0; id < 256; id++) usable |= supported[id];
            if (!usable) {
                ESP_LOGE(TAG, "No supported six-key keyboard layout; reports will be ignored");
                for (size_t m = 0; m < map_count; m++)
                    ESP_LOG_BUFFER_HEX_LEVEL(TAG, maps[m].data, maps[m].len, ESP_LOG_INFO);
            }
            display_status(usable ? SCREEN_CONNECTED : SCREEN_UNSUPPORTED,
                usable ? esp_hidh_dev_name_get(p->open.dev) : "KEYBOARD REPORT FORMAT");
            status_led_set_link(usable ? LED_LINK_CONNECTED : LED_LINK_IDLE);
            const uint8_t *addr = esp_hidh_dev_bda_get(p->open.dev);
            if (usable) {
                memcpy(peer, addr, 6);
                have_peer = true;
                ESP_ERROR_CHECK(nvs_set_blob(settings, "peer", peer, 6));
                ESP_ERROR_CHECK(nvs_commit(settings));
            }
            ESP_LOGI(TAG, "Keyboard connected: %s", esp_hidh_dev_name_get(p->open.dev));
            xEventGroupSetBits(link_events, LINK_UP | LINK_DONE);
        } else {
            status_led_set_link(LED_LINK_IDLE);
            display_status(SCREEN_AUTH_FAILED, "CONNECTION FAILED");
            ESP_LOGW(TAG, "Connection failed: %s", esp_err_to_name(p->open.status));
            xEventGroupSetBits(link_events, LINK_DONE);
        }
        break;
    case ESP_HIDH_INPUT_EVENT:
        // BLE Report characteristic payload excludes its report ID.
        // Six-key layouts with or without a reserved byte normalize to USB.
        if (p->input.usage == ESP_HID_USAGE_KEYBOARD
            && p->input.map_index == keyboard_map && p->input.report_id < 256
            && supported[p->input.report_id] && p->input.length == supported[p->input.report_id]) {
            keyboard_report_t report = {0};
            report.bytes[0] = p->input.data[0];
            if (p->input.length == 8 && p->input.data[1] != 0) break;
            memcpy(report.bytes + 2, p->input.data + (p->input.length == 7 ? 1 : 2), 6);
            submit(report.bytes);
        } else {
            ESP_LOGW(TAG, "Unsupported report: usage=%u id=%u length=%u",
                p->input.usage, p->input.report_id, p->input.length);
        }
        break;
    case ESP_HIDH_CLOSE_EVENT:
        status_led_set_link(LED_LINK_IDLE);
        display_status(SCREEN_SCANNING, "KEYBOARD DISCONNECTED");
        submit(NULL);
        xEventGroupClearBits(link_events, LINK_UP);
        xEventGroupSetBits(link_events, LINK_DONE);
        ESP_LOGI(TAG, "Keyboard disconnected; keys released");
        break;
    default:
        break;
    }
}

static void scan_task(void *arg)
{
    for (;;) {
        if (xEventGroupGetBits(link_events) & LINK_UP) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        size_t count = 0;
        esp_hid_scan_result_t *results = NULL;
        ESP_LOGI(TAG, "Scanning for BLE keyboard (put keyboard in pairing mode)");
        display_status(SCREEN_SCANNING, have_peer ? "LOOKING FOR SAVED DEVICE" : "BLE KEYBOARDS ONLY");
        status_led_set_link(LED_LINK_SCANNING);
        esp_hid_gap_set_target(have_peer ? peer : NULL);
        esp_err_t err = esp_hid_scan(5, &count, &results);
        status_led_set_link(LED_LINK_IDLE);
        if (err != ESP_OK) ESP_LOGW(TAG, "Scan failed: %s", esp_err_to_name(err));
        esp_hid_scan_result_t *chosen = NULL;
        for (esp_hid_scan_result_t *r = results; r; r = r->next) {
            ESP_LOGI(TAG, "Found %s, usage=%s", r->name ? r->name : "(unnamed)",
                esp_hid_usage_str(r->usage));
            if (r->transport != ESP_HID_TRANSPORT_BLE) continue;
            if (have_peer) {
                if (memcmp(r->bda, peer, 6) == 0) chosen = r;
            } else if (CONFIG_BRIDGE_KEYBOARD_NAME[0] ?
                (r->name && strcmp(r->name, CONFIG_BRIDGE_KEYBOARD_NAME) == 0) :
                (r->usage == ESP_HID_USAGE_KEYBOARD || r->usage == ESP_HID_USAGE_GENERIC)) {
                if (!chosen || r->rssi > chosen->rssi) chosen = r;
            }
        }
        if (chosen) {
            status_led_set_link(LED_LINK_AUTHENTICATING);
            display_status(SCREEN_CONNECTING, chosen->name);
            uint8_t addr[6];
            memcpy(addr, chosen->bda, 6);
            uint8_t addr_type = chosen->ble.addr_type;
            esp_hid_scan_results_free(results);
            results = NULL;
            xEventGroupClearBits(link_events, LINK_DONE);
            if (esp_hidh_dev_open(addr, ESP_HID_TRANSPORT_BLE, addr_type)) {
                // No overlapping connection attempts. Stack reports completion/failure.
                xEventGroupWaitBits(link_events, LINK_DONE, pdTRUE, pdFALSE, portMAX_DELAY);
            } else {
                status_led_set_link(LED_LINK_IDLE);
            }
        }
        esp_hid_scan_results_free(results);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void usb_task(void *arg)
{
    keyboard_report_t pending = {0};
    bool dirty = true;
    bool was_mounted = false;
    bridge_mouse_t pending_mouse = {0};
    bool mouse_dirty = false;
    int previous_state = -1;
    unsigned last_resets = 0, last_setups = 0;
    uint32_t last_enumeration_log = 0;
    for (;;) {
        bool mounted = tud_mounted() && !tud_suspended();
        int usb_state = (tud_mounted() ? 1 : 0) | (tud_suspended() ? 2 : 0) | (tud_hid_n_get_protocol(0) << 2);
        if (usb_state != previous_state) {
            ESP_LOGI(TAG, "USB state mounted=%u suspended=%u protocol=%u", tud_mounted(), tud_suspended(), tud_hid_n_get_protocol(0));
            previous_state = usb_state;
        }
        unsigned resets = atomic_load(&usb_resets), setups = atomic_load(&usb_setups);
        uint32_t now_ms = esp_timer_get_time()/1000;
        if (!mounted && (resets != last_resets || setups != last_setups) && now_ms - last_enumeration_log >= 2000) {
            ESP_LOGI(TAG, "USB enumeration resets=%u setup_requests=%u", resets, setups);
            last_resets = resets; last_setups = setups; last_enumeration_log = now_ms;
        }
        display_usb(mounted);
        status_led_set_usb(mounted);
        if (!mounted) {
            // Never replay buffered keystrokes when a PC reconnects.
            xQueueReset(reports);
            xQueueReset(mouse_reports);
            mouse_dirty = false;
            memset(&pending, 0, sizeof(pending));
            portENTER_CRITICAL(&report_lock);
            memset(current_report, 0, sizeof(current_report));
            portEXIT_CRITICAL(&report_lock);
            dirty = true;
        } else {
            if (!was_mounted) dirty = true;
            if (dirty && tud_hid_n_ready(0)) {
                if (tud_hid_n_report(0, tud_hid_n_get_protocol(0) == HID_PROTOCOL_BOOT ? 0 : 1, pending.bytes, 8)) {
                    portENTER_CRITICAL(&report_lock);
                    memcpy(current_report, pending.bytes, 8);
                    portEXIT_CRITICAL(&report_lock);
                    dirty = false;
                }
            }
            if (!dirty && xQueueReceive(reports, &pending, 0) == pdTRUE) dirty = true;
            if (!dirty && tud_hid_n_get_protocol(0) != HID_PROTOCOL_BOOT && tud_hid_n_ready(0)) {
                if (!mouse_dirty) mouse_dirty = xQueueReceive(mouse_reports, &pending_mouse, 0) == pdTRUE;
                if (mouse_dirty) {
                    bridge_mouse_t remaining = pending_mouse;
                    uint8_t data[5];
                    bool more = mouse_report_next(&remaining, data);
                    if (tud_hid_n_report(0, 3, data, sizeof(data))) {
                        pending_mouse = remaining; mouse_dirty = more;
                        if ((int8_t)data[1] || (int8_t)data[2]) {
                            static uint32_t sent, last_log;
                            uint32_t now = esp_timer_get_time()/1000;
                            if (!sent++ || now - last_log >= 2000) {
                                ESP_LOGI(TAG, "USB pointer sent=%lu dx=%d dy=%d protocol=%u",
                                    (unsigned long)sent, (int8_t)data[1], (int8_t)data[2], tud_hid_n_get_protocol(0));
                                last_log = now;
                            }
                        }
                    }
                }
            }
        }
        was_mounted = mounted;
        // One full scheduler tick: pdMS_TO_TICKS(1) is zero at IDF's 100 Hz
        // default and would starve the BLE, console and display tasks.
        vTaskDelay(1);
    }
}

// An explicit diagnostic independent of Deskflow coordinates and focus.
static void mouse_test_task(void *arg)
{
    while (!tud_mounted() || tud_suspended()) vTaskDelay(pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(5000));
    display_network("USB MOUSE TEST");
    ESP_LOGI(TAG, "USB mouse test: 80-pixel square, no clicks");
    static const int8_t directions[4][2] = {{8,0},{0,8},{-8,0},{0,-8}};
    for (unsigned side = 0; side < 4 && tud_mounted(); side++) {
        for (unsigned step = 0; step < 10 && tud_mounted(); step++) {
            bridge_mouse_t report = {.id=2, .x=directions[side][0], .y=directions[side][1]};
            xSemaphoreTake(source_lock, portMAX_DELAY);
            queue_mouse(&report);
            xSemaphoreGive(source_lock);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
    display_network("USB TEST COMPLETE");
    ESP_LOGI(TAG, "USB mouse test complete");
    vTaskDelete(NULL);
}

static void console_task(void *arg)
{
    char command[32];
    size_t used = 0;
    bool overflow = false;
    TickType_t pressed_since = 0;
    for (;;) {
        char c;
        size_t received = 0;
        bool pressed = gpio_get_level(GPIO_NUM_0) == 0;
        if (!pressed && pressed_since) {
            if (xTaskGetTickCount() - pressed_since >= pdMS_TO_TICKS(50)) display_rotate();
            pressed_since = 0;
        }
        else if (pressed && !pressed_since) pressed_since = xTaskGetTickCount();
        if (pressed_since && xTaskGetTickCount() - pressed_since >= pdMS_TO_TICKS(3000)) {
            strcpy(command, "forget");
            used = 6;
            c = '\n';
            received = 1;
        } else {
            tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, (uint8_t *)&c, 1, &received);
        }
        if (received != 1) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (c == '\r' || c == '\n') {
            command[used] = 0;
            used = 0;
            if (overflow) { overflow = false; continue; }
            if (strncmp(command, "orientation ", 12) == 0) {
                const char *mode = command + 12;
                if (strcmp(mode, "auto") == 0) display_set_orientation(DISPLAY_ORIENTATION_AUTO);
                else {
                    for (int i = 0; i < ORIENTATION_COUNT; i++)
                        if (strcmp(mode, orientation_name(i)) == 0) display_set_orientation(i);
                }
                ESP_LOGI(TAG, "Orientation %s; accelerometer %s", orientation_name(display_get_orientation()),
                    display_accelerometer_available() ? "available" : "absent/disabled");
            } else if (strcmp(command, "reload") == 0) {
                config_storage_reload();
                ESP_LOGI(TAG, "Configuration reload requested");
            } else if (strcmp(command, "network") == 0) {
                wifi_manager_status(); deskflow_client_status();
            } else if (strcmp(command, "trace on") == 0) {
                diagnostic_log_enable(true);
            } else if (strcmp(command, "trace off") == 0) {
                diagnostic_log_enable(false);
            } else if (strcmp(command, "trace clear") == 0) {
                diagnostic_log_clear();
            } else if (strcmp(command, "trace dump") == 0) {
                diagnostic_log_dump();
            } else if (strcmp(command, "mouse-test") == 0) {
                ESP_ERROR_CHECK(xTaskCreate(mouse_test_task, "mouse_test", 3072, NULL, 1, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
            } else if (strcmp(command, "mouse-test-next") == 0) {
                ESP_ERROR_CHECK(nvs_set_u8(settings, "mouse_test", 1));
                ESP_ERROR_CHECK(nvs_commit(settings));
                ESP_LOGI(TAG, "USB mouse test armed for the next boot; starts five seconds after USB connects");
            } else if (strcmp(command, "restart") == 0) {
                ESP_LOGI(TAG, "Restart requested");
                vTaskDelay(pdMS_TO_TICKS(100));
                esp_restart();
            } else if (strcmp(command, "bootloader") == 0) {
                enter_bootloader();
            } else if (strcmp(command, "forget") == 0) {
                int count = esp_ble_get_bond_device_num();
                esp_ble_bond_dev_t *bonds = calloc(count, sizeof(*bonds));
                if (count && bonds) {
                    ESP_ERROR_CHECK(esp_ble_get_bond_device_list(&count, bonds));
                    for (int i = 0; i < count; i++)
                        ESP_ERROR_CHECK(esp_ble_remove_bond_device(bonds[i].bd_addr));
                }
                free(bonds);
                submit(NULL);
                esp_err_t err = nvs_erase_key(settings, "peer");
                if (err != ESP_ERR_NVS_NOT_FOUND) ESP_ERROR_CHECK(err);
                ESP_ERROR_CHECK(nvs_commit(settings));
                ESP_LOGI(TAG, "Forgot keyboard; restarting into pairing mode");
                status_led_set_link(LED_LINK_IDLE);
                display_status(SCREEN_STARTING, "KEYBOARD FORGOTTEN");
                vTaskDelay(pdMS_TO_TICKS(1500));
                esp_restart();
            } else {
                ESP_LOGI(TAG, "Board: %s", BOARD_NAME);
                ESP_LOGI(TAG, "Keyboard %s. Orientation %s; accelerometer %s. Commands: forget, bootloader, orientation <mode>",
                    (xEventGroupGetBits(link_events) & LINK_UP) ? "connected" : "disconnected",
                    orientation_name(display_get_orientation()), display_accelerometer_available() ? "available" : "absent/disabled");
                wifi_manager_status(); deskflow_client_status();
            }
        } else if (used < sizeof(command) - 1) {
            command[used++] = c;
        } else {
            overflow = true;
        }
    }
}

void app_main(void)
{
    status_led_init();
    gpio_config_t boot = {
        .pin_bit_mask = 1ULL << GPIO_NUM_0, .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&boot));
    ESP_ERROR_CHECK(nvs_flash_init());
    diagnostic_log_init();
    display_init();
    ESP_ERROR_CHECK(nvs_open("bridge", NVS_READWRITE, &settings));
    size_t n = sizeof(peer);
    have_peer = nvs_get_blob(settings, "peer", peer, &n) == ESP_OK && n == 6;
    reports = xQueueCreate(64, sizeof(keyboard_report_t));
    mouse_reports = xQueueCreate(128, sizeof(bridge_mouse_t));
    source_lock = xSemaphoreCreateMutex();
    console_logs = xQueueCreate(16, sizeof(log_line_t));
    link_events = xEventGroupCreate();
    assert(reports && mouse_reports && source_lock && link_events && console_logs);
    config_storage_init();

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf(serial, sizeof(serial), "%02X%02X%02X%02X%02X%02X",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    tinyusb_config_t usb = TINYUSB_DEFAULT_CONFIG();
    usb.descriptor.full_speed_config = config_descriptor;
    usb.descriptor.string = strings;
    usb.descriptor.string_count = sizeof(strings) / sizeof(strings[0]);
    ESP_ERROR_CHECK(tinyusb_driver_install(&usb));
    tinyusb_config_cdcacm_t cdc = {0};
    ESP_ERROR_CHECK(tinyusb_cdcacm_init(&cdc));
    ESP_ERROR_CHECK(xTaskCreate(log_task, "console_logs", 3072, NULL, 1, &log_task_handle) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    esp_log_set_vprintf(console_log);
    ESP_LOGI(TAG, "Board: %s; BLE/Deskflow -> USB input bridge", BOARD_NAME);
    ESP_ERROR_CHECK(esp_hid_gap_init(HID_HOST_MODE));
    uint8_t auth = ESP_LE_AUTH_REQ_SC_MITM_BOND;
    uint8_t io = ESP_IO_CAP_OUT;
    uint8_t key_size = 16;
    uint8_t keys = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    ESP_ERROR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth, 1));
    ESP_ERROR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &io, 1));
    ESP_ERROR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, 1));
    ESP_ERROR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &keys, 1));
    ESP_ERROR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &keys, 1));
    ESP_ERROR_CHECK(esp_ble_gattc_register_callback(esp_hidh_gattc_event_handler));
    esp_hidh_config_t config = { .callback = hid_event, .event_stack_size = 4096 };
    ESP_ERROR_CHECK(esp_hidh_init(&config));
    ESP_ERROR_CHECK(xTaskCreate(usb_task, "usb_reports", 3072, NULL, 4, &usb_task_handle) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(scan_task, "keyboard_scan", 6144, NULL, 2, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(console_task, "console", 4096, NULL, 1, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    wifi_manager_start();
    deskflow_client_start();
    uint8_t mouse_test = 0;
    if (nvs_get_u8(settings, "mouse_test", &mouse_test) == ESP_OK && mouse_test) {
        ESP_ERROR_CHECK(nvs_erase_key(settings, "mouse_test"));
        ESP_ERROR_CHECK(nvs_commit(settings));
        ESP_ERROR_CHECK(xTaskCreate(mouse_test_task, "mouse_test", 3072, NULL, 1, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    }
}

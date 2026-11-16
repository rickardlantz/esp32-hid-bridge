#include "wifi_manager.h"
#include "wifi_config.h"
#include "config_storage.h"
#include "display.h"
#include "status_led.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "wifi";
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static wifi_connection_t state;

void wifi_manager_connection(wifi_connection_t *connection)
{
    portENTER_CRITICAL(&state_lock); *connection = state; portEXIT_CRITICAL(&state_lock);
}
void wifi_manager_status(void)
{
    wifi_connection_t connection; wifi_manager_connection(&connection);
    ESP_LOGI(TAG, "Wi-Fi %s", connection.connected ? connection.address : "offline");
}
static void event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ip = data;
        portENTER_CRITICAL(&state_lock);
        state.connected = true; state.generation++;
        snprintf(state.address, sizeof(state.address), IPSTR, IP2STR(&ip->ip_info.ip));
        portEXIT_CRITICAL(&state_lock);
        status_led_set_wifi(LED_LINK_CONNECTED);
        display_network("WIFI CONNECTED");
        ESP_LOGI(TAG, "Wi-Fi address: " IPSTR, IP2STR(&ip->ip_info.ip));
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        portENTER_CRITICAL(&state_lock); state.connected = false; state.generation++; portEXIT_CRITICAL(&state_lock);
        status_led_set_wifi(LED_LINK_IDLE);
        display_network("WIFI DISCONNECTED");
    }
}
static bool same_credentials(const wifi_profile_t *a, const wifi_profile_t *b)
{
    return b->enabled && !strcmp(a->ssid, b->ssid) && !strcmp(a->password, b->password);
}
static bool still_configured(const wifi_profile_t *profile)
{
    wifi_profiles_t profiles; config_storage_wifi(&profiles);
    for (size_t i = 0; i < profiles.count; i++) if (same_credentials(profile, &profiles.profiles[i])) return true;
    return false;
}
static void wifi_task(void *arg)
{
    wifi_profiles_t *profiles = malloc(sizeof(*profiles)); assert(profiles);
    wifi_profile_t active = {0}; size_t next = 0;
    for (;;) {
        config_storage_wifi(profiles);
        wifi_connection_t connection; wifi_manager_connection(&connection);
        if (connection.connected && still_configured(&active)) {
            vTaskDelay(pdMS_TO_TICKS(250)); continue;
        }
        if (connection.connected) esp_wifi_disconnect();
        wifi_profile_t *chosen = NULL;
        for (size_t n = 0; n < profiles->count; n++) {
            size_t i = (next + n) % profiles->count;
            if (profiles->profiles[i].enabled) { chosen = &profiles->profiles[i]; next = (i+1) % profiles->count; break; }
        }
        if (!chosen) {
            display_network("WIFI NOT CONFIGURED"); status_led_set_wifi(LED_LINK_IDLE);
            vTaskDelay(pdMS_TO_TICKS(500)); continue;
        }
        wifi_config_t config = {0};
        memcpy(config.sta.ssid, chosen->ssid, strlen(chosen->ssid));
        memcpy(config.sta.password, chosen->password, strlen(chosen->password));
        config.sta.threshold.authmode = chosen->password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        config.sta.pmf_cfg.capable = true;
        active = *chosen;
        esp_wifi_disconnect();
        portENTER_CRITICAL(&state_lock); state.connected = false; strcpy(state.ssid, chosen->ssid); portEXIT_CRITICAL(&state_lock);
        display_network("WIFI CONNECTING"); status_led_set_wifi(LED_LINK_SCANNING);
        ESP_LOGI(TAG, "Trying Wi-Fi profile %u", (unsigned)(chosen - profiles->profiles + 1));
        esp_err_t error = esp_wifi_set_config(WIFI_IF_STA, &config);
        if (error == ESP_OK) error = esp_wifi_connect();
        uint32_t start = esp_timer_get_time()/1000;
        do {
            vTaskDelay(pdMS_TO_TICKS(100));
            wifi_manager_connection(&connection);
        } while (error == ESP_OK && !connection.connected &&
            (uint32_t)(esp_timer_get_time()/1000 - start) < 12000 && still_configured(chosen));
        status_led_set_wifi(connection.connected ? LED_LINK_CONNECTED : LED_LINK_IDLE);
        if (!connection.connected) {
            esp_wifi_disconnect();
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}
void wifi_manager_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_sta() ? ESP_OK : ESP_ERR_NO_MEM);
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event, NULL));
    ESP_ERROR_CHECK(esp_wifi_start()); ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(xTaskCreate(wifi_task, "wifi_profiles", 6144, NULL, 2, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

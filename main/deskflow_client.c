#include "deskflow_client.h"
#include "deskflow_protocol.h"
#include "deskflow_transport.h"
#include "config_storage.h"
#include "wifi_manager.h"
#include "display.h"
#include "status_led.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "deskflow";
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool ready, focused;
typedef struct {
    wifi_connection_t link;
    deskflow_config_t config;
} session_context_t;
static bool keep_running(void *arg)
{
    session_context_t *context = arg;
    wifi_connection_t link; wifi_manager_connection(&link);
    deskflow_config_t config;
    return link.connected && link.generation == context->link.generation &&
        !strcmp(link.ssid, context->link.ssid) && config_storage_deskflow(link.ssid, &config) &&
        !memcmp(&config, &context->config, sizeof(config));
}
static void keyboard_cb(void *arg, const uint8_t report[8]) { input_bridge_network_keyboard(report); }
static void mouse_cb(void *arg, const bridge_mouse_t *report) { input_bridge_network_mouse(report); }
static void focus_cb(void *arg, bool active)
{
    portENTER_CRITICAL(&state_lock); focused = active; portEXIT_CRITICAL(&state_lock);
    input_bridge_network_focus(active);
    display_network(active ? "DESKFLOW ACTIVE" : "DESKFLOW READY");
    ESP_LOGI(TAG, "Deskflow screen %s", active ? "entered" : "left");
}
static void ready_cb(void *arg, bool connected)
{
    portENTER_CRITICAL(&state_lock); ready = connected; portEXIT_CRITICAL(&state_lock);
    input_bridge_network_ready(connected);
    display_network(connected ? "DESKFLOW READY" : "WIFI CONNECTED");
    ESP_LOGI(TAG, "Deskflow %s", connected ? "ready" : "disconnected");
}
static void run_session(session_context_t *context)
{
    const deskflow_config_t *config = &context->config;
    display_network("DESKFLOW CONNECTING"); status_led_set_network(LED_LINK_AUTHENTICATING);
    ESP_LOGI(TAG, "Connecting to %s:%u as %s (%s)", config->server, config->port,
        config->screen, config->auth.enabled ? "TLS/pinned certificate" : "plain TCP");
    deskflow_transport_t *transport = deskflow_transport_connect(config, keep_running, context);
    deskflow_session_t *session = calloc(1, sizeof(*session));
    if (transport && session) {
        deskflow_callbacks_t cb = {
            .send = deskflow_transport_send, .keyboard = keyboard_cb, .mouse = mouse_cb,
            .focus = focus_cb, .ready = ready_cb, .arg = transport,
        };
        deskflow_session_init(session, config, &cb);
        uint32_t last_packet = esp_timer_get_time()/1000;
        uint8_t buffer[1024];
        uint32_t logged_moves = 0, last_motion_log = 0;
        while (keep_running(context)) {
            int n = deskflow_transport_read(transport, buffer, sizeof(buffer));
            if (n > 0) {
                last_packet = esp_timer_get_time()/1000;
                if (!deskflow_session_feed(session, buffer, n)) break;
                uint32_t moves = session->absolute_moves + session->relative_moves;
                if (moves != logged_moves && (!logged_moves || last_packet - last_motion_log >= 2000)) {
                    ESP_LOGI(TAG, "Pointer packets: absolute=%lu relative=%lu ignored=%lu focused=%u info_ack=%u",
                        (unsigned long)session->absolute_moves, (unsigned long)session->relative_moves,
                        (unsigned long)session->ignored_moves, session->focused, session->info_acked);
                    logged_moves = moves; last_motion_log = last_packet;
                }
            } else if (n == -2) {
                if ((uint32_t)(esp_timer_get_time()/1000 - last_packet) > session->keepalive_ms) {
                    ESP_LOGW(TAG, "Heartbeat timed out"); break;
                }
                deskflow_transport_wait(transport);
            } else break;
        }
        if (session->error) ESP_LOGW(TAG, "%s", session->error);
        deskflow_session_close(session);
    } else ESP_LOGW(TAG, "Connection failed; retrying");
    status_led_set_network(LED_LINK_IDLE);
    deskflow_transport_close(transport); free(session);
}
static void client_task(void *arg)
{
    session_context_t context;
    for (;;) {
        wifi_manager_connection(&context.link);
        if (!context.link.connected || !config_storage_deskflow(context.link.ssid, &context.config) || !context.config.server[0]) {
            vTaskDelay(pdMS_TO_TICKS(250)); continue;
        }
        run_session(&context);
        for (unsigned n = 0; n < 20 && keep_running(&context); n++) vTaskDelay(pdMS_TO_TICKS(100));
    }
}
void deskflow_client_status(void)
{
    bool online, active;
    portENTER_CRITICAL(&state_lock); online = ready; active = focused; portEXIT_CRITICAL(&state_lock);
    ESP_LOGI(TAG, "Deskflow %s", active ? "active" : online ? "ready" : "disconnected");
}
void deskflow_client_start(void)
{
    ESP_ERROR_CHECK(xTaskCreate(client_task, "deskflow", 16384, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

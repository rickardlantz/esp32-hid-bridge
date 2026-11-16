#include "diagnostic_log.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

#define LOG_LINES 32
#define LINE_SIZE 128
#define LOG_MAGIC 0x504c4731u
static struct saved_log {
    uint32_t magic, head, count;
    char lines[LOG_LINES][LINE_SIZE];
} saved;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t storage_lock;
static nvs_handle_t storage;
static bool enabled;
static uint32_t revision, persisted;
static esp_err_t last_save;
static int64_t capture_deadline;

static void append(const char *line)
{
    portENTER_CRITICAL(&lock);
    if (enabled) {
        strlcpy(saved.lines[saved.head], line, LINE_SIZE);
        saved.head = (saved.head + 1) % LOG_LINES;
        if (saved.count < LOG_LINES) saved.count++;
        revision++;
    }
    portEXIT_CRITICAL(&lock);
}
void diagnostic_log_capture(const char *line)
{
    // Never record typed keys, passwords, or dump output recursively.
    if (strstr(line, "diag_dump:")) return;
    if (strstr(line, "Pointer packets:") || strstr(line, "USB pointer") ||
        strstr(line, "USB state") || strstr(line, "USB enumeration") || strstr(line, "Deskflow screen") ||
        strstr(line, "Deskflow ready") || strstr(line, "Deskflow disconnected") ||
        strstr(line, "USB mouse test") || strstr(line, "Restart requested")) append(line);
}
static void flush_task(void *arg)
{
    struct saved_log *snapshot = malloc(sizeof(*snapshot));
    assert(snapshot);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(3000));
        portENTER_CRITICAL(&lock);
        bool expired = enabled && esp_timer_get_time() >= capture_deadline;
        portEXIT_CRITICAL(&lock);
        if (expired) diagnostic_log_enable(false);
        xSemaphoreTake(storage_lock, portMAX_DELAY);
        portENTER_CRITICAL(&lock);
        uint32_t version = revision;
        bool dirty = version != persisted;
        if (dirty) *snapshot = saved;
        portEXIT_CRITICAL(&lock);
        if (dirty) {
            last_save = nvs_set_blob(storage, "pointer_log", snapshot, sizeof(*snapshot));
            if (last_save == ESP_OK) last_save = nvs_commit(storage);
            if (last_save == ESP_OK) {
                portENTER_CRITICAL(&lock); persisted = version; portEXIT_CRITICAL(&lock);
            }
        }
        xSemaphoreGive(storage_lock);
    }
}
void diagnostic_log_init(void)
{
    storage_lock = xSemaphoreCreateMutex(); assert(storage_lock);
    ESP_ERROR_CHECK(nvs_open("diagnostics", NVS_READWRITE, &storage));
    size_t n = sizeof(saved);
    if (nvs_get_blob(storage, "pointer_log", &saved, &n) != ESP_OK || n != sizeof(saved) ||
        saved.magic != LOG_MAGIC || saved.head >= LOG_LINES || saved.count > LOG_LINES) {
        memset(&saved, 0, sizeof(saved)); saved.magic = LOG_MAGIC;
    }
    uint8_t active = 0;
    nvs_get_u8(storage, "enabled", &active); enabled = active != 0;
    capture_deadline = esp_timer_get_time() + 10LL * 60 * 1000000;
    append("--- Boot: pointer diagnostics enabled ---");
    ESP_ERROR_CHECK(xTaskCreate(flush_task, "pointer_log", 3072, NULL, 1, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
void diagnostic_log_enable(bool active)
{
    xSemaphoreTake(storage_lock, portMAX_DELAY);
    ESP_ERROR_CHECK(nvs_set_u8(storage, "enabled", active));
    ESP_ERROR_CHECK(nvs_commit(storage));
    portENTER_CRITICAL(&lock);
    enabled = active;
    capture_deadline = esp_timer_get_time() + 10LL * 60 * 1000000;
    portEXIT_CRITICAL(&lock);
    xSemaphoreGive(storage_lock);
    append("--- Pointer diagnostic capture enabled ---");
    ESP_LOGI("diagnostics", "Persistent pointer capture %s; trace dump reads it, trace off stops it", active ? "enabled" : "disabled");
}
void diagnostic_log_clear(void)
{
    xSemaphoreTake(storage_lock, portMAX_DELAY);
    portENTER_CRITICAL(&lock);
    memset(&saved, 0, sizeof(saved)); saved.magic = LOG_MAGIC; revision++; persisted = revision;
    portEXIT_CRITICAL(&lock);
    esp_err_t error = nvs_erase_key(storage, "pointer_log");
    if (error == ESP_OK) ESP_ERROR_CHECK(nvs_commit(storage));
    xSemaphoreGive(storage_lock);
    ESP_LOGI("diagnostics", "Pointer log cleared");
}
void diagnostic_log_dump(void)
{
    struct saved_log *snapshot = malloc(sizeof(*snapshot));
    if (!snapshot) return;
    xSemaphoreTake(storage_lock, portMAX_DELAY);
    portENTER_CRITICAL(&lock); *snapshot = saved; bool active = enabled; portEXIT_CRITICAL(&lock);
    esp_err_t error = last_save;
    xSemaphoreGive(storage_lock);
    ESP_LOGI("diag_dump", "Pointer log: %lu records, capture %s, last save %s", (unsigned long)snapshot->count,
        active ? "on" : "off", esp_err_to_name(error));
    for (unsigned i = 0; i < snapshot->count; i++) {
        unsigned index = (snapshot->head + LOG_LINES - snapshot->count + i) % LOG_LINES;
        ESP_LOGI("diag_dump", "%s", snapshot->lines[index]);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_LOGI("diag_dump", "End pointer log");
    free(snapshot);
}

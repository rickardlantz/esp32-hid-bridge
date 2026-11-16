#include "config_storage.h"
#include "fat_snapshot.h"
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_partition.h"
#include "esp_blockdev.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "wear_levelling.h"
#include "tinyusb_msc.h"
#include "nvs.h"
#include "server_auth.h"
#include <ctype.h>

static const char *TAG = "config";
static wl_handle_t medium;
static esp_blockdev_t block_device;
static SemaphoreHandle_t io_lock, config_lock;
static network_config_t active;
static unsigned active_version;
static atomic_uint writes, last_write_ms;
static atomic_bool force_reload;
static nvs_handle_t settings;

static const char example_ini[] =
    "; ESP input bridge: one section per Wi-Fi SSID. UTF-8, LF or CRLF.\r\n"
    "; Changes apply automatically after writes settle; no eject required.\r\n"
    "; Uncomment a section and set its values. Passwords are plain text.\r\n"
    "; TLS defaults to true and requires the server certificate SHA-256.\r\n"
    "; Set tls=false only when Deskflow server encryption is disabled.\r\n"
    "\r\n"
    ";[Your WiFi SSID]\r\n"
    ";password = your-wifi-password\r\n"
    ";server = 192.168.1.10\r\n"
    ";port = 24800\r\n"
    ";screen = esp-bridge\r\n"
    ";tls = true\r\n"
    ";server_fingerprint = <64 hexadecimal SHA-256 digits>\r\n"
    ";client_cert = CLIENT.CRT\r\n"
    ";client_key = CLIENT.KEY\r\n"
    ";width = 1920\r\n"
    ";height = 1080\r\n"
    ";layout = us\r\n"
    ";enabled = true\r\n";

static bool bounds(uint64_t address, size_t n)
{
    return address <= block_device.geometry.disk_size && n <= block_device.geometry.disk_size - address;
}
static esp_err_t block_read(esp_blockdev_handle_t dev, uint8_t *out, size_t cap, uint64_t address, size_t n)
{
    if (n > cap || !bounds(address, n)) return ESP_ERR_INVALID_SIZE;
    xSemaphoreTake(io_lock, portMAX_DELAY);
    esp_err_t err = wl_read(medium, address, out, n);
    xSemaphoreGive(io_lock);
    return err;
}
static esp_err_t block_write(esp_blockdev_handle_t dev, const uint8_t *data, uint64_t address, size_t n)
{
    if (!bounds(address, n) || address % 512 || n % 512) return ESP_ERR_INVALID_SIZE;
    xSemaphoreTake(io_lock, portMAX_DELAY);
    esp_err_t err = wl_erase_range(medium, address, n);
    if (err == ESP_OK) err = wl_write(medium, address, data, n);
    if (err == ESP_OK) {
        atomic_store(&last_write_ms, esp_timer_get_time() / 1000);
        atomic_fetch_add(&writes, 1);
    }
    xSemaphoreGive(io_lock);
    return err;
}
static esp_err_t block_sync(esp_blockdev_handle_t dev) { return ESP_OK; }
static esp_err_t block_ioctl(esp_blockdev_handle_t dev, uint8_t command, void *args)
{
    // Discard is optional; keeping released sectors does not change filesystem semantics.
    return command == ESP_BLOCKDEV_CMD_MARK_DELETED ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
}
static esp_err_t block_release(esp_blockdev_handle_t dev) { return ESP_OK; }
static const esp_blockdev_ops_t block_ops = {
    .read = block_read, .write = block_write, .sync = block_sync,
    .ioctl = block_ioctl, .release = block_release,
};
static bool snapshot_read(void *arg, uint64_t offset, void *data, size_t n)
{
    return bounds(offset, n) && wl_read(medium, offset, data, n) == ESP_OK;
}
static bool read_config(char *text, size_t *length)
{
    xSemaphoreTake(io_lock, portMAX_DELAY);
    bool ok = fat_snapshot_file(snapshot_read, NULL, block_device.geometry.disk_size,
        "CONFIG  INI", text, NETWORK_CONFIG_MAX + 1, length);
    xSemaphoreGive(io_lock);
    return ok;
}
bool config_storage_file(const char *name, char *text, size_t capacity, size_t *length)
{
    char short_name[11]; memset(short_name, ' ', sizeof(short_name));
    const char *dot = strchr(name, '.');
    if (!dot || dot == name || dot-name > 8 || !dot[1] || strlen(dot+1) > 3 || strchr(dot+1, '.')) return false;
    for (const char *s = name; *s; s++) {
        if (*s == '.') continue;
        if (!isalnum((unsigned char)*s) && *s != '_' && *s != '-') return false;
    }
    for (size_t i = 0; i < (size_t)(dot-name); i++) short_name[i] = toupper((unsigned char)name[i]);
    for (size_t i = 0; i < strlen(dot+1); i++) short_name[8+i] = toupper((unsigned char)dot[1+i]);
    xSemaphoreTake(io_lock, portMAX_DELAY);
    bool ok = fat_snapshot_file(snapshot_read, NULL, block_device.geometry.disk_size, short_name, text, capacity, length);
    xSemaphoreGive(io_lock);
    return ok;
}
static bool apply(const char *text, size_t length, network_config_t *parsed)
{
    unsigned line;
    const char *error;
    if (!network_config_parse(text, length, parsed, &line, &error)) {
        ESP_LOGW(TAG, "CONFIG.INI line %u: %s; previous settings retained", line, error);
        return false;
    }
    for (size_t i = 0; i < parsed->count; i++) {
        if (parsed->profiles[i].wifi.enabled && parsed->profiles[i].deskflow.auth.enabled &&
            !server_auth_validate(&parsed->profiles[i].deskflow.auth)) {
            ESP_LOGW(TAG, "Client certificate/key files are missing or invalid; previous settings retained");
            return false;
        }
    }
    // Editors can temporarily publish an empty file during truncate/replace.
    // Disable networks explicitly with enabled=false instead of erasing all
    // sections, so a partially saved file cannot discard working credentials.
    if (!parsed->count && active.count) {
        ESP_LOGW(TAG, "Empty configuration retained previous settings; use enabled=false to disable profiles");
        return false;
    }
    xSemaphoreTake(config_lock, portMAX_DELAY);
    bool changed = memcmp(&active, parsed, sizeof(active)) != 0;
    if (changed || !active_version) { active = *parsed; active_version++; }
    xSemaphoreGive(config_lock);
    if (changed) {
        ESP_ERROR_CHECK(nvs_set_blob(settings, "last_good_v1", parsed, sizeof(*parsed)));
        ESP_ERROR_CHECK(nvs_commit(settings));
        ESP_LOGI(TAG, "Applied CONFIG.INI (%u network profiles)", (unsigned)parsed->count);
    }
    return true;
}
void config_storage_get(network_config_t *config, unsigned *version)
{
    xSemaphoreTake(config_lock, portMAX_DELAY);
    *config = active;
    if (version) *version = active_version;
    xSemaphoreGive(config_lock);
}
void config_storage_reload(void) { atomic_store(&force_reload, true); }
void config_storage_wifi(wifi_profiles_t *profiles)
{
    memset(profiles, 0, sizeof(*profiles));
    xSemaphoreTake(config_lock, portMAX_DELAY);
    profiles->count = active.count;
    for (size_t i = 0; i < active.count; i++) profiles->profiles[i] = active.profiles[i].wifi;
    xSemaphoreGive(config_lock);
}
bool config_storage_deskflow(const char *ssid, deskflow_config_t *config)
{
    bool found = false;
    xSemaphoreTake(config_lock, portMAX_DELAY);
    for (size_t i = 0; i < active.count; i++) {
        if (active.profiles[i].wifi.enabled && !strcmp(ssid, active.profiles[i].wifi.ssid)) {
            *config = active.profiles[i].deskflow; found = true; break;
        }
    }
    xSemaphoreGive(config_lock);
    return found;
}

static void watch_task(void *arg)
{
    char *text = malloc(NETWORK_CONFIG_MAX + 1), *candidate = malloc(NETWORK_CONFIG_MAX + 1);
    network_config_t *parsed = malloc(sizeof(*parsed));
    assert(text && candidate && parsed);
    size_t candidate_length = 0;
    bool have_candidate = false;
    unsigned seen = atomic_load(&writes);
    for (;;) {
        unsigned generation = atomic_load(&writes);
        uint32_t now = esp_timer_get_time() / 1000;
        bool forced = atomic_exchange(&force_reload, false);
        if (forced) { seen = generation - 1; have_candidate = false; }
        if (generation != seen && (uint32_t)(now - atomic_load(&last_write_ms)) >= 1500) {
            size_t length;
            if (!read_config(text, &length)) {
                ESP_LOGW(TAG, "CONFIG.INI is missing or not yet readable; previous settings retained");
                seen = generation; have_candidate = false;
            } else if (!have_candidate || length != candidate_length || memcmp(candidate, text, length)) {
                memcpy(candidate, text, length);
                candidate_length = length; have_candidate = true;
            } else {
                if (generation == atomic_load(&writes)) {
                    apply(text, length, parsed);
                    seen = generation;
                }
                have_candidate = false;
            }
        } else if (generation == seen || (uint32_t)(now - atomic_load(&last_write_ms)) < 1500) {
            have_candidate = false;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// esp_tinyusb's default callback does not implement SYNCHRONIZE CACHE(10).
// Writes are synchronous at this block-device boundary; acknowledge the flush.
int32_t __real_tud_msc_scsi_cb(uint8_t lun, const uint8_t command[16], void *buffer, uint16_t size);
int32_t __wrap_tud_msc_scsi_cb(uint8_t lun, const uint8_t command[16], void *buffer, uint16_t size)
{
    if (command[0] == 0x35) return 0;
    return __real_tud_msc_scsi_cb(lun, command, buffer, size);
}

void config_storage_init(void)
{
    io_lock = xSemaphoreCreateMutex(); config_lock = xSemaphoreCreateMutex();
    assert(io_lock && config_lock);
    ESP_ERROR_CHECK(nvs_open("network_cfg", NVS_READWRITE, &settings));
    size_t saved_length = sizeof(active);
    if (nvs_get_blob(settings, "last_good_v1", &active, &saved_length) != ESP_OK ||
        saved_length != sizeof(active) || active.count > NETWORK_PROFILE_MAX) memset(&active, 0, sizeof(active));
    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "storage");
    ESP_ERROR_CHECK(partition ? ESP_OK : ESP_ERR_NOT_FOUND);
    ESP_ERROR_CHECK(wl_mount(partition, &medium));
    assert(wl_sector_size(medium) == 512);
    block_device.ops = &block_ops;
    block_device.geometry = (esp_blockdev_geometry_t){
        .disk_size = wl_size(medium), .read_size = 1, .write_size = 512, .erase_size = 512,
    };
    uint8_t boot[512];
    ESP_ERROR_CHECK(wl_read(medium, 0, boot, sizeof(boot)));
    bool blank = true;
    for (size_t i = 0; i < sizeof(boot); i++) if (boot[i] != 0xff) blank = false;
    tinyusb_msc_driver_config_t driver = { .user_flags.auto_mount_off = true };
    ESP_ERROR_CHECK(tinyusb_msc_install_driver(&driver));
    tinyusb_msc_storage_config_t config = {
        .medium.blockdev = &block_device,
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_APP,
        .fat_fs = {
            .base_path = "/config", .config.max_files = 3,
            .do_not_format = !blank,
        },
    };
    tinyusb_msc_storage_handle_t handle;
    ESP_ERROR_CHECK(tinyusb_msc_new_storage_blockdev(&config, &handle));
    // Only seed a new volume before it is exposed to USB. Never write files
    // from firmware while the computer owns the filesystem.
    if (blank) {
        FILE *f = fopen("/config/CONFIG.INI", "wb");
        ESP_ERROR_CHECK(f ? ESP_OK : ESP_FAIL);
        fwrite(example_ini, 1, sizeof(example_ini)-1, f); fclose(f);
        f = fopen("/config/README.TXT", "wb");
        ESP_ERROR_CHECK(f ? ESP_OK : ESP_FAIL);
        fputs("ESP input bridge configuration drive.\r\nEdit CONFIG.INI, one section per SSID.\r\n"
            "Settings reload automatically after USB writes settle (about 2 seconds).\r\n"
            "Use sync/flush if the operating system caches writes.\r\n"
            "Invalid edits preserve the last working settings.\r\n"
            "Deskflow: add the configured screen name on the server.\r\n"
            "TLS: use the SHA-256 fingerprint of the server certificate.\r\n"
            "The volume exposes data storage, not the firmware/NVS partitions.\r\n", f);
        fclose(f);
    }
    ESP_ERROR_CHECK(tinyusb_msc_set_storage_mount_point(handle, TINYUSB_MSC_STORAGE_MOUNT_USB));
    char *text = malloc(NETWORK_CONFIG_MAX + 1);
    network_config_t *parsed = malloc(sizeof(*parsed));
    assert(text && parsed);
    size_t length;
    if (read_config(text, &length)) apply(text, length, parsed);
    free(parsed); free(text);
    ESP_ERROR_CHECK(xTaskCreate(watch_task, "config_watch", 4096, NULL, 1, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

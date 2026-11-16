#pragma once
#include "network_config.h"

void config_storage_init(void);
void config_storage_get(network_config_t *config, unsigned *version);
void config_storage_reload(void);
void config_storage_wifi(wifi_profiles_t *profiles);
bool config_storage_deskflow(const char *ssid, deskflow_config_t *config);
bool config_storage_file(const char *name, char *text, size_t capacity, size_t *length);

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wifi_config.h"
#include "deskflow_config.h"

#define NETWORK_PROFILE_MAX WIFI_PROFILE_MAX
#define NETWORK_CONFIG_MAX 8192
typedef struct {
    wifi_profile_t wifi;
    deskflow_config_t deskflow;
} network_profile_t;
typedef struct {
    size_t count;
    network_profile_t profiles[NETWORK_PROFILE_MAX];
} network_config_t;

// Unknown fields and invalid active profiles fail atomically. No credentials
// are included in errors; error_line identifies where the INI needs attention.
bool network_config_parse(const char *text, size_t length, network_config_t *out,
    unsigned *error_line, const char **error);

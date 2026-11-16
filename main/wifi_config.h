#pragma once
#include <stdbool.h>
#include <stddef.h>

#define WIFI_PROFILE_MAX 8
typedef struct {
    char ssid[33], password[65];
    bool enabled;
} wifi_profile_t;
typedef struct {
    size_t count;
    wifi_profile_t profiles[WIFI_PROFILE_MAX];
} wifi_profiles_t;

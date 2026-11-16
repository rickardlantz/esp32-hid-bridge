#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool enabled, have_fingerprint;
    uint8_t fingerprint[32];
    char client_cert[13], client_key[13];
    uint64_t material_version;
} server_auth_t;
typedef struct {
    char server[128], screen[64];
    uint16_t port, width, height;
    bool swedish;
    server_auth_t auth;
} deskflow_config_t;

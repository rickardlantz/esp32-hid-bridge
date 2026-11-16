#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    bool connected;
    char ssid[33], address[16];
    uint32_t generation;
} wifi_connection_t;

void wifi_manager_start(void);
void wifi_manager_connection(wifi_connection_t *connection);
void wifi_manager_status(void);

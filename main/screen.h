#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "orientation.h"
typedef enum {
    SCREEN_STARTING, SCREEN_SCANNING, SCREEN_CONNECTING, SCREEN_PAIRING,
    SCREEN_CONNECTED, SCREEN_AUTH_FAILED, SCREEN_UNSUPPORTED
} screen_state_t;
typedef struct {
    screen_state_t state;
    bool usb_connected;
    uint32_t pin;
    char detail[25];
} screen_status_t;

// RGB565 in LCD wire byte order; clips all drawing to the requested strip.
void screen_render_strip(uint16_t *pixels, int y, int rows,
    const display_geometry_t *geometry, const screen_status_t *status);

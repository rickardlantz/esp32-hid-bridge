#pragma once
#include <stdbool.h>
#include <stdint.h>

// Mouse report ID 2: relative movement in screen pixels (USB report ID 3).
typedef struct {
    uint8_t id, buttons;
    int16_t x, y;
    int8_t wheel, pan;
} bridge_mouse_t;
void input_bridge_network_keyboard(const uint8_t report[8]);
void input_bridge_network_mouse(const bridge_mouse_t *report);
void input_bridge_network_focus(bool active);
void input_bridge_network_ready(bool ready);

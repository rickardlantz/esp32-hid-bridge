#pragma once
#include "input_bridge.h"

// Consume one conventional five-byte USB mouse report. Large movements are
// split without repeating wheel input or changing the button state.
bool mouse_report_next(bridge_mouse_t *pending, uint8_t data[5]);

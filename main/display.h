#pragma once
#include "screen.h"

void display_init(void);
void display_status(screen_state_t state, const char *detail);
void display_pairing_pin(uint32_t pin);
void display_auth_complete(bool success);
void display_usb(bool connected);
// Manual selections persist. Auto uses a sensor when present, otherwise portrait.
#define DISPLAY_ORIENTATION_AUTO ORIENTATION_COUNT
void display_set_orientation(int mode);
void display_rotate(void);
orientation_t display_get_orientation(void);
bool display_accelerometer_available(void);

#pragma once
#include "status_led_pattern.h"

void status_led_init(void);
void status_led_set_link(led_link_state_t state);
void status_led_set_usb(bool ready);

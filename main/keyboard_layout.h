#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Verify the descriptor, rather than treating every eight-byte payload as keys.
// Returns the validated BLE payload length: 7 (no reserved byte), 8, or 0.
uint8_t keyboard_layout_size(const uint8_t *map, size_t length, uint8_t report_id);
static inline bool keyboard_layout_supported(const uint8_t *map, size_t length, uint8_t id)
{
    return keyboard_layout_size(map, length, id) != 0;
}

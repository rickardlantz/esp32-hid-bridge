#include "keyboard_layout.h"

uint8_t keyboard_layout_size(const uint8_t *map, size_t length, uint8_t wanted)
{
    uint32_t page = 0, size = 0, count = 0, minimum = 0, maximum = 0;
    uint32_t offset = 0;
    uint8_t id = 0;
    bool modifiers = false, reserved = false, keys = false;
    for (size_t i = 0; i < length;) {
        uint8_t prefix = map[i++];
        if (prefix == 0xfe) return false; // Unsupported long item.
        size_t n = prefix & 3;
        if (n == 3) n = 4;
        if (length - i < n) return false;
        uint32_t value = 0;
        for (size_t j = 0; j < n; j++) value |= (uint32_t)map[i++] << (8 * j);
        switch (prefix & 0xfc) {
        case 0x04: page = value; break;
        case 0x74: size = value; break;
        case 0x94: count = value; break;
        case 0x84:
            if (!value || value > 255) return false;
            id = value;
            break;
        case 0xa4: case 0xb4: return false; // Global push/pop not supported yet.
        case 0x18: minimum = value; break;
        case 0x28: maximum = value; break;
        case 0x80:
            if (id == wanted) {
                if (size > 64 || count > 64 || size * count > 64 - offset) return false;
                if (offset == 0 && page == 7 && size == 1 && count == 8 &&
                    minimum == 0xe0 && maximum == 0xe7 && value == 2) modifiers = true;
                else if (offset == 8 && size * count == 8 && (value & 1)) reserved = true;
                else if ((offset == 8 || offset == 16) && page == 7 && size == 8 && count == 6 &&
                    value == 0 && minimum == 0 && maximum <= 255 && maximum >= 0x65) keys = true;
                else return false;
                offset += size * count;
            }
            minimum = maximum = 0;
            break;
        default:
            // Local items expire after every Main item, including collections.
            if ((prefix & 0x0c) == 0) minimum = maximum = 0;
            break;
        }
    }
    if (!modifiers || !keys) return 0;
    if (offset == 64 && reserved) return 8;
    if (offset == 56 && !reserved) return 7;
    return 0;
}

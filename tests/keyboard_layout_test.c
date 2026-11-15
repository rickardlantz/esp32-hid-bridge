#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "keyboard_layout.h"

int main(void)
{
    uint8_t map[] = {
        0x05,0x01, 0x09,0x06, 0xa1,0x01,
        0x85,0x03,
        0x05,0x07, 0x19,0xe0, 0x29,0xe7, 0x75,0x01, 0x95,0x08, 0x81,0x02,
        0x75,0x08, 0x95,0x01, 0x81,0x01,
        0x19,0x00, 0x29,0x65, 0x75,0x08, 0x95,0x06, 0x81,0x00, 0xc0
    };
    assert(keyboard_layout_supported(map, sizeof(map), 3));
    assert(!keyboard_layout_supported(map, sizeof(map), 0));
    // Every prefix before the complete key array must fail closed.
    for (size_t n = 0; n < sizeof(map) - 2; n++)
        assert(!keyboard_layout_supported(map, n, 3));
    uint8_t changed[sizeof(map)];
    memcpy(changed, map, sizeof(map));
    changed[sizeof(map)-2] = 2; // Variable bitmap rather than key array.
    assert(!keyboard_layout_supported(changed, sizeof(changed), 3));
    memcpy(changed, map, sizeof(map));
    changed[sizeof(map)-4] = 7; // Extra key extends beyond 64 bits.
    assert(!keyboard_layout_supported(changed, sizeof(changed), 3));
    memcpy(changed, map, sizeof(map));
    changed[17] = 7; // Seven modifier bits shift all following fields.
    assert(!keyboard_layout_supported(changed, sizeof(changed), 3));
    uint8_t plain[sizeof(map)-2];
    memcpy(plain, map, 6);
    memcpy(plain+6, map+8, sizeof(map)-8);
    assert(keyboard_layout_supported(plain, sizeof(plain), 0));
    uint8_t logitech[sizeof(map)-6];
    memcpy(logitech, map, 20);
    memcpy(logitech + 20, map + 26, sizeof(map) - 26);
    assert(keyboard_layout_size(logitech, sizeof(logitech), 3) == 7);
    assert(keyboard_layout_size(map, sizeof(map), 3) == 8);
    puts("keyboard descriptor tests passed");
}

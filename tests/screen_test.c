#include "screen.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    size_t count = 172 * 320;
    uint16_t *full = malloc(count * sizeof(*full));
    uint16_t *strips = malloc(count * sizeof(*strips));
    assert(full && strips);
    for (orientation_t rotation = 0; rotation < ORIENTATION_COUNT; rotation++) {
      display_geometry_t geometry = orientation_geometry(rotation);
      for (screen_state_t state = SCREEN_STARTING; state <= SCREEN_UNSUPPORTED; state++) {
        screen_status_t s = {.state = state, .pin = 12345, .usb_connected = true};
        strcpy(s.detail, "LOGITECH KEYBOARD");
        screen_render_strip(full, 0, geometry.height, &geometry, &s);
        for (int y = 0; y < geometry.height; y += 16) {
            int rows = geometry.height - y;
            if (rows > 16) rows = 16;
            screen_render_strip(strips + y * geometry.width, y, rows, &geometry, &s);
        }
        assert(memcmp(full, strips, count * sizeof(*full)) == 0);
      }
    }
    if (argc > 1) {
        display_geometry_t geometry = orientation_geometry(ORIENTATION_LANDSCAPE);
        screen_status_t s = {.state = SCREEN_PAIRING, .pin = 12345, .usb_connected = true};
        screen_render_strip(full, 0, geometry.height, &geometry, &s);
        FILE *f = fopen(argv[1], "wb");
        assert(f);
        fprintf(f, "P6\n%d %d\n255\n", geometry.width, geometry.height);
        for (size_t i = 0; i < count; i++) {
            uint16_t p = (full[i] >> 8) | (full[i] << 8);
            fputc(((p >> 11) & 31) * 255 / 31, f);
            fputc(((p >> 5) & 63) * 255 / 63, f);
            fputc((p & 31) * 255 / 31, f);
        }
        fclose(f);
    }
    free(full);
    free(strips);
    puts("screen strip rendering tests passed");
}

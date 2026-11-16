#include "screen.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// Compact 5x7 uppercase font, columns with low bit at the top.
static const uint8_t glyphs[][5] = {
    {0x3e,0x51,0x49,0x45,0x3e}, {0,0x42,0x7f,0x40,0},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4b,0x31},
    {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30}, {1,0x71,9,5,3},
    {0x36,0x49,0x49,0x49,0x36}, {6,0x49,0x49,0x29,0x1e},
    {0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36},
    {0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c},
    {0x7f,0x49,0x49,0x49,0x41}, {0x7f,9,9,9,1},
    {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,8,8,8,0x7f},
    {0,0x41,0x7f,0x41,0}, {0x20,0x40,0x41,0x3f,1},
    {0x7f,8,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
    {0x7f,2,0x0c,2,0x7f}, {0x7f,4,8,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e}, {0x7f,9,9,9,6},
    {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,9,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {1,1,0x7f,1,1},
    {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f},
    {0x3f,0x40,0x38,0x40,0x3f}, {0x63,0x14,8,0x14,0x63},
    {7,8,0x70,8,7}, {0x61,0x51,0x49,0x45,0x43}
};
typedef struct { uint16_t *pixels; int y, rows, width; } canvas_t;
static uint16_t color(unsigned rgb)
{
    uint16_t c = ((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0) | ((rgb >> 3) & 0x001f);
    return (c >> 8) | (c << 8);
}
static void rect(canvas_t *c, int x, int y, int width, int height, uint16_t ink)
{
    int left = x < 0 ? 0 : x, right = x + width;
    if (right > c->width) right = c->width;
    int top = y < c->y ? c->y : y, bottom = y + height;
    if (bottom > c->y + c->rows) bottom = c->y + c->rows;
    for (int row = top; row < bottom; row++)
        for (int col = left; col < right; col++)
            c->pixels[(row - c->y) * c->width + col] = ink;
}
static void text(canvas_t *c, const char *s, int y, int scale, uint16_t ink)
{
    int x = (c->width - ((int)strlen(s) * 6 - 1) * scale) / 2;
    for (; *s; s++, x += 6 * scale) {
        int ch = toupper((unsigned char)*s);
        int index = ch >= '0' && ch <= '9' ? ch - '0' :
            (ch >= 'A' && ch <= 'Z' ? ch - 'A' + 10 : -1);
        if (index < 0) continue;
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 7; row++)
                if (glyphs[index][col] & (1 << row))
                    rect(c, x + col * scale, y + row * scale, scale, scale, ink);
    }
}
void screen_render_strip(uint16_t *pixels, int y, int rows,
    const display_geometry_t *g, const screen_status_t *s)
{
    canvas_t c = {pixels, y, rows, g->width};
    uint16_t bg = color(0x091321), white = color(0xf1f5f9), muted = color(0x94a3b8);
    uint16_t accent = color(s->state == SCREEN_CONNECTED ? 0x34d399 :
        s->state == SCREEN_PAIRING ? 0xfbbf24 :
        s->state >= SCREEN_AUTH_FAILED ? 0xfb7185 : 0x38bdf8);
    rect(&c, 0, 0, g->width, g->height, bg);
    static const char *titles[] = {
        "STARTING", "SCANNING", "CONNECTING", "PAIRING",
        "CONNECTED", "PAIR FAILED", "UNSUPPORTED"
    };
    if (g->width > g->height) {
        // Landscape uses a compact header/footer, leaving the center for the
        // PIN or primary status. It reflows content rather than stretching it.
        rect(&c, 12, 10, 24, 3, accent);
        text(&c, "KEYBOARD BRIDGE", 19, 2, white);
        rect(&c, 12, 41, g->width - 24, 24, color(0x17263a));
        text(&c, titles[s->state], 46, 2, accent);
        if (s->state == SCREEN_PAIRING) {
            char pin[7];
            snprintf(pin, sizeof(pin), "%06lu", (unsigned long)(s->pin % 1000000));
            text(&c, pin, 75, 5, accent);
            text(&c, "TYPE CODE ON KEYBOARD THEN ENTER", 119, 1, white);
        } else if (s->state == SCREEN_CONNECTED) {
            text(&c, s->detail, 79, 1, muted);
            text(&c, "READY TO TYPE", 101, 2, accent);
        } else if (s->state == SCREEN_SCANNING) {
            text(&c, "PUT KEYBOARD IN PAIRING MODE", 83, 1, white);
            text(&c, s->detail, 107, 1, muted);
        } else {
            text(&c, s->detail, 83, 1, white);
            if (s->state == SCREEN_AUTH_FAILED) text(&c, "TRY PAIRING AGAIN", 107, 1, muted);
        }
        rect(&c, 12, 139, g->width - 24, 1, color(0x26364b));
        char footer[48];
        snprintf(footer, sizeof(footer), "%s  %s", s->usb_connected ? "USB READY" : "USB WAITING", s->network);
        text(&c, footer, 146, 1,
            s->usb_connected ? color(0x34d399) : muted);
        text(&c, "BOOT TAP ROTATE   HOLD 3S PAIR", 160, 1, muted);
        return;
    }
    rect(&c, 14, 10, 28, 3, accent);
    text(&c, "KEYBOARD", 25, 2, white);
    text(&c, "BRIDGE", 46, 2, white);
    rect(&c, 12, 77, g->width - 24, 32, color(0x17263a));
    text(&c, titles[s->state], 86, 2, accent);
    if (s->state == SCREEN_PAIRING) {
        char pin[7];
        snprintf(pin, sizeof(pin), "%06lu", (unsigned long)(s->pin % 1000000));
        text(&c, "ENTER THIS CODE", 130, 1, muted);
        text(&c, pin, 151, 4, accent);
        text(&c, "ON YOUR KEYBOARD", 200, 1, white);
        text(&c, "THEN PRESS ENTER", 215, 1, white);
    } else if (s->state == SCREEN_CONNECTED) {
        text(&c, s->detail, 141, 1, muted);
        text(&c, "READY", 173, 3, accent);
        text(&c, "TO TYPE", 202, 2, white);
    } else if (s->state == SCREEN_SCANNING) {
        text(&c, "PUT KEYBOARD IN", 145, 1, white);
        text(&c, "PAIRING MODE", 161, 1, white);
        text(&c, s->detail, 196, 1, muted);
    } else {
        text(&c, s->detail, 151, 1, white);
        if (s->state == SCREEN_AUTH_FAILED) text(&c, "TRY PAIRING AGAIN", 177, 1, muted);
    }
    rect(&c, 14, 243, 144, 1, color(0x26364b));
    text(&c, s->usb_connected ? "USB CONNECTED" : "USB WAITING", 255, 1,
        s->usb_connected ? color(0x34d399) : muted);
    text(&c, s->network, 268, 1, muted);
    text(&c, "BOOT TAP ROTATE", 280, 1, muted);
    text(&c, "HOLD 3S TO PAIR", 300, 1, muted);

}

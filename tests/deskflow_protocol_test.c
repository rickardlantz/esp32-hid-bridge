#include "deskflow_protocol.h"
#include "deskflow_keys.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t sent[65536], keyboard[8]; size_t sent_length;
    bridge_mouse_t mouse;
    bool focused, ready;
    unsigned mouse_events;
} sink_t;
static bool send_bytes(void *arg, const uint8_t *data, size_t n)
{
    sink_t *sink = arg;
    assert(sink->sent_length + n <= sizeof(sink->sent));
    memcpy(sink->sent + sink->sent_length, data, n); sink->sent_length += n; return true;
}
static void keyboard(void *arg, const uint8_t report[8]) { memcpy(((sink_t *)arg)->keyboard, report, 8); }
static void mouse(void *arg, const bridge_mouse_t *report) { sink_t *sink = arg; sink->mouse = *report; sink->mouse_events++; }
static void focus(void *arg, bool active) { ((sink_t *)arg)->focused = active; }
static void ready(void *arg, bool active) { ((sink_t *)arg)->ready = active; }
static void put32(uint8_t *p, uint32_t n) { p[0]=n>>24; p[1]=n>>16; p[2]=n>>8; p[3]=n; }
static bool packet(deskflow_session_t *s, const uint8_t *body, size_t n)
{
    uint8_t header[4]; put32(header, n);
    // Deliberately fragmented: framing must survive arbitrary TCP boundaries.
    for (unsigned i = 0; i < 4; i++) if (!deskflow_session_feed(s, header+i, 1)) return false;
    for (size_t i = 0; i < n; i++) if (!deskflow_session_feed(s, body+i, 1)) return false;
    return true;
}
int main(void)
{
    sink_t *sink = calloc(1, sizeof(*sink));
    deskflow_session_t *s = calloc(1, sizeof(*s));
    assert(sink && s);
    deskflow_config_t config = {.width=1920, .height=1080}; strcpy(config.screen, "esp-bridge");
    deskflow_callbacks_t cb = {.send=send_bytes, .keyboard=keyboard, .mouse=mouse, .focus=focus, .ready=ready, .arg=sink};
    deskflow_session_init(s, &config, &cb);
    assert(packet(s, (const uint8_t *)"Barrier\0\1\0\10", 11));
    assert(s->hello && s->protocol_minor == 8);
    assert(!memcmp(sink->sent+4, "Barrier", 7));
    assert(packet(s, (const uint8_t *)"QINF", 4));
    assert(packet(s, (const uint8_t *)"DSOP\0\0\0\2HART\0\0\3\xe8", 16));
    assert(sink->ready && s->keepalive_ms == 3000);
    assert(packet(s, (const uint8_t *)"CIAK", 4));
    uint8_t enter[14] = {'C','I','N','N'};
    assert(packet(s, enter, sizeof(enter)) && sink->focused);
    uint8_t down[] = {'D','K','D','L', 0,'a', 0,0, 0,30, 0,0,0,2, 'e','n'};
    assert(packet(s, down, sizeof(down)) && sink->keyboard[2] == 4);
    uint8_t up[] = {'D','K','U','P', 0,'Z', 0,0, 0,30};
    assert(packet(s, up, sizeof(up)) && sink->keyboard[2] == 0); // Release by physical ID, not changed symbol.
    uint8_t ctrl[] = {'D','K','D','N', 0xef,0xe4, 0,2, 0,31};
    assert(packet(s, ctrl, sizeof(ctrl)) && sink->keyboard[0] == 0x10);
    memcpy(ctrl, "DKUP", 4);
    assert(packet(s, ctrl, sizeof(ctrl)) && sink->keyboard[0] == 0);
    uint8_t move[] = {'D','M','M','V', 7,0x7f, 4,0x37};
    assert(packet(s, move, sizeof(move)) && sink->mouse.id == 2 && sink->mouse.x == 1919 && sink->mouse.y == 1079);
    // Independently check Deskflow's left/middle/right IDs against USB bits.
    static const uint8_t expected[] = {0,1,4,2,8,16};
    for (unsigned button = 1; button <= 5; button++) {
        uint8_t event[] = {'D','M','D','N',button};
        assert(packet(s, event, sizeof(event)) && sink->mouse.buttons == expected[button]);
        memcpy(event, "DMUP", 4);
        assert(packet(s, event, sizeof(event)) && sink->mouse.buttons == 0);
    }
    assert(packet(s, (const uint8_t *)"DMDN\1", 5) && sink->mouse.buttons == 1);
    assert(packet(s, (const uint8_t *)"DMDN\3", 5) && sink->mouse.buttons == 3);
    assert(packet(s, (const uint8_t *)"DMUP\1", 5) && sink->mouse.buttons == 2);
    assert(packet(s, (const uint8_t *)"DMUP\3", 5) && sink->mouse.buttons == 0);
    assert(sink->mouse.x == 0 && sink->mouse.y == 0); // Buttons must not replay motion.
    assert(packet(s, move, sizeof(move)) && sink->mouse.x == 0 && sink->mouse.y == 0);
    move[5] = 0x75; move[7] = 0x23;
    assert(packet(s, move, sizeof(move)) && sink->mouse.x == -10 && sink->mouse.y == -20);
    unsigned mouse_count = sink->mouse_events;
    uint8_t wheel[] = {'D','M','W','M', 0,0, 0,60};
    assert(packet(s, wheel, sizeof(wheel)) && sink->mouse_events == mouse_count);
    assert(packet(s, wheel, sizeof(wheel)) && sink->mouse.wheel == 1);
    uint8_t relative[] = {'D','M','R','M', 0xff,0xf6, 0,20};
    assert(packet(s, relative, sizeof(relative)) && sink->mouse.id == 2 && sink->mouse.x == -10 && sink->mouse.y == 20);
    assert(packet(s, move, sizeof(move)) && sink->mouse.x == 10 && sink->mouse.y == -20);
    assert(packet(s, (const uint8_t *)"COUT", 4));
    enter[5] = 100; enter[7] = 50;
    assert(packet(s, enter, sizeof(enter)) && sink->mouse.x == 0 && sink->mouse.y == 0);
    uint8_t nearby[] = {'D','M','M','V',0,103,0,48};
    assert(packet(s, nearby, sizeof(nearby)) && sink->mouse.x == 3 && sink->mouse.y == -2);
    uint8_t *large = calloc(20000, 1); assert(large); memcpy(large, "DCLP", 4);
    assert(packet(s, large, 20000)); free(large);
    assert(packet(s, (const uint8_t *)"CALV", 4));
    assert(packet(s, (const uint8_t *)"COUT", 4) && !sink->focused && !sink->mouse.buttons);
    assert(packet(s, down, sizeof(down)) && sink->keyboard[2] == 0); // Ignore out-of-focus input.
    assert(!packet(s, (const uint8_t *)"DMMV\0", 5));
    deskflow_session_close(s);
    assert(!sink->ready && !sink->focused && !sink->keyboard[0] && !sink->keyboard[2]);
    assert(deskflow_key_map('@', false).usage == 0x1f);
    assert(deskflow_key_map('@', false).modifiers == 2);
    assert(deskflow_key_map('@', true).modifiers == 0x40);
    assert(deskflow_key_map(0xe5, true).usage == 0x2f);
    assert(deskflow_key_map(0xf6, true).usage == 0x33);
    assert(deskflow_key_map(0xe4, true).usage == 0x34);
    free(s); free(sink);
    puts("Deskflow framing, handshake, keyboard, mouse and disconnect tests passed");
}

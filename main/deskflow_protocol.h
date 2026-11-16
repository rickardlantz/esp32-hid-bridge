#pragma once
#include "deskflow_config.h"
#include "input_bridge.h"
#include <stddef.h>

#define DESKFLOW_FRAME_BUFFER 8192
typedef struct {
    bool (*send)(void *arg, const uint8_t *data, size_t length);
    void (*keyboard)(void *arg, const uint8_t report[8]);
    void (*mouse)(void *arg, const bridge_mouse_t *report);
    void (*focus)(void *arg, bool focused);
    void (*ready)(void *arg, bool ready);
    void *arg;
} deskflow_callbacks_t;
typedef struct {
    uint16_t button;
    uint8_t usage, modifiers;
} deskflow_pressed_t;
typedef struct {
    deskflow_callbacks_t cb;
    deskflow_config_t profile;
    uint8_t header[4], data[DESKFLOW_FRAME_BUFFER];
    unsigned header_length;
    uint32_t wanted, received, keepalive_ms;
    bool hello, ready, focused, info_acked;
    uint16_t protocol_minor, mask;
    deskflow_pressed_t keys[32];
    bridge_mouse_t pointer;
    int wheel_x, wheel_y;
    int pointer_x, pointer_y;
    uint32_t absolute_moves, relative_moves, ignored_moves;
    const char *error;
} deskflow_session_t;

void deskflow_session_init(deskflow_session_t *s, const deskflow_config_t *profile, const deskflow_callbacks_t *cb);
bool deskflow_session_feed(deskflow_session_t *s, const uint8_t *data, size_t length);
void deskflow_session_close(deskflow_session_t *s);

#include "deskflow_protocol.h"
#include "deskflow_keys.h"
#include <string.h>

static uint16_t be16(const uint8_t *p) { return ((uint16_t)p[0] << 8) | p[1]; }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)be16(p) << 16) | be16(p+2); }
static void put16(uint8_t *p, uint16_t n) { p[0] = n >> 8; p[1] = n; }
static void put32(uint8_t *p, uint32_t n) { put16(p, n >> 16); put16(p+2, n); }
static bool fail(deskflow_session_t *s, const char *error) { s->error = error; return false; }
static bool send(deskflow_session_t *s, const uint8_t *body, size_t n)
{
    uint8_t header[4]; put32(header, n);
    return (s->cb.send(s->cb.arg, header, 4) && s->cb.send(s->cb.arg, body, n)) || fail(s, "socket write failed");
}
static uint8_t mask_modifiers(uint16_t mask, uint8_t explicit)
{
    uint8_t modifiers = explicit;
    if ((mask & 1) && !(explicit & 0x22)) modifiers |= 2;
    if ((mask & 2) && !(explicit & 0x11)) modifiers |= 1;
    if ((mask & 4) && !(explicit & 0x44)) modifiers |= 4;
    if ((mask & 0x18) && !(explicit & 0x88)) modifiers |= 8;
    if (mask & 0x20) modifiers |= 0x40;
    return modifiers;
}
static void keyboard(deskflow_session_t *s)
{
    uint8_t report[8] = {0}, explicit = 0, implied = 0;
    unsigned count = 0;
    for (unsigned i = 0; i < 32; i++) {
        deskflow_pressed_t *key = &s->keys[i];
        if (!key->button) continue;
        if (key->usage >= 0xe0 && key->usage <= 0xe7) explicit |= 1u << (key->usage - 0xe0);
        else if (key->usage) {
            bool duplicate = false;
            for (unsigned j = 0; j < count && j < 6; j++) duplicate |= report[j+2] == key->usage;
            if (!duplicate) { if (count < 6) report[count+2] = key->usage; count++; }
            implied |= key->modifiers;
        }
    }
    if (count > 6) memset(report+2, 1, 6); // HID ErrorRollOver until keys are released.
    report[0] = mask_modifiers(s->mask, explicit) | implied;
    s->cb.keyboard(s->cb.arg, report);
}
static bool key_event(deskflow_session_t *s, const uint8_t *p, bool down)
{
    uint16_t id = be16(p+4), mask = be16(p+6), button = be16(p+8);
    if (!button) return fail(s, "invalid physical key identifier");
    if (!s->focused) return true;
    deskflow_pressed_t *slot = NULL;
    for (unsigned i = 0; i < 32; i++) if (s->keys[i].button == button) slot = &s->keys[i];
    s->mask = mask;
    if (down) {
        if (!slot) for (unsigned i = 0; i < 32; i++) if (!s->keys[i].button) { slot = &s->keys[i]; break; }
        if (!slot) return fail(s, "too many simultaneous keys");
        deskflow_key_t key = deskflow_key_map(id, s->profile.swedish);
        *slot = (deskflow_pressed_t){button, key.usage, key.modifiers};
    } else if (slot) {
        uint8_t usage = slot->usage;
        memset(slot, 0, sizeof(*slot));
        if (usage >= 0xe0 && usage <= 0xe7) {
            unsigned bit = (usage - 0xe0) % 4;
            uint16_t group = bit == 0 ? 2 : bit == 1 ? 1 : bit == 2 ? 0x24 : 0x18;
            s->mask &= ~group;
            uint8_t hid_group = bit == 0 ? 0x11 : bit == 1 ? 0x22 : bit == 2 ? 0x44 : 0x88;
            for (unsigned i = 0; i < 32; i++) s->keys[i].modifiers &= ~hid_group;
        }
    }
    keyboard(s);
    return true;
}
static void pointer(deskflow_session_t *s)
{
    s->cb.mouse(s->cb.arg, &s->pointer);
    s->pointer.wheel = s->pointer.pan = 0;
    if (s->pointer.id == 2) s->pointer.x = s->pointer.y = 0;
}
static void release(deskflow_session_t *s)
{
    memset(s->keys, 0, sizeof(s->keys)); s->mask = 0;
    keyboard(s);
    s->pointer.buttons = 0; s->pointer.wheel = s->pointer.pan = 0;
    if (s->pointer.id == 2) s->pointer.x = s->pointer.y = 0;
    pointer(s);
}
static bool command(deskflow_session_t *s, const uint8_t *p, size_t n)
{
    if (!s->hello) {
        if (n != 11 || (memcmp(p, "Barrier", 7) && memcmp(p, "Synergy", 7))) return fail(s, "invalid server hello");
        unsigned major = be16(p+7), minor = be16(p+9);
        if (major != 1 || minor < 1) return fail(s, "unsupported server protocol version");
        s->protocol_minor = minor < 8 ? minor : 8;
        uint8_t hello[128];
        size_t length = strlen(s->profile.screen);
        memcpy(hello, p, 7); put16(hello+7, 1); put16(hello+9, s->protocol_minor);
        put32(hello+11, length); memcpy(hello+15, s->profile.screen, length);
        s->hello = true;
        return send(s, hello, 15+length);
    }
    if (n < 4) return fail(s, "short protocol message");
    bool was_ready = s->ready;
    if (!memcmp(p, "QINF", 4)) {
        if (n != 4) return fail(s, "invalid QINF length");
        uint8_t info[18] = {'D','I','N','F'};
        put16(info+8, s->profile.width); put16(info+10, s->profile.height);
        put16(info+14, s->profile.width/2); put16(info+16, s->profile.height/2);
        if (!send(s, info, sizeof(info))) return false;
    } else if (!memcmp(p, "DSOP", 4)) {
        if (n < 8 || be32(p+4) > 1024 || be32(p+4) % 2 || 8 + (uint64_t)be32(p+4)*4 != n)
            return fail(s, "invalid server options");
        for (size_t i = 8; i + 7 < n; i += 8) {
            // Deskflow's heartbeat option is the four-byte ID 'HART'.
            if (!memcmp(p+i, "HART", 4)) {
                uint32_t rate = be32(p+i+4);
                if (rate >= 250 && rate <= 60000) s->keepalive_ms = rate * 3;
            }
        }
        if (!s->ready) { s->ready = true; s->cb.ready(s->cb.arg, true); }
    } else if (!memcmp(p, "CALV", 4)) {
        if (n != 4 || !send(s, p, 4)) return fail(s, "invalid keepalive or send failed");
    } else if (!memcmp(p, "CIAK", 4)) {
        if (n != 4) return fail(s, "invalid information acknowledgement");
        s->info_acked = true;
    } else if (!memcmp(p, "CINN", 4)) {
        if (n != 14 || !s->ready) return fail(s, "invalid screen entry");
        release(s);
        s->focused = true; s->cb.focus(s->cb.arg, true);
        s->mask = be16(p+12); keyboard(s);
        // Entry is a new coordinate baseline, not a movement from the last screen.
        s->pointer.id = 2;
        s->pointer_x = (int16_t)be16(p+4);
        s->pointer_y = (int16_t)be16(p+6);
        s->pointer.x = s->pointer.y = 0;
        pointer(s);
    } else if (!memcmp(p, "COUT", 4)) {
        if (n != 4) return fail(s, "invalid screen leave");
        release(s); s->focused = false; s->cb.focus(s->cb.arg, false);
    } else if (!memcmp(p, "DKDN", 4) || !memcmp(p, "DKUP", 4) || !memcmp(p, "DKDL", 4)) {
        bool language = !memcmp(p, "DKDL", 4);
        if ((!language && n != 10) || (language && (n < 14 || (uint64_t)be32(p+10) + 14 != n)))
            return fail(s, "invalid keyboard packet");
        if (!key_event(s, p, memcmp(p, "DKUP", 4) != 0)) return false;
    } else if (!memcmp(p, "DKRP", 4)) {
        // The target USB host performs repeat while the HID key remains held.
        if (n != 12 && (n < 16 || (uint64_t)be32(p+12) + 16 != n)) return fail(s, "invalid key repeat");
    } else if (!memcmp(p, "DMMV", 4) || !memcmp(p, "DMRM", 4)) {
        if (n != 8) return fail(s, "invalid pointer movement");
        bool relative = !memcmp(p, "DMRM", 4);
        if (relative) s->relative_moves++; else s->absolute_moves++;
        if (!s->focused || !s->info_acked) s->ignored_moves++;
        if (s->focused && s->info_acked) {
            int x = (int16_t)be16(p+4), y = (int16_t)be16(p+6);
            // A standard relative USB mouse works on Android as well as desktops.
            // Deskflow's absolute positions are in the advertised screen's pixels.
            int dx = relative ? x : x - s->pointer_x;
            int dy = relative ? y : y - s->pointer_y;
            s->pointer_x = relative ? s->pointer_x + x : x;
            s->pointer_y = relative ? s->pointer_y + y : y;
            s->pointer.id = 2;
            // Coordinate differences can exceed int16_t; preserve the full delta.
            do {
                int step_x = dx > 32767 ? 32767 : dx < -32768 ? -32768 : dx;
                int step_y = dy > 32767 ? 32767 : dy < -32768 ? -32768 : dy;
                s->pointer.x = step_x; s->pointer.y = step_y;
                pointer(s); dx -= step_x; dy -= step_y;
            } while (dx || dy);
        }
    } else if (!memcmp(p, "DMDN", 4) || !memcmp(p, "DMUP", 4)) {
        if (n != 5 || !p[4] || p[4] > 8) return fail(s, "invalid mouse button");
        if (s->focused && p[4] <= 5) {
            // Deskflow: left, middle, right. USB HID: left, right, middle.
            static const uint8_t bits[] = {0,1,4,2,8,16};
            if (!memcmp(p, "DMDN", 4)) s->pointer.buttons |= bits[p[4]];
            else s->pointer.buttons &= ~bits[p[4]];
            pointer(s);
        }
    } else if (!memcmp(p, "DMWM", 4)) {
        if (n != 8) return fail(s, "invalid mouse wheel");
        if (s->focused) {
            s->wheel_x += (int16_t)be16(p+4); s->wheel_y += (int16_t)be16(p+6);
            while (s->wheel_x / 120 || s->wheel_y / 120) {
                int x = s->wheel_x/120, y = s->wheel_y/120;
                if (x > 127) x = 127;
                if (x < -127) x = -127;
                if (y > 127) y = 127;
                if (y < -127) y = -127;
                s->pointer.pan = x; s->pointer.wheel = y;
                s->wheel_x -= x*120; s->wheel_y -= y*120;
                pointer(s);
            }
        }
    } else if (!memcmp(p, "CBYE", 4)) return fail(s, "server closed connection");
    else if (p[0] == 'E') return fail(s, "server refused client (check screen name/version)");
    else if (memcmp(p, "CNOP", 4) && memcmp(p, "CROP", 4) && memcmp(p, "CCLP", 4) &&
             memcmp(p, "DCLP", 4) && memcmp(p, "DFTR", 4) && memcmp(p, "DDRG", 4) &&
             memcmp(p, "CSEC", 4) && memcmp(p, "SECN", 4) && memcmp(p, "LSYN", 4))
        return fail(s, "unknown server message");
    return !was_ready || send(s, (const uint8_t *)"CNOP", 4);
}
void deskflow_session_init(deskflow_session_t *s, const deskflow_config_t *profile, const deskflow_callbacks_t *cb)
{
    memset(s, 0, sizeof(*s)); s->profile = *profile; s->cb = *cb;
    s->keepalive_ms = 15000; s->pointer.id = 2;
}
void deskflow_session_close(deskflow_session_t *s)
{
    release(s); s->focused = false; s->ready = false;
    s->cb.focus(s->cb.arg, false); s->cb.ready(s->cb.arg, false);
}
bool deskflow_session_feed(deskflow_session_t *s, const uint8_t *data, size_t length)
{
    while (length) {
        if (s->header_length < 4) {
            s->header[s->header_length++] = *data++; length--;
            if (s->header_length < 4) continue;
            s->wanted = be32(s->header); s->received = 0;
            if (s->wanted < 4 || s->wanted > 4*1024*1024) return fail(s, "invalid frame length");
        }
        size_t n = s->wanted - s->received;
        if (n > length) n = length;
        if (s->received < sizeof(s->data)) {
            size_t keep = sizeof(s->data) - s->received;
            if (keep > n) keep = n;
            memcpy(s->data + s->received, data, keep);
        }
        s->received += n; data += n; length -= n;
        if (s->received == s->wanted) {
            if (s->wanted > sizeof(s->data)) {
                if (memcmp(s->data, "DCLP", 4) && memcmp(s->data, "DFTR", 4) &&
                    memcmp(s->data, "DDRG", 4) && memcmp(s->data, "LSYN", 4))
                    return fail(s, "oversized control message");
            } else if (!command(s, s->data, s->wanted)) return false;
            s->header_length = 0;
        }
    }
    return true;
}

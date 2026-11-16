#include "network_config.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n-1])) s[--n] = 0;
    return s;
}
static bool copy(char *out, size_t cap, const char *in)
{
    size_t n = strlen(in);
    if (n >= cap) return false;
    memcpy(out, in, n + 1);
    return true;
}
static bool boolean(const char *s, bool *value)
{
    if (!strcmp(s, "true") || !strcmp(s, "1") || !strcmp(s, "yes")) *value = true;
    else if (!strcmp(s, "false") || !strcmp(s, "0") || !strcmp(s, "no")) *value = false;
    else return false;
    return true;
}
static bool number(const char *s, uint16_t *out, unsigned min, unsigned max)
{
    char *end;
    unsigned long value = strtoul(s, &end, 10);
    if (!*s || *end || value < min || value > max) return false;
    *out = value;
    return true;
}
static int hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static bool fingerprint(const char *s, uint8_t out[32])
{
    size_t n = 0;
    int high = -1;
    for (; *s; s++) {
        if (*s == ':' || *s == ' ') continue;
        int value = hex(*s);
        if (value < 0 || n >= 32) return false;
        if (high < 0) high = value;
        else { out[n++] = high * 16 + value; high = -1; }
    }
    return n == 32 && high == -1;
}
bool network_config_parse(const char *text, size_t length, network_config_t *out,
    unsigned *error_line, const char **error)
{
    memset(out, 0, sizeof(*out));
    *error_line = 1;
    *error = "invalid INI";
    if (length > NETWORK_CONFIG_MAX || memchr(text, 0, length)) {
        *error = "configuration too large or contains NUL bytes";
        return false;
    }
    network_profile_t *p = NULL;
    unsigned section_lines[NETWORK_PROFILE_MAX] = {0};
    size_t position = length >= 3 && !memcmp(text, "\xef\xbb\xbf", 3) ? 3 : 0;
    unsigned line_number = 0;
    while (position < length) {
        line_number++;
        *error_line = line_number;
        size_t start = position;
        while (position < length && text[position] != '\n') position++;
        size_t n = position - start;
        if (position < length) position++;
        char line[512];
        if (n >= sizeof(line)) { *error = "line too long"; return false; }
        memcpy(line, text + start, n); line[n] = 0;
        char *s = trim(line);
        if (!*s || *s == ';' || *s == '#') continue;
        if (*s == '[') {
            char *end = strrchr(s, ']');
            if (!end || *trim(end + 1)) return false;
            *end = 0;
            char *ssid = trim(s + 1);
            if (!*ssid || strlen(ssid) > 32 || out->count == NETWORK_PROFILE_MAX) {
                *error = "invalid SSID or too many networks"; return false;
            }
            for (size_t i = 0; i < out->count; i++)
                if (!strcmp(ssid, out->profiles[i].wifi.ssid)) {
                    *error = "duplicate SSID section"; return false;
                }
            section_lines[out->count] = line_number;
            p = &out->profiles[out->count++];
            copy(p->wifi.ssid, sizeof(p->wifi.ssid), ssid);
            copy(p->deskflow.screen, sizeof(p->deskflow.screen), "esp-bridge");
            p->deskflow.port = 24800; p->deskflow.width = 1920; p->deskflow.height = 1080;
            p->wifi.enabled = true; p->deskflow.auth.enabled = true;
            continue;
        }
        char *equal = strchr(s, '=');
        if (!p || !equal) return false;
        *equal = 0;
        char *key = trim(s), *value = trim(equal + 1);
        n = strlen(value);
        if (n && (value[0] == '"' || value[0] == '\'')) {
            if (n < 2 || value[n-1] != value[0]) return false;
            value[n-1] = 0; value++;
        }
        bool ok = false;
        if (!strcmp(key, "password")) ok = copy(p->wifi.password, sizeof(p->wifi.password), value);
        else if (!strcmp(key, "server")) ok = copy(p->deskflow.server, sizeof(p->deskflow.server), value);
        else if (!strcmp(key, "screen")) ok = *value && copy(p->deskflow.screen, sizeof(p->deskflow.screen), value);
        else if (!strcmp(key, "port")) ok = number(value, &p->deskflow.port, 1, 65535);
        else if (!strcmp(key, "width")) ok = number(value, &p->deskflow.width, 1, 32767);
        else if (!strcmp(key, "height")) ok = number(value, &p->deskflow.height, 1, 32767);
        else if (!strcmp(key, "enabled")) ok = boolean(value, &p->wifi.enabled);
        else if (!strcmp(key, "tls")) ok = boolean(value, &p->deskflow.auth.enabled);
        else if (!strcmp(key, "client_cert")) ok = copy(p->deskflow.auth.client_cert, sizeof(p->deskflow.auth.client_cert), value);
        else if (!strcmp(key, "client_key")) ok = copy(p->deskflow.auth.client_key, sizeof(p->deskflow.auth.client_key), value);
        else if (!strcmp(key, "server_fingerprint")) {
            ok = fingerprint(value, p->deskflow.auth.fingerprint); p->deskflow.auth.have_fingerprint = ok;
        } else if (!strcmp(key, "layout")) {
            ok = !strcmp(value, "us") || !strcmp(value, "se");
            p->deskflow.swedish = !strcmp(value, "se");
        } else { *error = "unknown setting"; return false; }
        if (!ok) { *error = "invalid setting value"; return false; }
    }
    for (size_t i = 0; i < out->count; i++) {
        p = &out->profiles[i];
        if (!p->wifi.enabled) continue;
        *error_line = section_lines[i];
        size_t n = strlen(p->wifi.password);
        if (n && n < 8) { *error = "Wi-Fi password must be empty or at least 8 bytes"; return false; }
        if (n == 64) for (size_t j = 0; j < n; j++)
            if (hex(p->wifi.password[j]) < 0) { *error = "64-byte Wi-Fi password must be a hexadecimal PSK"; return false; }
        if (p->deskflow.server[0] && p->deskflow.auth.enabled && !p->deskflow.auth.have_fingerprint) {
            *error = "TLS requires server_fingerprint"; return false;
        }
        if (!!p->deskflow.auth.client_cert[0] != !!p->deskflow.auth.client_key[0]) {
            *error = "client_cert and client_key must be specified together"; return false;
        }
    }
    *error = NULL;
    *error_line = 0;
    return true;
}

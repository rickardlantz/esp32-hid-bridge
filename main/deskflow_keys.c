#include "deskflow_keys.h"
#include <string.h>

deskflow_key_t deskflow_key_map(uint16_t key, bool se)
{
    deskflow_key_t out = {0};
    if (key >= 'a' && key <= 'z') out.usage = key - 'a' + 4;
    else if (key >= 'A' && key <= 'Z') { out.usage = key - 'A' + 4; out.modifiers = 2; }
    else if (key >= '1' && key <= '9') out.usage = key - '1' + 0x1e;
    else if (key == '0') out.usage = 0x27;
    else if (key == ' ') out.usage = 0x2c;
    else if (key >= 0xefbe && key <= 0xefc9) out.usage = key - 0xefbe + 0x3a;
    else if (key >= 0xefca && key <= 0xefd5) out.usage = key - 0xefca + 0x68;
    else if (key >= 0xefb1 && key <= 0xefb9) out.usage = key - 0xefb1 + 0x59;
    else switch (key) {
    case 0xef08: out.usage = 0x2a; break;
    case 0xef09: case 0xef89: out.usage = 0x2b; break;
    case 0xef0a: case 0xef0d: out.usage = 0x28; break;
    case 0xef1b: out.usage = 0x29; break;
    case 0xef13: out.usage = 0x48; break;
    case 0xef14: out.usage = 0x47; break;
    case 0xef50: out.usage = 0x4a; break;
    case 0xef51: out.usage = 0x50; break;
    case 0xef52: out.usage = 0x52; break;
    case 0xef53: out.usage = 0x4f; break;
    case 0xef54: out.usage = 0x51; break;
    case 0xef55: out.usage = 0x4b; break;
    case 0xef56: out.usage = 0x4e; break;
    case 0xef57: out.usage = 0x4d; break;
    case 0xef61: case 0xef15: out.usage = 0x46; break;
    case 0xef63: out.usage = 0x49; break;
    case 0xefff: out.usage = 0x4c; break;
    case 0xef67: out.usage = 0x65; break;
    case 0xef7f: out.usage = 0x53; break;
    case 0xef8d: out.usage = 0x58; break;
    case 0xefaa: out.usage = 0x55; break;
    case 0xefab: out.usage = 0x57; break;
    case 0xefad: out.usage = 0x56; break;
    case 0xefae: case 0xefac: out.usage = 0x63; break;
    case 0xefaf: out.usage = 0x54; break;
    case 0xefb0: out.usage = 0x62; break;
    case 0xefbd: out.usage = 0x67; break;
    case 0xef95: out.usage = 0x5f; break;
    case 0xef96: out.usage = 0x5c; break;
    case 0xef97: out.usage = 0x60; break;
    case 0xef98: out.usage = 0x5e; break;
    case 0xef99: out.usage = 0x5a; break;
    case 0xef9a: out.usage = 0x61; break;
    case 0xef9b: out.usage = 0x5b; break;
    case 0xef9c: out.usage = 0x59; break;
    case 0xef9d: out.usage = 0x5d; break;
    case 0xef9e: out.usage = 0x62; break;
    case 0xef9f: out.usage = 0x63; break;
    case 0xefe1: out.usage = 0xe1; break;
    case 0xefe2: out.usage = 0xe5; break;
    case 0xefe3: out.usage = 0xe0; break;
    case 0xefe4: out.usage = 0xe4; break;
    case 0xefe5: out.usage = 0x39; break;
    case 0xefe7: case 0xefeb: out.usage = 0xe3; break;
    case 0xefe8: case 0xefec: out.usage = 0xe7; break;
    case 0xefe9: out.usage = 0xe2; break;
    case 0xefea: case 0xef7e: out.usage = 0xe6; break;
    default: break;
    }
    if (out.usage) return out;
    const char *plain = "-=[]\\;'`,./";
    const char *shifted = "_+{}|:\"~<>?";
    static const uint8_t usages[] = {0x2d,0x2e,0x2f,0x30,0x31,0x33,0x34,0x35,0x36,0x37,0x38};
    if (!se && key < 128) {
        const char *p = strchr(plain, key);
        if (p && key) out.usage = usages[p-plain];
        p = strchr(shifted, key);
        if (p && key) { out.usage = usages[p-shifted]; out.modifiers = 2; }
        const char *numbers = "!@#$%^&*()";
        p = strchr(numbers, key);
        if (p && key) { out.usage = 0x1e + (p - numbers); out.modifiers = 2; }
    } else if (se) {
        switch (key) {
        case 0x00e5: out.usage = 0x2f; break;
        case 0x00c5: out.usage = 0x2f; out.modifiers = 2; break;
        case 0x00f6: out.usage = 0x33; break;
        case 0x00d6: out.usage = 0x33; out.modifiers = 2; break;
        case 0x00e4: out.usage = 0x34; break;
        case 0x00c4: out.usage = 0x34; out.modifiers = 2; break;
        case '+': out.usage = 0x2d; break;
        case '?': out.usage = 0x2d; out.modifiers = 2; break;
        case '\'': out.usage = 0x31; break;
        case '*': out.usage = 0x31; out.modifiers = 2; break;
        case ',': out.usage = 0x36; break;
        case ';': out.usage = 0x36; out.modifiers = 2; break;
        case '.': out.usage = 0x37; break;
        case ':': out.usage = 0x37; out.modifiers = 2; break;
        case '-': out.usage = 0x38; break;
        case '_': out.usage = 0x38; out.modifiers = 2; break;
        case '<': out.usage = 0x64; break;
        case '>': out.usage = 0x64; out.modifiers = 2; break;
        case '|': out.usage = 0x64; out.modifiers = 0x40; break;
        case '\\': out.usage = 0x2d; out.modifiers = 0x40; break;
        case '@': out.usage = 0x1f; out.modifiers = 0x40; break;
        case '$': out.usage = 0x21; out.modifiers = 0x40; break;
        case '{': out.usage = 0x24; out.modifiers = 0x40; break;
        case '[': out.usage = 0x25; out.modifiers = 0x40; break;
        case ']': out.usage = 0x26; out.modifiers = 0x40; break;
        case '}': out.usage = 0x27; out.modifiers = 0x40; break;
        case 0x00a7: out.usage = 0x35; break;
        case 0x00a4: out.usage = 0x21; out.modifiers = 2; break;
        default: {
            const char *numbers = "!\"#\xa4%&/()=";
            const char *p = key < 128 && key ? strchr(numbers, key) : NULL;
            if (p) { out.usage = 0x1e + (p - numbers); out.modifiers = 2; }
            break;
        }
        }
    }
    return out;
}

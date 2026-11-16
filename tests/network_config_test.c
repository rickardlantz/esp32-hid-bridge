#include "network_config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool parse(const char *s, network_config_t *out)
{
    unsigned line; const char *error;
    bool ok = network_config_parse(s, strlen(s), out, &line, &error);
    assert(ok ? error == NULL : error != NULL);
    return ok;
}
int main(void)
{
    network_config_t config;
    assert(parse("\xef\xbb\xbf[Home WiFi]\r\npassword=\"p#; assword\"\r\nserver=192.168.1.10\r\ntls=false\r\n"
        "screen=usb-machine\r\nlayout=se\r\n[Work]\nenabled=false\n", &config));
    assert(config.count == 2 && !strcmp(config.profiles[0].wifi.ssid, "Home WiFi"));
    assert(!strcmp(config.profiles[0].wifi.password, "p#; assword"));
    assert(!strcmp(config.profiles[0].deskflow.server, "192.168.1.10"));
    assert(config.profiles[0].deskflow.swedish && !config.profiles[0].deskflow.auth.enabled);
    assert(!config.profiles[1].wifi.enabled);
    assert(!parse("[Home]\nserver=example.com\n", &config)); // No implicit unverified TLS.
    assert(parse("[Home]\nserver=example.com\nserver_fingerprint="
        "AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99\n", &config));
    assert(config.profiles[0].deskflow.auth.have_fingerprint);
    assert(config.profiles[0].deskflow.auth.fingerprint[0] == 0xaa);
    assert(!parse("[Home]\npassword=short\n", &config));
    assert(!parse("[Home]\n[Home]\n", &config));
    assert(!parse("[Home]\nwidth=0\n", &config));
    assert(!parse("[Home]\nport=65536\n", &config));
    assert(!parse("[Home]\nunknown=secret\n", &config));
    assert(!parse("[Home]\npassword=\"unterminated\n", &config));
    assert(parse("; Disabled on first boot\n", &config) && config.count == 0);
    puts("INI configuration tests passed");
}

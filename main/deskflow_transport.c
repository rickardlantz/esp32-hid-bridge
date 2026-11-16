#include "deskflow_transport.h"
#include "server_auth.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"
static const char *TAG = "deskflow_tcp";
struct deskflow_transport {
    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    bool tls;
    const deskflow_config_t *profile;
    deskflow_alive_fn alive;
    void *alive_arg;
    bool want_write;
    server_identity_t identity;
};
static void close_connection(deskflow_transport_t *c)
{
    mbedtls_ssl_free(&c->ssl); mbedtls_ssl_config_free(&c->config); mbedtls_net_free(&c->net);
    server_identity_free(&c->identity);
}
static bool wait_socket(deskflow_transport_t *c, bool writing, uint32_t deadline_ms)
{
    while ((int32_t)(deadline_ms - (uint32_t)(esp_timer_get_time()/1000)) > 0 && c->alive(c->alive_arg)) {
        fd_set fds; FD_ZERO(&fds); FD_SET(c->net.fd, &fds);
        struct timeval timeout = {.tv_usec = 100000};
        int ready = select(c->net.fd + 1, writing ? NULL : &fds, writing ? &fds : NULL, NULL, &timeout);
        if (ready > 0) return true;
        if (ready < 0 && errno != EINTR) return false;
    }
    return false;
}
static bool connect_to_server(deskflow_transport_t *c, const deskflow_config_t *p, deskflow_alive_fn alive, void *arg)
{
    memset(c, 0, sizeof(*c)); c->profile = p; c->alive = alive; c->alive_arg = arg; c->tls = p->auth.enabled;
    mbedtls_net_init(&c->net); mbedtls_ssl_init(&c->ssl); mbedtls_ssl_config_init(&c->config);
    mbedtls_x509_crt_init(&c->identity.certificate); mbedtls_pk_init(&c->identity.key);
    char port[6]; snprintf(port, sizeof(port), "%u", p->port);
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM}, *addresses = NULL;
    if (getaddrinfo(p->server, port, &hints, &addresses)) return false;
    for (struct addrinfo *a = addresses; a; a = a->ai_next) {
        c->net.fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (c->net.fd < 0) continue;
        fcntl(c->net.fd, F_SETFL, O_NONBLOCK);
        int err = connect(c->net.fd, a->ai_addr, a->ai_addrlen);
        if (err == 0 || (errno == EINPROGRESS && wait_socket(c, true, esp_timer_get_time()/1000 + 5000))) {
            int error = 0; socklen_t n = sizeof(error);
            if (!getsockopt(c->net.fd, SOL_SOCKET, SO_ERROR, &error, &n) && !error) break;
        }
        close(c->net.fd); c->net.fd = -1;
    }
    freeaddrinfo(addresses);
    if (c->net.fd < 0) return false;
    int yes = 1; setsockopt(c->net.fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    if (!p->auth.enabled) return true;
    if (!p->auth.have_fingerprint || psa_crypto_init() != PSA_SUCCESS) return false;
    if (mbedtls_ssl_config_defaults(&c->config, MBEDTLS_SSL_IS_CLIENT,
            MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) return false;
    // Authentication is an explicit SHA-256 certificate pin below; no clock,
    // public CA, hostname exception, or trust-on-first-use is needed.
    mbedtls_ssl_conf_authmode(&c->config, MBEDTLS_SSL_VERIFY_NONE);
    uint64_t identity_version;
    if (!server_identity_load(&p->auth, &c->identity, &identity_version)) return false;
    if (p->auth.client_cert[0] && mbedtls_ssl_conf_own_cert(&c->config, &c->identity.certificate, &c->identity.key)) return false;
    if (mbedtls_ssl_setup(&c->ssl, &c->config) || mbedtls_ssl_set_hostname(&c->ssl, p->server)) return false;
    mbedtls_ssl_set_bio(&c->ssl, &c->net, mbedtls_net_send, mbedtls_net_recv, NULL);
    uint32_t deadline = esp_timer_get_time()/1000 + 10000;
    int result;
    while ((result = mbedtls_ssl_handshake(&c->ssl)) != 0) {
        if ((result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) ||
            !wait_socket(c, result == MBEDTLS_ERR_SSL_WANT_WRITE, deadline)) return false;
    }
    const mbedtls_x509_crt *certificate = mbedtls_ssl_get_peer_cert(&c->ssl);
    uint8_t hash[32]; size_t length = 0;
    if (!certificate || psa_hash_compute(PSA_ALG_SHA_256, certificate->raw.p, certificate->raw.len,
            hash, sizeof(hash), &length) != PSA_SUCCESS || length != 32) return false;
    unsigned difference = 0;
    for (size_t i = 0; i < sizeof(hash); i++) difference |= hash[i] ^ p->auth.fingerprint[i];
    if (difference) { ESP_LOGE(TAG, "Deskflow server certificate fingerprint mismatch"); return false; }
    return true;
}
bool deskflow_transport_send(void *arg, const uint8_t *data, size_t length)
{
    deskflow_transport_t *c = arg;
    uint32_t deadline = esp_timer_get_time()/1000 + 5000;
    while (length) {
        int n = c->tls ? mbedtls_ssl_write(&c->ssl, data, length) : send(c->net.fd, data, length, 0);
        if (n > 0) { data += n; length -= n; continue; }
        bool retry = c->tls ? (n == MBEDTLS_ERR_SSL_WANT_WRITE || n == MBEDTLS_ERR_SSL_WANT_READ) :
            (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR));
        if (!retry || !wait_socket(c, n != MBEDTLS_ERR_SSL_WANT_READ, deadline)) return false;
    }
    return true;
}

deskflow_transport_t *deskflow_transport_connect(const deskflow_config_t *config, deskflow_alive_fn alive, void *arg)
{
    deskflow_transport_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    if (connect_to_server(c, config, alive, arg)) return c;
    close_connection(c); free(c); return NULL;
}
int deskflow_transport_read(deskflow_transport_t *c, uint8_t *data, size_t capacity)
{
    int n = c->tls ? mbedtls_ssl_read(&c->ssl, data, capacity) : recv(c->net.fd, data, capacity, 0);
    if (n >= 0) return n;
    c->want_write = c->tls && n == MBEDTLS_ERR_SSL_WANT_WRITE;
    bool retry = c->tls ? (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) :
        (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR);
    return retry ? -2 : -1;
}
void deskflow_transport_wait(deskflow_transport_t *c)
{
    fd_set fds; FD_ZERO(&fds); FD_SET(c->net.fd, &fds);
    struct timeval timeout = {.tv_usec = 50000};
    select(c->net.fd+1, c->want_write ? NULL : &fds, c->want_write ? &fds : NULL, NULL, &timeout);
}
void deskflow_transport_close(deskflow_transport_t *c)
{
    if (c) { close_connection(c); free(c); }
}

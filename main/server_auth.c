#include "server_auth.h"
#include "config_storage.h"
#include <stdlib.h>
#include <string.h>
#include "psa/crypto.h"

#define IDENTITY_MAX 4096
void server_identity_free(server_identity_t *id)
{
    mbedtls_pk_free(&id->key); mbedtls_x509_crt_free(&id->certificate);
}
bool server_identity_load(const server_auth_t *auth, server_identity_t *id, uint64_t *version)
{
    mbedtls_x509_crt_init(&id->certificate); mbedtls_pk_init(&id->key);
    *version = 0;
    if (!auth->client_cert[0] && !auth->client_key[0]) return true;
    char *cert = calloc(1, IDENTITY_MAX + 1), *key = calloc(1, IDENTITY_MAX + 1);
    size_t cert_size = 0, key_size = 0;
    bool ok = cert && key && psa_crypto_init() == PSA_SUCCESS &&
        config_storage_file(auth->client_cert, cert, IDENTITY_MAX + 1, &cert_size) &&
        config_storage_file(auth->client_key, key, IDENTITY_MAX + 1, &key_size);
    if (ok) {
        ok = !mbedtls_x509_crt_parse(&id->certificate, (const unsigned char *)cert, cert_size+1) &&
            !mbedtls_pk_parse_key(&id->key, (const unsigned char *)key, key_size+1, NULL, 0) &&
            !mbedtls_pk_check_pair(&id->certificate.pk, &id->key);
    }
    if (ok) {
        uint64_t hash = 14695981039346656037ull;
        for (size_t i = 0; i < cert_size; i++) hash = (hash ^ (unsigned char)cert[i]) * 1099511628211ull;
        for (size_t i = 0; i < key_size; i++) hash = (hash ^ (unsigned char)key[i]) * 1099511628211ull;
        *version = hash;
    }
    if (key) memset(key, 0, IDENTITY_MAX + 1);
    free(cert); free(key);
    return ok;
}
bool server_auth_validate(server_auth_t *auth)
{
    server_identity_t identity;
    bool ok = server_identity_load(auth, &identity, &auth->material_version);
    server_identity_free(&identity);
    return ok;
}

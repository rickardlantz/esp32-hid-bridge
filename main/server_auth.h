#pragma once
#include "deskflow_config.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
typedef struct {
    mbedtls_x509_crt certificate;
    mbedtls_pk_context key;
} server_identity_t;

bool server_auth_validate(server_auth_t *auth);
bool server_identity_load(const server_auth_t *auth, server_identity_t *identity, uint64_t *version);
void server_identity_free(server_identity_t *identity);

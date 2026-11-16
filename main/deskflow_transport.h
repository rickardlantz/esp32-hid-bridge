#pragma once
#include "deskflow_config.h"

typedef struct deskflow_transport deskflow_transport_t;
typedef bool (*deskflow_alive_fn)(void *arg);
deskflow_transport_t *deskflow_transport_connect(const deskflow_config_t *config, deskflow_alive_fn alive, void *arg);
bool deskflow_transport_send(void *transport, const uint8_t *data, size_t length);
// Positive byte count, 0 for closed, -1 error, -2 would block.
int deskflow_transport_read(deskflow_transport_t *transport, uint8_t *data, size_t capacity);
void deskflow_transport_wait(deskflow_transport_t *transport);
void deskflow_transport_close(deskflow_transport_t *transport);

#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { uint8_t usage, modifiers; } deskflow_key_t;
deskflow_key_t deskflow_key_map(uint16_t key, bool swedish);

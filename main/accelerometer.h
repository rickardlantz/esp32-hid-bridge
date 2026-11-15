#pragma once
#include <stdbool.h>
typedef struct { int x, y, z; } acceleration_t;
typedef enum { ACCELEROMETER_WAIT, ACCELEROMETER_SAMPLE, ACCELEROMETER_ERROR } acceleration_result_t;

// Optional hardware boundary. No bus or pins are touched when disabled.
bool accelerometer_init(void);
acceleration_result_t accelerometer_read(acceleration_t *sample);

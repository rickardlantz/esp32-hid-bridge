#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ORIENTATION_PORTRAIT, ORIENTATION_LANDSCAPE,
    ORIENTATION_PORTRAIT_INVERTED, ORIENTATION_LANDSCAPE_INVERTED,
    ORIENTATION_COUNT
} orientation_t;

typedef struct {
    int width, height, x_gap, y_gap;
    bool swap_xy, mirror_x, mirror_y;
} display_geometry_t;

display_geometry_t orientation_geometry(orientation_t orientation);
const char *orientation_name(orientation_t orientation);

// Sensor-independent gravity policy. Inputs are screen-aligned gravity
// in milli-g: +X points right, +Y down, +Z out of the display. A backend must
// transform sensor axes to this coordinate system before supplying samples.
typedef struct {
    orientation_t current, candidate;
    uint32_t candidate_since;
    bool tracking;
} orientation_tracker_t;
bool orientation_sample(orientation_tracker_t *tracker, int x, int y, int z, uint32_t now_ms);

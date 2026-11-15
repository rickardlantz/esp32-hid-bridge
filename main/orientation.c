#include "orientation.h"
#include <stdlib.h>

display_geometry_t orientation_geometry(orientation_t o)
{
    bool landscape = o == ORIENTATION_LANDSCAPE || o == ORIENTATION_LANDSCAPE_INVERTED;
    return (display_geometry_t){
        .width = landscape ? 320 : 172, .height = landscape ? 172 : 320,
        .x_gap = landscape ? 0 : 34, .y_gap = landscape ? 34 : 0,
        .swap_xy = landscape,
        .mirror_x = o == ORIENTATION_LANDSCAPE || o == ORIENTATION_PORTRAIT_INVERTED,
        .mirror_y = o == ORIENTATION_PORTRAIT_INVERTED || o == ORIENTATION_LANDSCAPE_INVERTED,
    };
}
const char *orientation_name(orientation_t o)
{
    static const char *names[] = {"portrait", "landscape", "portrait-inverted", "landscape-inverted"};
    return o < ORIENTATION_COUNT && o >= 0 ? names[o] : "invalid";
}
bool orientation_sample(orientation_tracker_t *t, int x, int y, int z, uint32_t now)
{
    // Reject free fall, motion, face-up/down placement, and diagonal ambiguity.
    // No gravity direction is reliable in these conditions; keep the last view.
    if (x < -2000 || x > 2000 || y < -2000 || y > 2000 || z < -2000 || z > 2000) {
        t->tracking = false;
        return false;
    }
    int ax = abs(x), ay = abs(y), az = abs(z);
    int magnitude2 = x*x + y*y + z*z;
    if (magnitude2 < 750*750 || magnitude2 > 1250*1250 ||
        az > 800 || (ax > ay ? ax - ay : ay - ax) < 250) {
        t->tracking = false;
        return false;
    }
    orientation_t proposed = ax > ay ?
        (x > 0 ? ORIENTATION_LANDSCAPE : ORIENTATION_LANDSCAPE_INVERTED) :
        (y > 0 ? ORIENTATION_PORTRAIT : ORIENTATION_PORTRAIT_INVERTED);
    if (proposed == t->current) { t->tracking = false; return false; }
    if (!t->tracking || t->candidate != proposed) {
        t->candidate = proposed;
        t->candidate_since = now;
        t->tracking = true;
        return false;
    }
    if ((uint32_t)(now - t->candidate_since) < 500) return false;
    t->current = proposed;
    t->tracking = false;
    return true;
}

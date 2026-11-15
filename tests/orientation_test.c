#include "orientation.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

int main(void)
{
    orientation_tracker_t t = {.current = ORIENTATION_PORTRAIT};
    assert(!orientation_sample(&t, 1000, 0, 0, 0));
    assert(!orientation_sample(&t, 1000, 0, 0, 499));
    assert(orientation_sample(&t, 1000, 0, 0, 500));
    assert(t.current == ORIENTATION_LANDSCAPE);
    assert(!orientation_sample(&t, -1000, 0, 0, 600));
    assert(!orientation_sample(&t, 700, 700, 0, 1000)); // Diagonal cancels dwell.
    assert(!orientation_sample(&t, -1000, 0, 0, 1200));
    assert(orientation_sample(&t, -1000, 0, 0, 1700));
    assert(t.current == ORIENTATION_LANDSCAPE_INVERTED);
    assert(!orientation_sample(&t, 0, -1000, 0, 1800));
    assert(orientation_sample(&t, 0, -1000, 0, 2300));
    assert(t.current == ORIENTATION_PORTRAIT_INVERTED);
    assert(!orientation_sample(&t, 0, 0, 1000, 2400)); // Flat: retain last view.
    assert(!orientation_sample(&t, 0, 0, 0, 2500)); // Free fall.
    assert(!orientation_sample(&t, 2000, 0, 0, 2600)); // Motion/shock.
    assert(!orientation_sample(&t, INT32_MIN, 0, 0, 2700));
    assert(t.current == ORIENTATION_PORTRAIT_INVERTED);
    assert(!orientation_sample(&t, 0, 1000, 0, UINT32_MAX - 200));
    assert(orientation_sample(&t, 0, 1000, 0, 300)); // Millisecond clock wrap.
    assert(t.current == ORIENTATION_PORTRAIT);
    for (orientation_t o = 0; o < ORIENTATION_COUNT; o++) {
        display_geometry_t g = orientation_geometry(o);
        assert(g.width * g.height == 172 * 320);
        assert(g.swap_xy == (g.width == 320));
        assert((g.x_gap == 34) == !g.swap_xy);
        assert((g.y_gap == 34) == g.swap_xy);
    }
    puts("orientation policy tests passed");
}

#include "mouse_report.h"

static int step(int value) { return value > 127 ? 127 : value < -127 ? -127 : value; }
bool mouse_report_next(bridge_mouse_t *pending, uint8_t data[5])
{
    int x = step(pending->x), y = step(pending->y);
    data[0] = pending->buttons; data[1] = (uint8_t)x; data[2] = (uint8_t)y;
    data[3] = (uint8_t)pending->wheel; data[4] = (uint8_t)pending->pan;
    pending->x -= x; pending->y -= y;
    pending->wheel = pending->pan = 0;
    return pending->x || pending->y;
}

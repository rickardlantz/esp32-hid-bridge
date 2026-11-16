#include "mouse_report.h"
#include <assert.h>
#include <stdio.h>

static void movement(int x, int y)
{
    bridge_mouse_t pending = {.id=2,.buttons=5,.x=x,.y=y,.wheel=-3,.pan=2};
    int total_x=0, total_y=0, wheel=0, pan=0, reports=0;
    bool more;
    do {
        uint8_t data[5]; more=mouse_report_next(&pending,data);
        assert(data[0]==5);
        assert((int8_t)data[1]>=-127 && (int8_t)data[2]>=-127);
        total_x+=(int8_t)data[1]; total_y+=(int8_t)data[2];
        wheel+=(int8_t)data[3]; pan+=(int8_t)data[4];
        assert(++reports<260);
    } while(more);
    assert(total_x==x && total_y==y && wheel==-3 && pan==2);
}
int main(void)
{
    movement(0,0); movement(3,-2); movement(127,-127); movement(128,-128);
    movement(-32768,32767); movement(32767,-32768);
    puts("USB mouse packet splitting tests passed");
}

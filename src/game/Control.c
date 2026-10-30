#include "common.h"
void Control_ClearControl(void * control)
{
    memset(control, 0, 0x20);
}

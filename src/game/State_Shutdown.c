#include "common.h"


s32 StateShutdown_Init(void) {
    return 1;
}

s32 StateShutdown_Heartbeat(void) {
    return 0;
}

s32 StateShutdown_Shutdown(void) {
    return 1;
}


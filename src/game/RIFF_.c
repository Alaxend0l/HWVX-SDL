#include "common.h"


s32 GetID_(u8 *chunk) {
    s32 id = 0;
    s32 i;
    for (i = 0; i < 4; i++) {
        id = (id << 8) | chunk[i];
    }
    return id;
}

s32 GetType_(u8 *chunk) {
    u8 **p = &chunk;
    return (((((*p)[8] << 8) | (*p)[9]) << 8 | (*p)[10]) << 8) | (*p)[11];
}

/* The original calls these rather than inlining them. */


s8 CmpID_(u8 *chunk, s32 type) {
    return (s8)(GetID_(chunk) == type);
}

s8 CmpType_(u8 *chunk, s32 type) {
    return (s8)(GetType_(chunk) == type);
}


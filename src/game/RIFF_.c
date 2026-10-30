#include "common.h"


s32 GetType_(u8 *chunk);
s32 GetID_(u8 *chunk);
s8 CmpID_(u8 *chunk, s32 type) {
    return (s8)(GetID_(chunk) == type);
}

s8 CmpType_(u8 *chunk, s32 type) {
    return (s8)(GetType_(chunk) == type);
}

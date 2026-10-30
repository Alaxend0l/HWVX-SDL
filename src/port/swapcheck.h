/* Bookkeeping for byte-swapped data pack blobs, and (in a HWVX_SWAPCHECK
 * build) a checker that reports game reads disagreeing with how the bytes
 * were swapped. See swapcheck.c. */
#ifndef PORT_SWAPCHECK_H
#define PORT_SWAPCHECK_H

#include "port.h"

/* How each blob byte was swapped (src/game/Port_Swap.c). */
enum {
    CLAIM_DEFAULT = 0, /* swapped as part of a 32-bit word */
    CLAIM_DONE,        /* pointer slot (32-bit) */
    CLAIM_U32,
    CLAIM_U16,
    CLAIM_BYTES        /* left as it is */
};

/* Takes ownership of `claim` (one entry per blob byte, malloc'd). */
void PortSwapCheck_Register(u8 *base, u32 size, u8 *claim, const char *pack, s32 packType);
/* Heap memory being freed: forget blobs inside it. */
void PortSwapCheck_Forget(const void *p, u32 size);

#endif

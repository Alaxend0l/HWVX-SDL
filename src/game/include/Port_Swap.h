/* Byte-swapping of data packs on load (src/game/Port_Swap.c). Port-only. */
#ifndef PORT_SWAP_H
#define PORT_SWAP_H

#include "port.h"

/* Pack type (FileStreaming's data pack type) of the pack being loaded; picks
 * the walker for its root objects. */
extern s32 g_portSwapPackType;
extern const char *g_portSwapPackName;


/* SFX descriptors (0x20 each) in a pack's sound data; samples stay big-endian. */

#endif

/* Byte-swapping of data packs on load (src/game/Port_Swap.c). Port-only. */
#ifndef PORT_SWAP_H
#define PORT_SWAP_H

#include "port.h"

/* Pack type (FileStreaming's data pack type) of the pack being loaded; picks
 * the walker for its root objects. */
extern s32 g_portSwapPackType;
extern const char *g_portSwapPackName;

/* Texture-id relocations ({ slot offset, texture } pairs) whose slot holds
 * two u16s (GeometryGroup.textureOverride/texture): the swapper sets this
 * bit in the offset. The original stores the id zero-extended as a
 * big-endian u32, i.e. u16 0 at the slot and the id at slot + 2; flagged
 * slots get that layout, the others the plain u32 store. */
#define PORT_TEXRELOC_U16 0x80000000u

/* Number of texture-id relocations at the start of PortSwap_PackData's
 * relocTables (set by the loader before the call). */
extern u32 g_portSwapNumTextureRelocs;

static inline void PortSwap_StoreTextureID(u8 *base, u32 slot, u32 texID) {
    if (slot & PORT_TEXRELOC_U16) {
        slot &= ~PORT_TEXRELOC_U16;
        *(u16 *)(base + slot) = 0;
        *(u16 *)(base + slot + 2) = (u16)texID;
    } else {
        *(u32 *)(base + slot) = texID;
    }
}

/* The PortSwap_* functions are declared in game_protos.h (generated). */

/* FileManager_NGC.c: whose data the file is, PORT_DATA_* (port.h). */
s32 Port_FileFormat(s32 handle);

/* Port_Data.c: each load step for a file of format `fmt`. GameCube packs go
 * to the byte-swapper (PortSwap_*); the other releases' to their converters. */
void PortData_Words(s32 fmt, void *words, u32 count);
void PortData_TOC(s32 fmt, u32 *buf, u32 bufWords);
void PortData_PackHeader(s32 fmt, u32 *hdr, u32 numWords, u32 numRefs, u32 refSize);
void PortData_SoundPointers(s32 fmt, u32 *pointers, u32 count, u8 *soundBase);
void PortData_SFX(s32 fmt, u8 *sfx, u32 count);
void PortData_PackData(s32 fmt, u8 *relocTables, u32 relocTableWords, u8 *base, u32 dataSize,
                       u32 *texRelocs, u32 numTexRelocs, u32 *pointers, u32 numPointers,
                       u32 textureDataOffset, u32 numTextures, u32 shaderOffset, u32 numShaders,
                       u32 triggerOffset, u32 numTriggers);
void PortData_QuadPack(s32 fmt, u8 *data, u32 size, u32 index);

#endif

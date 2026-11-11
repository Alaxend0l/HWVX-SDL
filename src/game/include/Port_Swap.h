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
/* FileManager_NGC.c: move a file's position up to a multiple of `align`. */
void Port_FileAlign(s32 handle, u32 align);
/* The PS2's packs sit on 2048-byte sectors and pad their header and sound
 * data to whole sectors (DataPack_LoadDataPack skips the padding). */
#define PORT_PS2_SECTOR 0x800

/* Port_Data.c: each load step for a file of format `fmt`. GameCube packs go
 * to the byte-swapper (PortSwap_*); the other releases' to their converters. */
void PortData_Words(s32 fmt, void *words, u32 count);
void PortData_TOC(s32 fmt, u32 *buf, u32 bufWords);
void PortData_PackHeader(s32 fmt, u32 *hdr, u32 numWords, u32 numRefs, u32 refSize);
void PortData_SoundPointers(s32 fmt, u32 *pointers, u32 count, u8 *soundBase);
/* The pack's sound data (soundBase, soundDataSize), set before its banks and
 * samples are converted: the converters write in place, and every range they
 * take from the pack must lie inside it (PortData_InSound). */
void PortData_SoundData(u8 *base, u32 size);
int PortData_InSound(const void *p, u64 len);
/* A quad pack's tables (relocations, shaders, texture relocations, pointers)
 * and data inside its `size`; fatal otherwise. */
void PortData_CheckQuadTables(const u32 *hdr, u32 size, u32 index);
void PortData_SFX(s32 fmt, u8 *sfx, u32 count);
/* After CreateSFX has copied the pack's samples (PS2: frees the decoded ones). */
void PortData_SFXDone(s32 fmt);
/* A song bank as its pack loads; returns the bank to give SongManager_LoadSong:
 * GameCube/PC the same one, converted in place; PS2 a rebuilt copy, or NULL
 * if there is none. PortData_SongBankDone frees that copy once the song is
 * loaded (CreateMIDI copies the bank). */
u8 *PortData_SongBank(s32 fmt, u8 *bank);
void PortData_SongBankDone(s32 fmt);
void PortData_PackData(s32 fmt, u8 *relocTables, u32 relocTableWords, u8 *base, u32 dataSize,
                       u32 *texRelocs, u32 numTexRelocs, u32 *pointers, u32 numPointers,
                       u32 textureDataOffset, u32 numTextures, u32 shaderOffset, u32 numShaders,
                       u32 triggerOffset, u32 numTriggers);
void PortData_QuadPack(s32 fmt, u8 *data, u32 size, u32 index);

/* Port_Geometry.c: a relocated blob's GameCube geometry in the indexed
 * layout; returns the block holding it (freed with the pack) or NULL. */
void *PortGeometry_ConvertGC(u8 *base, u32 size, const u32 *pointers, u32 numPointers, const u32 *relocs,
                             u32 numRelocs);
/* The same for the PS2's geometry (VIF packets), after relocation; flags the
 * texture-id relocations ({slot, texture} pairs) at converted groups' 0x18
 * PORT_TEXRELOC_U16. Levels are marked PORT_LAYOUT_INDEXED_NOCULL. */
void *PortGeometry_ConvertPS2(u8 *base, u32 size, const u32 *pointers, u32 numPointers, u32 *texIdRelocs,
                              u32 numTexIdRelocs);
/* Converted data kept by its owner, freed when the owner is released: a
 * quadrant pack's geometry (WorldQuad has no field to spare: all of it is
 * world data), a PS2 pack's expanded textures (Port_PS2.c). */
void PortGeometry_Keep(void *owner, void *block);
void PortGeometry_Release(void *owner);

/* Port_PS2.c: the PS2 release's textures and sounds. PortPS2_Textures runs
 * after relocation: PSMT8 textures expanded through their CLUTs into the
 * returned block (kept with the pack), every header made the PC's raw
 * RGBA8888 (PORT_TEX_PC | 1). PortPS2_SFX decodes a pack's SPU-ADPCM
 * samples to PCM16 in a block PortPS2_SFXDone frees; PortPS2_SongBank
 * rebuilds a bank in the GameCube's form (NULL if not one), freed by
 * PortPS2_SongBankDone. */
void *PortPS2_Textures(u8 *base, u32 size, u32 texOff, u32 numTex, u32 palOff, u32 numPal);
void PortPS2_SFX(u8 *desc, u32 count);
void PortPS2_SFXDone(void);
u8 *PortPS2_SongBank(const u8 *bank);
void PortPS2_SongBankDone(void);

#endif

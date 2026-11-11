/* Loading the releases' data packs: one entry per load step, by the file's
 * format (PORT_DATA_*, port.h). Port-only file.
 *
 * The GameCube, PC and PS2 releases share BrainCore's pack format (TOC, pack
 * header, relocation and pointer tables, the object graph) and the file names
 * (dvd.c finds a pack under any release's name). What differs:
 *
 * - GameCube (.xgc): big-endian; every step goes to the byte-swapper
 *   (Port_Swap.c). Geometry and textures are GX's, which the port draws.
 * - PC (.xpc): little-endian, so the tables and the object graph are already
 *   in host order. Sound samples are PCM16 LE; the mixer reads them
 *   big-endian, as from ARAM, so they're swapped here. Geometry (triangle
 *   lists) and textures (DXT) are not GX's: see PC_PackData.
 * - PS2 (.xps): little-endian too; not loadable yet (dvd.c doesn't look for
 *   it). Its steps go in the PORT_DATA_PS2 cases.
 *
 * docs/beta-cars/research/pc_formats.md (HWVX-X-FILE-EDITOR) has the PC
 * formats. */
#include "common.h"
#include "Voice.h"
#include "Port_Swap.h"

static void Unsupported(s32 fmt) {
    Port_Fatal("data format %d is not supported yet", (int)fmt);
}

/* Words read straight from a file (TOC entries and such). */
void PortData_Words(s32 fmt, void *words, u32 count) {
    switch (fmt) {
    case PORT_DATA_GC:
        Port_SwapArray32(words, count);
        break;
    case PORT_DATA_PC:
        break;
    default:
        Unsupported(fmt);
    }
}

void PortData_TOC(s32 fmt, u32 *buf, u32 bufWords) {
    switch (fmt) {
    case PORT_DATA_GC:
        PortSwap_TOC(buf, bufWords);
        break;
    case PORT_DATA_PC:
        break;
    default:
        Unsupported(fmt);
    }
}

void PortData_PackHeader(s32 fmt, u32 *hdr, u32 numWords, u32 numRefs, u32 refSize) {
    switch (fmt) {
    case PORT_DATA_GC:
        PortSwap_PackHeader(hdr, numWords, numRefs, refSize);
        break;
    case PORT_DATA_PC:
        break;
    default:
        Unsupported(fmt);
    }
}

/* The sound data's pointer table, and the slots it lists (blob offsets,
 * relocated by the caller afterwards). */
void PortData_SoundPointers(s32 fmt, u32 *pointers, u32 count, u8 *soundBase) {
    u32 i;

    switch (fmt) {
    case PORT_DATA_GC:
        Port_SwapArray32(pointers, count);
        for (i = 0; i < count; i++) {
            u32 *slot = (u32 *)(soundBase + pointers[i]);
            *slot = BE32(*slot);
        }
        break;
    case PORT_DATA_PC:
        break;
    default:
        Unsupported(fmt);
    }
}

/* PC: the samples of `count` SFX descriptors (relocated: data is a handle)
 * to big-endian, once each, for the mixer (Port_ReadBE16). */
static void PC_SFXSamples(u8 *desc, u32 count) {
    SFX *sfx = (SFX *)desc;
    u32 i, j, n;

    for (i = 0; i < count; i++) {
        if (sfx[i].bits != 16 || sfx[i].data == 0) {
            continue;
        }
        for (j = 0; j < i && sfx[j].data != sfx[i].data; j++) {
        }
        if (j < i) {
            continue;   /* shared with an earlier descriptor: already swapped */
        }
        n = sfx[i].length / 2;
        Port_SwapArray16(PTR(sfx[i].data), n);
    }
}

void PortData_SFX(s32 fmt, u8 *sfx, u32 count) {
    switch (fmt) {
    case PORT_DATA_GC:
        PortSwap_SFXDescriptors(sfx, count);
        break;
    case PORT_DATA_PC:
        PC_SFXSamples(sfx, count);
        break;
    default:
        Unsupported(fmt);
    }
}

/* PC: texture headers, 0x10 each and packed (the GameCube pads them to 0x20;
 * DataPack_RefreshTextures steps by the pack's format): u16 format, width,
 * height, mip levels (0 or the full chain), then the pixels' pointer slot.
 * LoadBTF_RAM reads the u16s byte-reversed (__sthbrx), as the byte-swapper
 * leaves the GameCube's, so they're stored that way; the format becomes
 * PORT_TEX_PC | format, which gx_tex.c decodes (port.h). */
static void PC_Textures(u8 *base, u32 dataSize, u32 off, u32 count) {
    u32 i, k;

    for (i = 0; i < count && off + 0x10 <= dataSize; i++, off += 0x10) {
        u16 *h = (u16 *)(base + off);

        h[0] = (u16)(PORT_TEX_PC | h[0]);
        for (k = 0; k < 4; k++) {
            h[k] = (u16)(h[k] >> 8 | h[k] << 8);
        }
    }
}

/* PC: what the GameCube code can't read in a PC pack's blob. Tables and the
 * object graph are already in host order. Still to convert: geometry (u16
 * triangle lists and unified vertex arrays; GX's are display lists). */
static void PC_PackData(u8 *base, u32 dataSize, u32 textureDataOffset, u32 numTextures) {
    if (numTextures != 0) {
        PC_Textures(base, dataSize, textureDataOffset, numTextures);
    }
}

void PortData_PackData(s32 fmt, u8 *relocTables, u32 relocTableWords, u8 *base, u32 dataSize,
                       u32 *texRelocs, u32 numTexRelocs, u32 *pointers, u32 numPointers,
                       u32 textureDataOffset, u32 numTextures, u32 shaderOffset, u32 numShaders,
                       u32 triggerOffset, u32 numTriggers) {
    switch (fmt) {
    case PORT_DATA_GC:
        PortSwap_PackData(relocTables, relocTableWords, base, dataSize, texRelocs, numTexRelocs, pointers,
                          numPointers, textureDataOffset, numTextures, shaderOffset, numShaders, triggerOffset,
                          numTriggers);
        break;
    case PORT_DATA_PC:
        PC_PackData(base, dataSize, textureDataOffset, numTextures);
        break;
    default:
        Unsupported(fmt);
    }
}

/* A world quadrant's geometry (DataPack_LoadQuadPack). */
void PortData_QuadPack(s32 fmt, u8 *data, u32 size, u32 index) {
    switch (fmt) {
    case PORT_DATA_GC:
        PortSwap_QuadPack(data, size, index);
        break;
    case PORT_DATA_PC:
        break;
    default:
        Unsupported(fmt);
    }
}

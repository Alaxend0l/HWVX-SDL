/* Loading the releases' data packs: one entry per load step, by the file's
 * format (PORT_DATA_*, port.h). Port-only file.
 *
 * The GameCube, PC and PS2 releases share BrainCore's pack format (TOC, pack
 * header, relocation and pointer tables, the object graph) and the file names
 * (dvd.c finds a pack under any release's name). What differs:
 *
 * - GameCube (.xgc): big-endian; every step goes to the byte-swapper
 *   (Port_Swap.c), and the PCM16 samples of sound effects and song banks
 *   are swapped here. Geometry and textures are GX's, which the port draws.
 * - PC (.xpc): little-endian, so the tables, the object graph and the
 *   samples are already in host order. Geometry (triangle lists) and
 *   textures (DXT) are not GX's: see PC_PackData.
 * - PS2 (.xps, under XPS/ENGLISH/): little-endian too, the PC's tables and
 *   object graph; the header and sound data padded to 2048-byte sectors
 *   (DataPack_LoadDataPack skips that). Geometry (VIF packets) and textures
 *   (PSMT8 + CLUTs) are converted after relocation, in DataPack_LoadDataPack
 *   and DataPack_LoadQuadPack (Port_Geometry.c, Port_PS2.c); SPU-ADPCM sound
 *   effects and song banks are decoded to PCM16 here, as they load.
 *
 * Sound samples are kept in host order, whichever release they come from:
 * the mixer (ax.c) reads them as plain s16.
 *
 * docs/beta-cars/research/pc_formats.md (HWVX-X-FILE-EDITOR) has the PC
 * formats, docs/ps2-version/README.md the PS2's. */
#include "common.h"
#include "Voice.h"
#include "Gfx.h"
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
    case PORT_DATA_PS2:   /* little-endian too */
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
    case PORT_DATA_PS2:   /* little-endian too */
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
    case PORT_DATA_PS2:   /* little-endian too */
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
    case PORT_DATA_PS2:   /* little-endian too */
        break;
    default:
        Unsupported(fmt);
    }
}

/* The 16-bit samples of `count` SFX descriptors (host order already;
 * relocated: data is a handle) byte-swapped, once each: the GameCube's are
 * big-endian, the mixer reads host order. */
static void SwapSFXSamples(u8 *desc, u32 count) {
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

/* After CreateSFX has copied a pack's samples to ARAM. */
void PortData_SFXDone(s32 fmt) {
    if (fmt == PORT_DATA_PS2) {
        PortPS2_SFXDone();
    }
}

void PortData_SFX(s32 fmt, u8 *sfx, u32 count) {
    switch (fmt) {
    case PORT_DATA_GC:
        PortSwap_SFXDescriptors(sfx, count);
        SwapSFXSamples(sfx, count);
        break;
    case PORT_DATA_PC:
        break;
    case PORT_DATA_PS2:
        PortPS2_SFX(sfx, count);     /* SPU-ADPCM to PCM16 (Port_PS2.c) */
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

/* PC geometry, found before relocation: pointer slots still hold blob
 * offsets, and the pack's pointer list says which words are slots. */
typedef struct PCScan {
    u8 *base;
    u32 size;
    u8 *isSlot;       /* per word: a pointer slot */
    u8 *isTexSlot;    /* per word: a marked level's group texture slot (0x18) */
} PCScan;

static u16 Half(const PCScan *s, u32 off) {
    return off + 2 <= s->size ? *(u16 *)(s->base + off) : 0;
}

/* The blob offset the slot at `off` points to, or ~0 if it isn't a slot. */
static u32 Target(const PCScan *s, u32 off) {
    u32 t;

    if ((off & 3) || off + 4 > s->size || !s->isSlot[off / 4]) {
        return ~0u;
    }
    t = *(u32 *)(s->base + off);
    return t < s->size ? t : ~0u;
}

/* A drawn level: groups with index lists, a vertex buffer with positions.
 * A shadow volume's level (groups flagged 0x4000) has the same header but
 * edge lists; Render_DrawShadows reads those as they are. */
static int PC_IsLevel(const PCScan *s, u32 lod, int *shadow) {
    u32 ng = Half(s, lod + 2), groups = Target(s, lod + 8), vb = Target(s, lod + 0xC), j;

    if (Half(s, lod) != 0 || ng == 0 || ng > 0x400 || groups == ~0u || vb == ~0u) {
        return 0;
    }
    *shadow = (Half(s, groups + 2) & 0x4000) != 0;
    if (*shadow) {
        return 1;
    }
    if (Target(s, vb) == ~0u || Target(s, vb + 8) == ~0u) {
        return 0;
    }
    for (j = 0; j < ng; j++) {
        if (Target(s, groups + j * 0x20 + 0x1C) == ~0u) {
            return 0;
        }
    }
    return 1;
}

/* A geometry: header 0x0002 (the GameCube's is 0x2003), up to 8 levels of
 * 0x18 at 0x8, LOD distances at 0xC. Its drawn levels get
 * PORT_LAYOUT_INDEXED and their groups' texture slots are noted. */
static void PC_MarkGeometry(PCScan *s, u32 g) {
    u32 n = Half(s, g + 2), lods = Target(s, g + 8), i, j;
    int shadow;

    /* the PC writes the levels right after the 0x40-byte header; other
     * structs start with u16 2 and pointers at 0x8 and 0xC too */
    if (Half(s, g) != 0x0002 || n == 0 || n > 8 || lods != g + 0x40 || Target(s, g + 0xC) == ~0u) {
        return;
    }
    for (i = 0; i < n; i++) {
        if (!PC_IsLevel(s, lods + i * 0x18, &shadow)) {
            return;
        }
    }
    for (i = 0; i < n; i++) {
        u32 lod = lods + i * 0x18, groups = Target(s, lod + 8);

        if (!PC_IsLevel(s, lod, &shadow) || shadow) {
            continue;
        }
        ((Geometry *)(s->base + lod))->portLayout = PORT_LAYOUT_INDEXED;
        for (j = 0; j < Half(s, lod + 2); j++) {
            s->isTexSlot[(groups + j * 0x20 + 0x18) / 4] = 1;
        }
    }
}

/* PC geometry in a blob (pack data or a quadrant's GX data): found by its
 * headers, drawn levels marked PORT_LAYOUT_INDEXED (Render_Geometry_.c).
 *
 * A group's texture relocation stores the id as a u32 at 0x18: the PC binds
 * its low half (0x18 little-endian), the GameCube code reads 0x1A and takes
 * 0x18 for ShaderHolder's override. Flagged PORT_TEXRELOC_U16, the store
 * (PortSwap_StoreTextureID) writes 0 there and the id at 0x1A. */
static void PC_MarkGeometries(u8 *base, u32 size, u32 *texIdRelocs, u32 numTexIdRelocs, const u32 *relocs,
                              u32 numRelocs, const u32 *pointers, u32 numPointers) {
    PCScan s;
    u32 i;

    s.base = base;
    s.size = size;
    s.isSlot = calloc(size / 4 + 1, 2);
    s.isTexSlot = s.isSlot + size / 4 + 1;
    if (s.isSlot == NULL) {
        return;
    }
    for (i = 0; i < numPointers; i++) {
        if (!(pointers[i] & 3) && pointers[i] + 4 <= size) {
            s.isSlot[pointers[i] / 4] = 1;
        }
    }
    /* every header in the blob: many geometries are reached through offsets
     * the game relocates itself (thing records, car parts, motion parts, a
     * car's shadow model), not through the pointer list */
    for (i = 0; i + 0x40 <= size; i += 4) {
        if (*(u16 *)(base + i) == 0x0002 && s.isSlot[(i + 8) / 4] && s.isSlot[(i + 0xC) / 4]) {
            PC_MarkGeometry(&s, i);
        }
    }
    (void)relocs;
    (void)numRelocs;
    for (i = 0; i < numTexIdRelocs; i++) {
        u32 slot = texIdRelocs[i * 2];
        if (!(slot & 3) && slot + 4 <= size && s.isTexSlot[slot / 4]) {
            texIdRelocs[i * 2] = slot | PORT_TEXRELOC_U16;
        }
    }
    free(s.isSlot);
}

static u32 LE32(const u8 *p) {
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static void ToBE32(u8 *p) {
    u32 v = LE32(p);
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)v;
}

static void Swap16At(u8 *p) {
    u8 t = p[0];
    p[0] = p[1];
    p[1] = t;
}

static u32 BE32At(const u8 *p) {
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | (u32)p[3];
}

/* A song's instrument bank ('sndb': header words 0x4 size, 0xC, 0x10 count,
 * 0x14 table size; 0x20 per instrument from 0x20: u32 sample address,
 * length, loop, rate, u8 bits 0x11; the samples after the table): its
 * 16-bit samples byte-swapped in place, once each. `be`: the header is
 * big-endian ('RIFX'). */
static void SwapBankSamples(u8 *bank, int be) {
    u32 (*rd)(const u8 *) = be ? BE32At : LE32;
    u32 size = rd(bank + 4), count = rd(bank + 0x10), table = rd(bank + 0x14), i, j;

    if (table < 0x20 + count * 0x20 || table > size + 8) {
        return;
    }
    for (i = 0; i < count; i++) {
        u8 *e = bank + 0x20 + i * 0x20;
        u32 addr = rd(e), len = rd(e + 4);

        if (e[0x11] != 16 || table + addr + len > size + 8) {
            continue;
        }
        for (j = 0; j < i && rd(bank + 0x20 + j * 0x20) != addr; j++) {
        }
        if (j == i) {   /* not shared with an earlier instrument */
            Port_SwapArray16(bank + table + addr, len / 2);
        }
    }
}

/* PC: a song's instrument bank, 'RIFF' 'sndb', little-endian where the
 * GameCube's is 'RIFX' (the songs themselves are 'RIFX' on both). Its header
 * and instruments are turned into the GameCube's in place, once, as the pack
 * loads, so CreateMIDI (MIDI.c) reads it as it reads the GameCube's: header
 * words 0x4, 0xC-0x14 (0x18/0x1C, never read, get the GameCube's 1 and 0 for
 * the PC's uninitialised 0xCCCCCCCC); per instrument u32 sample address,
 * length, loop, rate, u16 0x12 and the envelope u16s 0x18-0x1E. The samples
 * stay as they are: host order. */
static void PC_SongBank(u8 *bank) {
    u32 count, i, j;

    if (memcmp(bank, "RIFF", 4) != 0 || memcmp(bank + 8, "sndb", 4) != 0) {
        return;
    }
    count = LE32(bank + 0x10);
    for (i = 0; i < count; i++) {
        u8 *e = bank + 0x20 + i * 0x20;

        for (j = 0; j < 0x10; j += 4) {
            ToBE32(e + j);
        }
        Swap16At(e + 0x12);
        for (j = 0x18; j < 0x20; j += 2) {
            Swap16At(e + j);
        }
    }
    ToBE32(bank + 4);
    ToBE32(bank + 0xC);
    ToBE32(bank + 0x10);
    ToBE32(bank + 0x14);
    memset(bank + 0x18, 0, 8);
    bank[0x1B] = 1;
    memcpy(bank, "RIFX", 4);
}

/* A song's instrument bank as its pack loads (DataPack_LoadDataPack): in
 * the GameCube's form ('RIFX' header, PortSwap_MIDI swaps CreateMIDI's
 * copy), its samples in host order. Returns the bank to load: GameCube and
 * PC convert it in place; the PS2's is rebuilt in a block (Port_PS2.c),
 * freed by PortData_SongBankDone once CreateMIDI has copied it. */
u8 *PortData_SongBank(s32 fmt, u8 *bank) {
    switch (fmt) {
    case PORT_DATA_GC:
        if (memcmp(bank, "RIFX", 4) == 0 && memcmp(bank + 8, "sndb", 4) == 0) {
            SwapBankSamples(bank, 1);
        }
        break;
    case PORT_DATA_PC:
        PC_SongBank(bank);
        break;
    case PORT_DATA_PS2:
        return PortPS2_SongBank(bank);
    default:
        Unsupported(fmt);
    }
    return bank;
}

void PortData_SongBankDone(s32 fmt) {
    if (fmt == PORT_DATA_PS2) {
        PortPS2_SongBankDone();
    }
}

/* PC: what the GameCube code can't read in a PC pack's blob. Tables and the
 * object graph are already in host order; textures and geometry are not
 * the GameCube's. */
static void PC_PackData(u8 *relocTables, u32 numTextureRelocs, u8 *base, u32 dataSize, u32 *texRelocs,
                        u32 numTexRelocs, u32 *pointers, u32 numPointers, u32 textureDataOffset,
                        u32 numTextures) {
    if (numTextures != 0) {
        PC_Textures(base, dataSize, textureDataOffset, numTextures);
    }
    PC_MarkGeometries(base, dataSize, (u32 *)relocTables, numTextureRelocs, texRelocs, numTexRelocs, pointers,
                      numPointers);
}

/* PC: a quadrant's geometry (DataPack_LoadQuadPack): header 0x54 (valid,
 * numPointers, numRelocs, ..., numShaders 0x1C, dataSize 0x24, numTexRelocs
 * 0x28), texture-id relocations, shaders, pointer relocations, pointers,
 * dataSize bytes, then the GX data the slots are relative to. */
static void PC_QuadPack(u8 *data, u32 size) {
    u32 *hdr = (u32 *)data;
    u32 off = 0x54, nRelocs, nShaders, nTexRelocs, nPointers;
    u32 *relocs, *texRelocs, *pointers;

    if (size < 0x54 || hdr[0] == 0) {
        return;
    }
    nPointers = hdr[1];
    nRelocs = hdr[2];
    nShaders = hdr[7];
    nTexRelocs = hdr[10];
    relocs = (u32 *)(data + off);
    off += (nRelocs + nShaders) * 8;
    texRelocs = (u32 *)(data + off);
    off += nTexRelocs * 8;
    pointers = (u32 *)(data + off);
    off += nPointers * 4 + hdr[9];
    if (off > size) {
        return;
    }
    PC_MarkGeometries(data + off, size - off, relocs, nRelocs, texRelocs, nTexRelocs, pointers, nPointers);
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
        PC_PackData(relocTables, g_portSwapNumTextureRelocs, base, dataSize, texRelocs, numTexRelocs, pointers,
                    numPointers, textureDataOffset, numTextures);
        break;
    case PORT_DATA_PS2:
        break;      /* converted after relocation (PortPS2_*, PortGeometry_ConvertPS2) */
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
        PC_QuadPack(data, size);
        break;
    case PORT_DATA_PS2:
        break;      /* converted after relocation (PortPS2_*, PortGeometry_ConvertPS2) */
    default:
        Unsupported(fmt);
    }
}

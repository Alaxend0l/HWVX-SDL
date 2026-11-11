/* The PS2 release's textures and sounds in the forms the port draws and
 * plays. Port-only file; geometry is in Port_Geometry.c. The formats and
 * the rules are in docs/ps2-version/README.md (HWVX-X-FILE-EDITOR), with
 * reference decoders in its scripts/ (ps2_tex.py, ps2_audio.py).
 *
 * Textures: 0x10-byte headers, packed like the PC's: u16 format, width,
 * height, levels (0, or the full chain to 1x1), the pixels' pointer, u32 the
 * CLUT's index. Format 2 is PSMT8 (one byte per pixel into a CLUT), format 1
 * PSMCT32 (bytes R, G, B, A). Pixels are linear, row-major, levels back to
 * back. CLUTs are 0x400 bytes each (256 x R, G, B, A, alpha 0..255) at the
 * pack's palette offset, in CSM1 order: logical index i is stored with its
 * bits 3 and 4 swapped. Both become the PC's raw RGBA8888 (PORT_TEX_PC | 1):
 * PSMCT32 already is, PSMT8 is expanded into a block the pack keeps. Their
 * colours are the PC's (the GameCube's are those squared), so the PC
 * texture setting applies to them too.
 *
 * Sounds: the GameCube/PC descriptor, with SPU-ADPCM samples (16-byte
 * blocks of 28 samples; block 0 is always silence), length in ADPCM bytes
 * and the loop as the byte offset of its block. Decoded to PCM16 for
 * CreateSFX, block 0 dropped. Song banks ('RIFF' 'sndb', bits 4) are rebuilt
 * in the GameCube's form ('RIFX' header, PCM16 samples in host order), which
 * CreateMIDI reads. */
#include "common.h"
#include "Voice.h"
#include "Port_Swap.h"
#include "xdl.h"

/* ---- textures ------------------------------------------------------------ */

static u32 CSM1(u32 i) {
    return (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1);
}

static u32 Levels(u16 levels) {
    return levels ? levels : 1;
}

/* The game draws textures up to 0x200 (Render_CreateTextureSlot); anything
 * much bigger is a damaged header. */
#define PS2_MAX_TEX 0x400

/* Bytes of a PSMT8 texture's levels (indices), or of their RGBA8 (`bpp` 4). */
static u64 TexBytes(const u16 *h, u32 bpp) {
    u32 w = h[1], ht = h[2], l;
    u64 n = 0;

    for (l = 0; l < Levels(h[3]); l++) {
        n += (u64)(w ? w : 1) * (ht ? ht : 1) * bpp;
        w >>= 1;
        ht >>= 1;
    }
    return n;
}

/* A PSMT8 texture the converter can read: a sane size, its indices inside
 * the pack and its CLUT among the pack's. */
static int Indexed(const u8 *base, u32 size, const u16 *h, u32 palOff, u32 numPal) {
    u32 clut = *(const u32 *)(h + 6);
    const u8 *src = SLOT_LOAD(const u8 *, h + 4);

    return h[0] == 2 && h[1] <= PS2_MAX_TEX && h[2] <= PS2_MAX_TEX && h[3] <= 16 && clut < numPal &&
           (u64)palOff + ((u64)clut + 1) * 0x400 <= size && src >= base && src <= base + size &&
           TexBytes(h, 1) <= (u64)(base + size - src);
}

void *PortPS2_Textures(u8 *base, u32 size, u32 texOff, u32 numTex, u32 palOff, u32 numPal) {
    u32 i, l;
    u64 total = 0;
    u8 *block, *out;
    int unknown = 0;

    for (i = 0; i < numTex && (u64)texOff + ((u64)i + 1) * 0x10 <= size; i++) {
        const u16 *h = (const u16 *)(base + texOff + i * 0x10);

        if (Indexed(base, size, h, palOff, numPal)) {
            total += TexBytes(h, 4);
        }
    }
    block = total && total < 0x10000000 ? Allocate((u32)total + 0x20, 0, "") : NULL;
    Xdl_NoteBlock(XDL_SEG_BLOCK, block, (u32)total + 0x20);     /* and its pointers: for the pack cache (xdl.c) */
    out = block != NULL ? (u8 *)PTR((HND(block) + 0x1F) & ~0x1Fu) : NULL;
    for (i = 0; i < numTex && (u64)texOff + ((u64)i + 1) * 0x10 <= size; i++) {
        u16 *h = (u16 *)(base + texOff + i * 0x10);
        u32 clut = *(u32 *)(h + 6), k;

        if (out != NULL && Indexed(base, size, h, palOff, numPal)) {
            const u8 *pal = base + palOff + clut * 0x400;
            const u8 *src = SLOT_LOAD(const u8 *, h + 4);
            u32 w = h[1], ht = h[2];

            SLOT_STORE(h + 4, out);
            Xdl_NoteSlot(h + 4);
            for (l = 0; l < Levels(h[3]); l++) {
                u32 n = (w ? w : 1) * (ht ? ht : 1);

                for (k = 0; k < n; k++) {
                    memcpy(out + k * 4, pal + CSM1(src[k]) * 4, 4);
                }
                src += n;
                out += n * 4;
                w >>= 1;
                ht >>= 1;
            }
            h[0] = 1;
        } else if (h[0] != 1) {
            unknown++;  /* not converted: left out (LoadBTF_RAM would misread it) */
            continue;
        }
        /* as PC_Textures (Port_Data.c): PORT_TEX_PC | format, u16s byte-reversed for LoadBTF_RAM */
        h[0] = (u16)(PORT_TEX_PC | h[0]);
        for (k = 0; k < 4; k++) {
            h[k] = (u16)(h[k] >> 8 | h[k] << 8);
        }
    }
    if (unknown) {
        Port_Log("PS2 textures: %d of %u not converted (unknown format, damaged or out of memory)", unknown,
                 (unsigned)numTex);
    }
    return block;
}

/* ---- SPU-ADPCM ----------------------------------------------------------- */

static const s32 s_filter[5][2] = { { 0, 0 }, { 60, 0 }, { 115, -52 }, { 98, -55 }, { 122, -60 } };

/* Decode `blocks` 16-byte blocks from src into 28 samples each at dst. */
static void DecodeADPCM(const u8 *src, u32 blocks, s16 *dst) {
    s32 s1 = 0, s2 = 0;
    u32 b, k;

    for (b = 0; b < blocks; b++, src += 16) {
        u32 shift = src[0] & 0xF, filter = (src[0] >> 4) & 7;
        s32 f0, f1;

        if (filter > 4) {
            filter = 0;
        }
        f0 = s_filter[filter][0];
        f1 = s_filter[filter][1];
        for (k = 0; k < 28; k++) {
            s32 nib = (src[2 + k / 2] >> ((k & 1) * 4)) & 0xF, s;

            s = (s32)(s16)(nib << 12) >> shift;
            s += (s1 * f0 + s2 * f1 + 32) >> 6;
            s = s > 32767 ? 32767 : s < -32768 ? -32768 : s;
            *dst++ = (s16)s;
            s2 = s1;
            s1 = s;
        }
    }
}

/* PCM bytes for `len` ADPCM bytes, block 0 dropped. */
static u32 PCMBytes(u32 len) {
    return len >= 32 ? (len / 16 - 1) * 28 * 2 : 0;
}

/* A loop at ADPCM byte offset `loop` (or ~0) as a PCM byte offset. */
static u32 PCMLoop(u32 loop) {
    return loop == 0xFFFFFFFF ? loop : loop >= 16 ? (loop / 16 - 1) * 28 * 2 : 0;
}

/* ---- sound effects ------------------------------------------------------- */

static void *s_sfxBlock;

/* Decode a pack's SFX samples into a block, which PortPS2_SFXDone frees once
 * CreateSFX has copied them to ARAM. */
void PortPS2_SFX(u8 *desc, u32 count) {
    SFX *sfx = (SFX *)desc;
    u32 i;
    u64 total = 0;
    u8 *out;

    /* a sample outside the pack's sound data is dropped, not decoded */
    for (i = 0; i < count; i++) {
        if (sfx[i].data == 0 || !PortData_InSound(PTR(sfx[i].data), sfx[i].length)) {
            sfx[i].length = 0;
        }
        total += ((PCMBytes(sfx[i].length) + 0x1F) & ~0x1Fu) + 0x20;
    }
    s_sfxBlock = total && total < 0x10000000 ? Allocate((u32)total + 0x20, 0, "") : NULL;
    Xdl_NoteBlock(XDL_SEG_SFX, s_sfxBlock, (u32)total + 0x20);
    if (s_sfxBlock == NULL) {
        /* the samples stay SPU-ADPCM, which would play as noise: silence them */
        for (i = 0; i < count; i++) {
            sfx[i].length = 0;
        }
        return;
    }
    out = (u8 *)PTR((HND(s_sfxBlock) + 0x1F) & ~0x1Fu);
    for (i = 0; i < count; i++) {
        u32 len = sfx[i].length, pcm = PCMBytes(len);
        const u8 *src = sfx[i].data ? (const u8 *)PTR(sfx[i].data) : NULL;

        if (src == NULL || pcm == 0) {
            sfx[i].length = 0;
            continue;
        }
        DecodeADPCM(src + 16, len / 16 - 1, (s16 *)out);
        memset(out + pcm, 0, 0x20);     /* CreateSFX_ copies up to 32 bytes past the end */
        sfx[i].data = HND(out);
        Xdl_NoteSlot(&sfx[i].data);
        sfx[i].length = pcm;
        sfx[i].loop = PCMLoop(sfx[i].loop);
        sfx[i].unused10 = 0;
        sfx[i].bits = 16;
        out += ((pcm + 0x1F) & ~0x1Fu) + 0x20;
    }
}

void PortPS2_SFXDone(void) {
    Xdl_BlockDone(s_sfxBlock);
    Deallocate(s_sfxBlock);
    s_sfxBlock = NULL;
}

/* ---- song banks ---------------------------------------------------------- */

static void *s_bank;

static u32 LE32(const u8 *p) {
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static void PutBE32(u8 *p, u32 v) {
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)v;
}

/* A PS2 bank ('RIFF' 'sndb': the PC's header and 0x20-byte instruments,
 * sample offsets, lengths and loops in ADPCM bytes, bits 4, samples at the
 * bank + its table size) rebuilt in the GameCube's form, its samples decoded
 * to PCM16 in host order. Returns the new bank, freed by
 * PortPS2_SongBankDone once CreateMIDI has copied it; NULL if not one. */
u8 *PortPS2_SongBank(const u8 *bank) {
    u32 count, table, i, pcm = 0, total;
    u8 *nb, *samples;

    if (memcmp(bank, "RIFF", 4) != 0 || memcmp(bank + 8, "sndb", 4) != 0) {
        return NULL;
    }
    count = LE32(bank + 0x10);
    table = LE32(bank + 0x14);
    if (count > 0x1000 || table < 0x20 + count * 0x20 || !PortData_InSound(bank, table)) {
        return NULL;
    }
    for (i = 0; i < count; i++) {
        u64 sum = (u64)pcm + PCMBytes(LE32(bank + 0x20 + i * 0x20 + 4));

        if (sum > 0x8000000) {
            return NULL;
        }
        pcm = (u32)sum;
    }
    total = 0x20 + count * 0x20 + pcm;
    s_bank = Allocate(total + 0x40, 0, "");
    Xdl_NoteBlock(XDL_SEG_BANK, s_bank, total + 0x40);
    if (s_bank == NULL) {
        return NULL;
    }
    nb = (u8 *)PTR((HND(s_bank) + 0x1F) & ~0x1Fu);
    memset(nb, 0, total + 0x20);
    memcpy(nb, "RIFX", 4);
    PutBE32(nb + 4, total - 8);
    memcpy(nb + 8, "sndb", 4);
    PutBE32(nb + 0xC, LE32(bank + 0xC));
    PutBE32(nb + 0x10, count);
    PutBE32(nb + 0x14, 0x20 + count * 0x20);
    PutBE32(nb + 0x18, 1);
    samples = nb + 0x20 + count * 0x20;
    pcm = 0;
    for (i = 0; i < count; i++) {
        const u8 *e = bank + 0x20 + i * 0x20;
        u8 *o = nb + 0x20 + i * 0x20;
        u32 addr = LE32(e), len = LE32(e + 4), n = PCMBytes(len), k;

        if ((u64)table + addr + len <= (u64)LE32(bank + 4) + 8 && PortData_InSound(bank, (u64)table + addr + len) &&
            n) {
            DecodeADPCM(bank + table + addr + 16, len / 16 - 1, (s16 *)(samples + pcm));
        }
        PutBE32(o, pcm);
        PutBE32(o + 4, n);
        PutBE32(o + 8, PCMLoop(LE32(e + 8)));
        PutBE32(o + 0xC, LE32(e + 0xC));
        memcpy(o + 0x10, e + 0x10, 0x10);
        o[0x11] = 16;
        for (k = 0x12; k < 0x20; k += 2) {
            if (k == 0x14 || k == 0x16) {
                continue;       /* program, key range: bytes */
            }
            o[k] = e[k + 1];
            o[k + 1] = e[k];
        }
        pcm += n;
    }
    return nb;
}

void PortPS2_SongBankDone(void) {
    Xdl_BlockDone(s_bank);
    Deallocate(s_bank);
    s_bank = NULL;
}

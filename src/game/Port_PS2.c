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

/* ---- textures ------------------------------------------------------------ */

static u32 CSM1(u32 i) {
    return (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1);
}

static u32 Levels(u16 levels) {
    return levels ? levels : 1;
}

void *PortPS2_Textures(u8 *base, u32 size, u32 texOff, u32 numTex, u32 palOff, u32 numPal) {
    u32 i, l, total = 0;
    u8 *block, *out;

    for (i = 0; i < numTex && texOff + (i + 1) * 0x10 <= size; i++) {
        u16 *h = (u16 *)(base + texOff + i * 0x10);
        u32 w = h[1], ht = h[2];

        if (h[0] != 2) {
            continue;
        }
        for (l = 0; l < Levels(h[3]); l++) {
            total += (w ? w : 1) * (ht ? ht : 1) * 4;
            w >>= 1;
            ht >>= 1;
        }
    }
    block = total ? Allocate(total + 0x20, 0, "") : NULL;
    out = block != NULL ? (u8 *)PTR((HND(block) + 0x1F) & ~0x1Fu) : NULL;
    for (i = 0; i < numTex && texOff + (i + 1) * 0x10 <= size; i++) {
        u16 *h = (u16 *)(base + texOff + i * 0x10);
        u32 clut = *(u32 *)(h + 6), k;

        if (h[0] == 2 && out != NULL && clut < numPal && palOff + (clut + 1) * 0x400 <= size) {
            const u8 *pal = base + palOff + clut * 0x400;
            const u8 *src = SLOT_LOAD(const u8 *, h + 4);
            u32 w = h[1], ht = h[2];

            SLOT_STORE(h + 4, out);
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
            continue;   /* not a known format: left as it is */
        }
        /* as PC_Textures (Port_Data.c): PORT_TEX_PC | format, u16s byte-reversed for LoadBTF_RAM */
        h[0] = (u16)(PORT_TEX_PC | h[0]);
        for (k = 0; k < 4; k++) {
            h[k] = (u16)(h[k] >> 8 | h[k] << 8);
        }
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
    u32 i, total = 0;
    u8 *out;

    for (i = 0; i < count; i++) {
        total += ((PCMBytes(sfx[i].length) + 0x1F) & ~0x1Fu) + 0x20;
    }
    s_sfxBlock = total ? Allocate(total + 0x20, 0, "") : NULL;
    if (s_sfxBlock == NULL) {
        return;
    }
    out = (u8 *)PTR((HND(s_sfxBlock) + 0x1F) & ~0x1Fu);
    for (i = 0; i < count; i++) {
        u32 len = sfx[i].length, pcm = PCMBytes(len);
        const u8 *src = sfx[i].data ? (const u8 *)PTR(sfx[i].data) : NULL;

        if (src == NULL || pcm == 0) {
            continue;
        }
        DecodeADPCM(src + 16, len / 16 - 1, (s16 *)out);
        memset(out + pcm, 0, 0x20);     /* CreateSFX_ copies up to 32 bytes past the end */
        sfx[i].data = HND(out);
        sfx[i].length = pcm;
        sfx[i].loop = PCMLoop(sfx[i].loop);
        sfx[i].unused10 = 0;
        sfx[i].bits = 16;
        out += ((pcm + 0x1F) & ~0x1Fu) + 0x20;
    }
}

void PortPS2_SFXDone(void) {
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
    if (table < 0x20 + count * 0x20) {
        return NULL;
    }
    for (i = 0; i < count; i++) {
        pcm += PCMBytes(LE32(bank + 0x20 + i * 0x20 + 4));
    }
    total = 0x20 + count * 0x20 + pcm;
    s_bank = Allocate(total + 0x40, 0, "");
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

        if (table + addr + len <= LE32(bank + 4) + 8 && n) {
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
    Deallocate(s_bank);
    s_bank = NULL;
}

/* Byte-swapping of BrainCore data packs (.xgc) on load. Port-only file.
 *
 * Pack files are big-endian. The game reads them in place, so everything it
 * reads has to be in host order before it looks. What is known for certain:
 *
 * - the TOC, the pack header and the relocation tables are 32-bit words;
 * - the pack's pointer table lists every pointer slot in the data blob.
 *
 * Inside the blob, fields have mixed sizes and the file says nothing about
 * them. The blob is therefore swapped with a per-byte "claim" map:
 *
 * - pointer slots are swapped first (they're known);
 * - type walkers (Claim_* below, rooted at the pack's object table) claim the
 *   fields they know: u16s, single bytes, strings, raw data such as texture
 *   pixels, which the GX layer reads in big-endian itself;
 * - strings reached through any pointer are found heuristically;
 * - every other aligned word is swapped as a 32-bit value, which is right for
 *   the u32/s32/f32 fields that make up most of the data.
 *
 * So a wrong value read from a pack usually means a u8/u16 field (or a byte
 * array) that no walker claims yet: add it to the walker for its type.
 * tools/xgc_inspect.py shows a pack's layout.
 */
#include "common.h"
#include "Port_Swap.h"
#include "swapcheck.h"


typedef struct Swapper {
    u8 *base;      /* data blob (pointer slots hold blob offsets, host order) */
    u32 size;
    u8 *claim;     /* one entry per blob byte */
    s32 packType;
} Swapper;

s32 g_portSwapPackType = -1;
const char *g_portSwapPackName;

void PortSwap_TOC(u32 *buf, u32 bufWords) {
    u32 n, i;

    Port_SwapArray32(buf, 4);
    n = 4 + buf[3] * (sizeof(FileTOCEntry) / 4);
    for (i = 4; i < n && i < bufWords; i++) {
        buf[i] = BE32(buf[i]);
    }
}

void PortSwap_PackHeader(u32 *hdr, u32 numWords, u32 numRefs, u32 refSize) {
    u32 i;
    u8 *refs = (u8 *)(hdr + numWords);

    Port_SwapArray32(hdr, numWords);
    for (i = 0; i < numRefs; i++) {
        /* DataPackRef: char name[0xC]; u32 id; pack pointer (runtime) */
        Port_SwapArray32(refs + i * refSize + 0xC, 2);
    }
}

/* ---- claim helpers ------------------------------------------------------ */

static int InBlob(Swapper *s, u32 off, u32 n) {
    return off < s->size && n <= s->size - off;
}

static void Claim(Swapper *s, u32 off, u32 n, u8 kind) {
    u32 i;

    if (!InBlob(s, off, n)) {
        return;
    }
    for (i = 0; i < n; i++) {
        if (s->claim[off + i] != CLAIM_DONE) {
            s->claim[off + i] = kind;
        }
    }
}

static void ClaimU16(Swapper *s, u32 off) { Claim(s, off, 2, CLAIM_U16); }
static void ClaimBytes(Swapper *s, u32 off, u32 n) { Claim(s, off, n, CLAIM_BYTES); }

/* Pointer slot at blob offset `off`: the target's blob offset, or ~0. */
static u32 Target(Swapper *s, u32 off) {
    u32 v;

    if (!InBlob(s, off, 4) || s->claim[off] != CLAIM_DONE) {
        return ~0u;
    }
    v = *(u32 *)(s->base + off);
    return v < s->size ? v : ~0u;
}

static u32 Word(Swapper *s, u32 off) {
    return InBlob(s, off, 4) ? Port_ReadBE32(s->base + off) : 0;
}

/* ---- textures (BTF headers, Gfx_TextureHandler.c: LoadBTF_RAM) ----------
 * 0x10-byte header, one per texture, 32-byte aligned, from textureDataOffset:
 *   u16 format (0x0), u16 width (0x2), u16 height (0x4), u16 mipCount (0x6),
 *   pixel data pointer (0x8), u32 (0xC).
 * The u16s are little-endian in the file and the game swaps them itself, so
 * they get swapped like any big-endian u16 here. Pixel data stays as it is. */

static u32 TexBpp(u32 format) {
    switch (format) {
    case 1: return 32;  /* GX_TF_RGBA8 */
    case 4: return 4;   /* GX_TF_CMPR */
    default: return 16;
    }
}

static u32 TexSize(u32 format, u32 w, u32 h, u32 mips) {
    u32 size = 0;
    u32 bw = format == 4 ? 8 : 4; /* CMPR blocks are 8x8, the rest 4x4 tiles */
    u32 i;

    if (mips == 0 || format == 1) {
        mips = 1;  /* RGBA8 textures have no mip levels (Render_CreateTextureSlot) */
    }
    for (i = 0; i < mips && w && h; i++) {
        u32 tw = (w + bw - 1) / bw * bw;
        u32 th = (h + bw - 1) / bw * bw;
        size += tw * th * TexBpp(format) / 8;
        w >>= 1;
        h >>= 1;
    }
    return size;
}

static void Claim_Textures(Swapper *s, u32 off, u32 count) {
    u32 i;

    for (i = 0; i < count; i++) {
        u8 *h = s->base + off;
        u32 format, w, ht, mips, pix;

        if (!InBlob(s, off, 0x10)) {
            return;
        }
        /* little-endian u16s in the file */
        format = h[0] | (h[1] << 8);
        w = h[2] | (h[3] << 8);
        ht = h[4] | (h[5] << 8);
        mips = h[6] | (h[7] << 8);
        ClaimU16(s, off + 0);
        ClaimU16(s, off + 2);
        ClaimU16(s, off + 4);
        ClaimU16(s, off + 6);
        pix = Target(s, off + 8);
        if (pix != ~0u) {
            ClaimBytes(s, pix, TexSize(format, w, ht, mips));
        }
        off += 0x20; /* 0x10 header, padded to 32 */
    }
}

/* Strings: a pointer target with at least two printable characters and a
 * terminating NUL, not claimed by anything else. */
static void Claim_StringTargets(Swapper *s, const u32 *pointers, u32 numPointers) {
    u32 i;

    for (i = 0; i < numPointers; i++) {
        u32 t = Target(s, pointers[i]);
        u32 n = 0;

        if (t == ~0u || s->claim[t] != CLAIM_DEFAULT) {
            continue;
        }
        while (t + n < s->size && s->base[t + n] >= 0x20 && s->base[t + n] < 0x7F) {
            n++;
        }
        if (n >= 2 && t + n < s->size && s->base[t + n] == 0) {
            ClaimBytes(s, t, n + 1);
        }
    }
}

/* ---- MCP (mcp.xgc; State_GameManager.c, GameSettings.c) ------------------
 *   0x00 u8 x4 (game type at 0x01)   0x04 menu name (string)
 *   0x28 menu file   0x2C numSteps   0x30 steps (0xC each: u8 type, movie
 *   name, pack ref)   0x34/0x38 count + table of s32 */

static void Claim_MCP(Swapper *s, u32 off) {
    u32 steps, n, i;

    ClaimBytes(s, off, 4);
    steps = Target(s, off + 0x30);
    n = Word(s, off + 0x2C);
    for (i = 0; steps != ~0u && i < n; i++) {
        ClaimBytes(s, steps + i * 0xC, 4);  /* u8 type + padding */
    }
}

/* ---- sound sequences (SoundSequence.c, ExtractKeys.c: ExtractSoundKey) ----
 * params: u32 0x0, u8 0x4 (all players), u8 0x5 (max instances),
 * u16 0x6 (random delay), key track 0x8 -> keys (0x10 each: f32 duration,
 * u8 active, numSounds, sounds -> Sound* list); Sound: u8 x8, then f32 volume,
 * pitch, pitch range, sample, range pointers. */
static void Claim_SoundSeq(Swapper *s, u32 off) {
    u32 keys, n, i, j;

    if (off == ~0u) {
        return;
    }
    ClaimBytes(s, off + 4, 2);
    ClaimU16(s, off + 6);
    keys = Target(s, off + 0xC);
    n = Word(s, off + 8);
    for (i = 0; keys != ~0u && i < n && i < 0x1000; i++) {
        u32 k = keys + i * 0x10;
        u32 list = Target(s, k + 0xC);
        u32 ns = Word(s, k + 8);
        ClaimBytes(s, k + 4, 4);
        for (j = 0; list != ~0u && j < ns && j < 0x100; j++) {
            u32 snd = Target(s, list + j * 4);
            if (snd != ~0u) {
                ClaimBytes(s, snd, 8);
            }
        }
    }
}

/* ---- interfaces (InterfaceManager.c, ExtractKeys.c) ---------------------- */

/* A key track { u32 count; keys } whose keys are `stride` bytes with the
 * u8 fields (active, interp, ...) in the word at `bytesAt`. */
static void Claim_KeyTrack(Swapper *s, u32 track, u32 stride, u32 bytesAt) {
    u32 keys = Target(s, track + 4);
    u32 n = Word(s, track);
    u32 i;

    for (i = 0; keys != ~0u && i < n && i < 0x10000; i++) {
        ClaimBytes(s, keys + i * stride + bytesAt, 4);
    }
}

/* Font (InterfaceManager_AddFont): u32 0x0, u8 0x8-0xB, count 0x20,
 * spacing 0x24 (0xC each: u8 char, f32, f32), f32 0x28. */
static void Claim_Font(Swapper *s, u32 off) {
    u32 sp, n, i;

    if (off == ~0u) {
        return;
    }
    ClaimBytes(s, off + 8, 4);
    sp = Target(s, off + 0x24);
    n = Word(s, off + 0x20);
    for (i = 0; sp != ~0u && i < n && i < 0x200; i++) {
        ClaimBytes(s, sp + i * 0xC, 4);
    }
}

/* Key tracks by key type (ExtractKeys.c). */
enum { KEYS_ACTIVE, KEYS_VEC4, KEYS_COLOR, KEYS_UV };

static void Claim_Track(Swapper *s, u32 track, int kind) {
    switch (kind) {
    case KEYS_ACTIVE: Claim_KeyTrack(s, track, 0x08, 0x00); break; /* u8 value, active; f32 duration */
    case KEYS_VEC4: Claim_KeyTrack(s, track, 0x20, 0x10); break;   /* Vector4; u8 active, interp */
    case KEYS_UV: Claim_KeyTrack(s, track, 0x28, 0x20); break;     /* 8 f32; u8 active, interp */
    case KEYS_COLOR:                                                /* RGBA8; u8 active, interp */
        Claim_KeyTrack(s, track, 0x0C, 0x00);
        Claim_KeyTrack(s, track, 0x0C, 0x04);
        break;
    }
}

/* Object animation (AddObject: ObjAnim): pos, rot, scale, color, uv. */
static void Claim_ObjAnim(Swapper *s, u32 off) {
    if (off == ~0u) {
        return;
    }
    Claim_Track(s, off + 0x00, KEYS_VEC4);
    Claim_Track(s, off + 0x08, KEYS_VEC4);
    Claim_Track(s, off + 0x10, KEYS_VEC4);
    Claim_Track(s, off + 0x18, KEYS_COLOR);
    Claim_Track(s, off + 0x20, KEYS_UV);
}

static void Claim_ActionList(Swapper *s, u32 off);

/* Object state animation (MoveObject, obj + 0x254): active, pos, color,
 * scale, uv, rot. */
static void Claim_StateAnim(Swapper *s, u32 off) {
    if (off == ~0u) {
        return;
    }
    Claim_Track(s, off + 0x00, KEYS_ACTIVE);
    Claim_Track(s, off + 0x08, KEYS_VEC4);
    Claim_Track(s, off + 0x10, KEYS_COLOR);
    Claim_Track(s, off + 0x18, KEYS_VEC4);
    Claim_Track(s, off + 0x20, KEYS_UV);
    Claim_Track(s, off + 0x28, KEYS_VEC4);
    Claim_SoundSeq(s, Target(s, off + 0x30));   /* played on entering the state */
    Claim_ActionList(s, off + 0x34);            /* run on entering the state */
}

static void Claim_IMHeader(Swapper *s, u32 off);

/* { s8 isVar; u8 var; u16 value } (conditions, actions, event args) */
static void Claim_VarValue(Swapper *s, u32 off) {
    if (off != ~0u) {
        ClaimBytes(s, off, 2);
        ClaimU16(s, off + 2);
    }
}

/* Action list (ExecuteActions): { count; actions (8 each: u8 type, data) }.
 *   type 1: data { u8, u8, u16, u32 }
 *   type 2: data { code var -> { id, value }, value }
 *   type 3: data is a { s8, u8, u16 } assignment */
static void Claim_ActionList(Swapper *s, u32 off) {
    u32 list, n, i;

    if (off == ~0u) {
        return;
    }
    list = Target(s, off + 4);
    n = Word(s, off);
    for (i = 0; list != ~0u && i < n && i < 0x400; i++) {
        u32 a = list + i * 8;
        u32 data = Target(s, a + 4);
        u8 type = s->base[a];   /* still big-endian */

        ClaimBytes(s, a, 4);
        if (data == ~0u) {
            continue;
        }
        switch (type) {
        case 1:
            ClaimBytes(s, data, 2);
            ClaimU16(s, data + 2);
            break;
        case 2: {
            u32 cv = Target(s, data);
            if (cv != ~0u) {
                Claim_VarValue(s, Target(s, cv + 4));
            }
            Claim_VarValue(s, Target(s, data + 4));
            break;
        }
        case 3:
            Claim_VarValue(s, data);
            break;
        }
    }
}

/* Input event definition (PlayEvent: EventDef): target 0x0 (the header of the
 * interface it opens), u8 flags 0x4,
 * u8 type 0x5, u16 id 0x6, args 0x8 -> { s8 isVar, u8, u16 value },
 * bytes 0x18, numMods 0x1C, mods 0x20 (8 each: u8 slot, s32 delta). */
static void Claim_EventDef(Swapper *s, u32 off) {
    u32 args, mods, n, i;

    if (off == ~0u || s->claim[off + 4] == CLAIM_BYTES) {
        return;
    }
    /* target 0x0: an interface header (types 1, 0x20) or an action list (0xB) */
    switch (s->base[off + 5]) {
    case 1:
    case 0x20:
        Claim_IMHeader(s, Target(s, off));
        break;
    case 0xB:
        Claim_ActionList(s, Target(s, off));
        break;
    }
    ClaimBytes(s, off + 4, 2);
    ClaimU16(s, off + 6);
    /* args 0x8: a table of pointers to { s8, u8, u16 } values */
    args = Target(s, off + 8);
    if (args != ~0u) {
        Claim_VarValue(s, Target(s, args));
        Claim_VarValue(s, Target(s, args + 4));
    }
    mods = Target(s, off + 0x20);
    n = Word(s, off + 0x1C);
    for (i = 0; mods != ~0u && i < n && i < 0x100; i++) {
        ClaimBytes(s, mods + i * 8, 4);
    }
}

/* Input event list { count; events (0xC each: u8 type, def 0x8) }. */
static void Claim_EventList(Swapper *s, u32 countOff, u32 listOff) {
    u32 list = Target(s, listOff);
    u32 n = Word(s, countOff);
    u32 i;

    for (i = 0; list != ~0u && i < n && i < 0x1000; i++) {
        ClaimBytes(s, list + i * 0xC, 4);
        Claim_EventDef(s, Target(s, list + i * 0xC + 8));
    }
}

/* IMObjectData: u8 id + next[4] (0x0-0x7), numSteps 0x8, steps 0xC (0x28
 * each: var, val[4], u8 set[4] at 0x14), numEvents 0x10, events 0x14. */
static void Claim_IMObjectData(Swapper *s, u32 off) {
    u32 steps, n, i;

    if (off == ~0u) {
        return;
    }
    ClaimBytes(s, off, 8);
    steps = Target(s, off + 0xC);
    n = Word(s, off + 8);
    for (i = 0; steps != ~0u && i < n && i < 0x1000; i++) {
        ClaimBytes(s, steps + i * 0x28 + 0x14, 4);
    }
    Claim_EventList(s, off + 0x10, off + 0x14);
}

/* ObjDef / IMObject (0x6C): u8 kind 0x0, type 0x1, 0x2-0x7, active 0x8, input 0x9,
 * s16 0xC, sub 0x14 (u8 type, u8 at 0x0), anim 0x60, data 0x44. */
static void Claim_IMObject(Swapper *s, u32 off) {
    u32 sub;

    ClaimBytes(s, off, 12);
    ClaimU16(s, off + 0xC);
    ClaimU16(s, off + 0xE);
    sub = Target(s, off + 0x14);
    if (sub != ~0u) {
        /* AddObject's ObjSub / RenderObject's text info: u8 type 0x0, u8 0x1,
         * u8 blend 0xE, scaled 0xF, align 0x11, u16 width 0x14, height 0x16;
         * data 0x10 -> text { strings, font 0x4, font2 0x10 } for text */
        /* The bytes are still big-endian here: byte 1 is the object type. */
        switch (s->base[off + 1]) {
        case 0:
            /* ObjSub: u8 type, u8; value 0x4; data 0x10. Sub type 3: data is a
             * texture key animation { numKeys, keys, frameTime, u8 0xC, u16 0xE } */
            if (s->base[sub] == 3) {
                u32 anim = Target(s, sub + 0x10);
                if (anim != ~0u) {
                    ClaimBytes(s, anim + 0xC, 2);
                    ClaimU16(s, anim + 0xE);
                }
            }
            ClaimBytes(s, sub, 4);
            break;
        case 6:
        case 7:
            break;
        case 1:
        case 10:
        case 12:
            /* the sub is the text itself: strings 0x0, font 0x4, font2 0x10
             * (type 10); RenderObject reads u8 0xE, 0xF, 0x11, u16 0x14, 0x16 */
            ClaimBytes(s, sub + 0xC, 8);
            ClaimU16(s, sub + 0x14);
            ClaimU16(s, sub + 0x16);
            if (s->base[sub + 0x10] == 1) {
                /* string list (MoveObject): 0x8 -> { ..., u8 0x8, u16 0xA } */
                u32 list = Target(s, sub + 8);
                if (list != ~0u) {
                    ClaimBytes(s, list + 8, 2);
                    ClaimU16(s, list + 0xA);
                }
            }
            Claim_Font(s, Target(s, sub + (s->base[off + 1] == 10 ? 0x10 : 4)));
            break;
        }
    }
    Claim_ObjAnim(s, Target(s, off + 0x60));
    Claim_IMObjectData(s, Target(s, off + 0x44));
    if (s->base[off + 1] == 6 || s->base[off + 1] == 7) {
        /* child interface: 0x3C -> slot -> IMHeader */
        u32 h = Target(s, off + 0x3C);
        if (h != ~0u) {
            Claim_IMHeader(s, Target(s, h));
        }
    }
    {
        /* conditions (CheckConditions): 0x48 -> { u8 type; count 0x4;
         * conds 0x8 (0x1C each: u8 type, s32 value, data) } */
        u32 cl = Target(s, off + 0x48);
        if (cl != ~0u) {
            u32 c = Target(s, cl + 8), n = Word(s, cl + 4), i;
            ClaimBytes(s, cl, 4);
            for (i = 0; c != ~0u && i < n && i < 0x100; i++) {
                u32 cond = c + i * 0x1C;
                u32 data = Target(s, cond + 8);
                switch (s->base[cond]) {   /* still big-endian: u8 type */
                case 1:  /* data: { s8 isVar, u8, u16 value } */
                    Claim_VarValue(s, data);
                    break;
                case 2:  /* data: code variable { id, value -> { s8, u8, u16 } } */
                    if (data != ~0u) {
                        Claim_VarValue(s, Target(s, data + 4));
                    }
                    break;
                }
                ClaimBytes(s, cond, 4);
            }
        }
    }
    {
        /* states (SetObjectState): count 0x64, list 0x68 (0xC each: u8 type,
         * u32 value, data) */
        u32 st = Target(s, off + 0x68), n = Word(s, off + 0x64), i;
        for (i = 0; st != ~0u && i < n && i < 0x100; i++) {
            ClaimBytes(s, st + i * 0xC, 4);
            Claim_StateAnim(s, Target(s, st + i * 0xC + 8));
        }
    }
}

/* Interface header (IMHeader; InterfaceManager_Create's Header):
 *   u16 id 0x0 (+u16 0x2), numObjects 0xC, objects 0x10 (ObjDef, 0x6C each),
 *   numProps 0x14, props 0x18 (0xC each: u8 type, u32 value),
 *   vars 0x1C -> { count, IMVariable (0x34: u8 type, update; data -> IMCodeVar
 *   { id, val -> { s8 isVar, u8, u16 value } }) }, sound sequences 0x20,
 *   0x24, 0x28, 0x2C, music 0x30, layer 0x34, u8 mode 0x3C. */
static void Claim_IMHeader(Swapper *s, u32 off) {
    u32 objs, props, vars, n, i;

    if (off == ~0u || s->claim[off] == CLAIM_U16) {
        return; /* not there, or already done (interfaces nest) */
    }
    ClaimU16(s, off);
    ClaimU16(s, off + 2);
    ClaimBytes(s, off + 0x3C, 4);
    objs = Target(s, off + 0x10);
    n = Word(s, off + 0xC);
    for (i = 0; objs != ~0u && i < n && i < 500; i++) {
        Claim_IMObject(s, objs + i * 0x6C);
    }
    /* the header's input events (Create sees them as 0xC-byte properties) */
    Claim_EventList(s, off + 0x14, off + 0x18);
    (void)props;
    vars = Target(s, off + 0x1C);
    if (vars != ~0u) {
        u32 list = Target(s, vars + 4);
        n = Word(s, vars);
        for (i = 0; list != ~0u && i < n && i < 0x1000; i++) {
            u32 v = list + i * 0x34;
            u32 cv = Target(s, v + 0x10);
            ClaimBytes(s, v, 4);
            if (cv != ~0u) {
                u32 val = Target(s, cv + 4);
                if (val != ~0u) {
                    ClaimBytes(s, val, 2);
                    ClaimU16(s, val + 2);
                }
            }
        }
    }
    for (i = 0x20; i <= 0x2C; i += 4) {
        Claim_SoundSeq(s, Target(s, off + i));
    }
}

/* ---- worlds (pack type 4; State_ProgressionManager.c, State_GamePlay.c,
 *      LightManager.c, ShowStats.c, World.c) ----------------------------
 *   0x08 numCars, 0x0C cars (0x60 each: u8 0x34-0x37 incl. type/port 0x35,
 *        u8 0x58-0x5F incl. pack2 0x5D, pack3 0x5E)
 *   0x20 numQuads, 0x24 quads (0x50 each: items 0x30 -> { count 0x14,
 *        items 0x18 (0x40 each: u16 id 0x30, u16 0x32, u8 0x34-0x37 lo/hi) })
 *   0x68 numVolumes, 0x6C volumes (0x30 each: light 0x2C -> { u8 rgb 0x10-0x13,
 *        u8 type 0x18, s8 subtract 0x19 })
 *   0x9C game data: debug font 0xC8 (u8 0x4-0xB), fog color 0xC0 (GXColor) */
static void Claim_World(Swapper *s, u32 off) {
    u32 list, n, i;

    list = Target(s, off + 0x0C);
    n = Word(s, off + 0x08);
    for (i = 0; list != ~0u && i < n && i < 0x100; i++) {
        ClaimBytes(s, list + i * 0x60 + 0x34, 4);
        ClaimBytes(s, list + i * 0x60 + 0x58, 8);
    }
    list = Target(s, off + 0x24);
    n = Word(s, off + 0x20);
    for (i = 0; list != ~0u && i < n && i < 0x1000; i++) {
        u32 il = Target(s, list + i * 0x50 + 0x30);
        if (il != ~0u) {
            u32 items = Target(s, il + 0x18), m = Word(s, il + 0x14), j;
            for (j = 0; items != ~0u && j < m && j < 0x1000; j++) {
                ClaimU16(s, items + j * 0x40 + 0x30);
                ClaimU16(s, items + j * 0x40 + 0x32);
                ClaimBytes(s, items + j * 0x40 + 0x34, 4);
            }
        }
    }
    list = Target(s, off + 0x6C);
    n = Word(s, off + 0x68);
    for (i = 0; list != ~0u && i < n && i < 0x1000; i++) {
        u32 light = Target(s, list + i * 0x30 + 0x2C);
        if (light != ~0u) {
            ClaimBytes(s, light + 0x10, 4);
            ClaimBytes(s, light + 0x18, 4);
        }
    }
    {
        u32 gd = Target(s, off + 0x9C);
        if (gd != ~0u) {
            u32 font = Target(s, gd + 0xC8);
            u32 fog = Target(s, gd + 0xC0);
            if (font != ~0u) {
                ClaimBytes(s, font + 4, 8);
            }
            if (fog != ~0u) {
                ClaimBytes(s, fog, 4);
            }
        }
    }
}

/* ---- cars (pack type 0; CarA.c, CarA.h, AnimHandler.c) ------------------
 * Root objects are CarAInfo:
 *   u8 0x10-0x13 (damagePercent, driveMode), u16 skidTexture 0x14, u16 0x16
 *   skidColor 0x6C (GXColor), modelRef 0x78 -> { data 0x10 -> motion anim }
 *   collider 0xA0 (u8 type 0x10), forcePoints 0xA4 -> { count 0x4, points 0x8
 *   (0x30 each: u8 ray 0x10) }, wheels 0xD0/0xD4 (CarTrailBone, 0x50 each:
 *   u8 driven/skids/parent 0x3C-0x3F, u16 bone 0x40, u8 dusts 0x42-0x43)
 * Motion animation: u8 loop[3]/loopMode/pingPong/maxLoops at 0x1C-0x1F. */
/* Sound (ExtractKeys.c: Sound): u8 type, u8[2], u8 owner, u8[4]; f32 volume,
 * pitch, pitch range; sample id 0x14; range 0x18 */
static void Claim_Sound(Swapper *s, u32 off) {
    if (off != ~0u) {
        ClaimBytes(s, off, 8);
    }
}

/* Slave trigger list { count; triggers (0x20 each: Vector4 pos, u8 type 0x10,
 * u8, u16 id 0x12, f32 time 0x14, data 0x18) } (ThingManager.c, AnimHandler.c) */
static void Claim_SlaveList(Swapper *s, u32 off) {
    u32 t = Target(s, off + 4), n = Word(s, off), i;

    for (i = 0; t != ~0u && i < n && i < 0x400; i++) {
        ClaimBytes(s, t + i * 0x20 + 0x10, 2);
        ClaimU16(s, t + i * 0x20 + 0x12);
    }
}

static void Claim_MotionAnim(Swapper *s, u32 off) {
    if (off != ~0u) {
        ClaimBytes(s, off + 0x1C, 4);
    }
}

static void Claim_CarAInfo(Swapper *s, u32 off) {
    u32 p, n, i;

    ClaimBytes(s, off + 0x10, 4);
    ClaimU16(s, off + 0x14);
    ClaimU16(s, off + 0x16);
    p = Target(s, off + 0x6C);
    if (p != ~0u) {
        ClaimBytes(s, p, 4);
    }
    p = Target(s, off + 0x78);
    if (p != ~0u) {
        Claim_MotionAnim(s, Target(s, p + 0x10));
    }
    p = Target(s, off + 0xA0);
    if (p != ~0u) {
        ClaimBytes(s, p + 0x10, 4);
    }
    p = Target(s, off + 0xA4);
    if (p != ~0u) {
        u32 pts = Target(s, p + 8);
        n = Word(s, p + 4);
        for (i = 0; pts != ~0u && i < n && i < 0x100; i++) {
            ClaimBytes(s, pts + i * 0x30 + 0x10, 4);
        }
    }
    /* slaves: 0xE4 -> list at +4; weapon (0xE8/0xEC) and pickup (0xF0/0xF4) slaves,
     * 12-byte entries with a list at +4 */
    p = Target(s, off + 0xE4);
    if (p != ~0u) {
        Claim_SlaveList(s, p + 4);  /* MotionState_InitAllSlavesNow(info->slaves + 4) */
    }
    p = Target(s, off + 0xEC);
    n = Word(s, off + 0xE8);
    for (i = 0; p != ~0u && i < n && i < 0x40; i++) {
        Claim_SlaveList(s, p + i * 12 + 4);
    }
    p = Target(s, off + 0xF4);
    n = Word(s, off + 0xF0);
    for (i = 0; p != ~0u && i < n && i < 0x40; i++) {
        Claim_SlaveList(s, p + i * 12 + 4);
    }
    /* sounds: 0x60, gears 0xDC -> +0xC, sound sets 0x118/0x11C (pointer table) */
    Claim_Sound(s, Target(s, off + 0x60));
    p = Target(s, off + 0xDC);
    if (p != ~0u) {
        Claim_Sound(s, Target(s, p + 0xC));
    }
    p = Target(s, off + 0x11C);
    n = Word(s, off + 0x118);
    for (i = 0; p != ~0u && i < n && i < 0x40; i++) {
        Claim_Sound(s, Target(s, p + i * 4));
    }
    p = Target(s, off + 0xD4);
    n = Word(s, off + 0xD0);
    for (i = 0; p != ~0u && i < n && i < 0x20; i++) {
        u32 w = p + i * 0x50;
        ClaimBytes(s, w + 0x3C, 4);
        ClaimU16(s, w + 0x40);
        ClaimBytes(s, w + 0x42, 2);
    }
}

/* ---- shaders (every pack; ShaderHolder.c) ---------------------------------
 * The pack's shader table (shaderOffset, numShaders; 0xC each) points at
 * definitions: u8 0x0 (movie texture), color keys 0x4 (count, keys),
 * point keys 0xC (0x10 each: x, y, duration, u8 active 0xC, interp 0xD),
 * texture key animation 0x14 { numKeys, keys, frameTime, u8 0xC, u16 0xE }. */
static void Claim_Shaders(Swapper *s, u32 table, u32 count) {
    u32 i;

    for (i = 0; i < count && i < 0x1000; i++) {
        u32 def = Target(s, table + i * 0xC);
        u32 tex;

        if (def == ~0u || s->claim[def] == CLAIM_BYTES) {
            continue;
        }
        ClaimBytes(s, def, 4);
        Claim_Track(s, def + 0x4, KEYS_COLOR);
        Claim_KeyTrack(s, def + 0xC, 0x10, 0xC);
        tex = Target(s, def + 0x14);
        if (tex != ~0u) {
            ClaimBytes(s, tex + 0xC, 2);
            ClaimU16(s, tex + 0xE);
        }
    }
}

/* Root of every pack: { u32 count; entries -> { object, u32 id }[count] }. */
static void Claim_Roots(Swapper *s) {
    u32 table = Target(s, 4);
    u32 n = Word(s, 0);
    u32 i;

    if (s->packType == 4) {
        Claim_World(s, 0);  /* world packs: the world itself is at the base */
        return;
    }
    for (i = 0; table != ~0u && i < n; i++) {
        u32 obj = Target(s, table + i * 8);

        if (obj == ~0u) {
            continue;
        }
        switch (s->packType) {
        case 3: /* scripts: the MCP */
            Claim_MCP(s, obj);
            break;
        case 0: /* cars */
            Claim_CarAInfo(s, obj);
            break;
        case 1: /* interface packs */
            Claim_IMHeader(s, obj);
            break;

        default:
            break;
        }
    }
}

/* ---- entry point -------------------------------------------------------- */

void PortSwap_PackData(u8 *relocTables, u32 relocTableWords, u8 *base, u32 dataSize,
                       u32 *texRelocs, u32 numTexRelocs, u32 *pointers, u32 numPointers,
                       u32 textureDataOffset, u32 numTextures, u32 shaderOffset, u32 numShaders) {
    Swapper s;
    u32 i;

    Port_SwapArray32(relocTables, relocTableWords);
    Port_SwapArray32(texRelocs, numTexRelocs * 2);
    Port_SwapArray32(pointers, numPointers);

    s.base = base;
    s.size = dataSize;
    s.packType = g_portSwapPackType;
    s.claim = calloc(dataSize ? dataSize : 1, 1);
    if (s.claim == NULL) {
        Port_Fatal("out of memory swapping a data pack");
    }

    /* pointer slots: blob offsets, relocated by the caller afterwards */
    for (i = 0; i < numPointers; i++) {
        if (InBlob(&s, pointers[i], 4)) {
            *(u32 *)(base + pointers[i]) = Port_ReadBE32(base + pointers[i]);
            memset(s.claim + pointers[i], CLAIM_DONE, 4);
        }
    }
    /* texture-id slots: overwritten, or relocated like pointers */
    for (i = 0; i < numTexRelocs; i++) {
        u32 off = texRelocs[i * 2];
        if (InBlob(&s, off, 4)) {
            *(u32 *)(base + off) = Port_ReadBE32(base + off);
            memset(s.claim + off, CLAIM_DONE, 4);
        }
    }

    if (numTextures != 0) {
        Claim_Textures(&s, textureDataOffset, numTextures);
    }
    Claim_Roots(&s);
    Claim_Shaders(&s, shaderOffset, numShaders);
    Claim_StringTargets(&s, pointers, numPointers);

    for (i = 0; i + 4 <= dataSize; i += 4) {
        u8 *c = s.claim + i;
        u8 *p = base + i;

        if (c[0] == CLAIM_DEFAULT && c[1] == CLAIM_DEFAULT && c[2] == CLAIM_DEFAULT && c[3] == CLAIM_DEFAULT) {
            *(u32 *)p = Port_ReadBE32(p);
            continue;
        }
        if (c[0] == CLAIM_U32) {
            *(u32 *)p = Port_ReadBE32(p);
            continue;
        }
        if (c[0] == CLAIM_U16 || (c[0] == CLAIM_DEFAULT && c[1] == CLAIM_DEFAULT)) {
            if (c[0] != CLAIM_BYTES && c[0] != CLAIM_DONE) {
                *(u16 *)p = Port_ReadBE16(p);
            }
        }
        if (c[2] == CLAIM_U16 || (c[2] == CLAIM_DEFAULT && c[3] == CLAIM_DEFAULT)) {
            if (c[2] != CLAIM_BYTES && c[2] != CLAIM_DONE) {
                *(u16 *)(p + 2) = Port_ReadBE16(p + 2);
            }
        }
    }
    PortSwapCheck_Register(base, dataSize, s.claim, g_portSwapPackName, s.packType);
}

/* ---- sound effects (Voice.c: SFX) ----------------------------------------
 *   0x00 sample data (a pointer, swapped with the sound pointer table)
 *   0x04 size, 0x08, 0x0C: u32   0x10, 0x11 (sample format): u8
 *   0x12 u16   0x14..0x1F: u32 (not read by the C code) */
void PortSwap_SFXDescriptors(u8 *sfx, u32 count) {
    u32 i;

    for (i = 0; i < count; i++, sfx += 0x20) {
        Port_SwapArray32(sfx + 4, 3);
        *(u16 *)(sfx + 0x12) = Port_ReadBE16(sfx + 0x12);
        Port_SwapArray32(sfx + 0x14, 3);
    }
}

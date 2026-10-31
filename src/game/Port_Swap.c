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

#ifdef HWVX_SWAPCHECK
#include <execinfo.h>
/* HWVX_SWAPTRACE=offset (hex): log a backtrace for every claim that covers
 * this blob offset, in any pack (swap-check builds only). */
static void Claim_Trace(Swapper *s, u32 off, u32 n, u8 kind) {
    static int init;
    static u32 traceOff = ~0u;
    void *frames[8];
    int k, count;

    if (!init) {
        const char *e = getenv("HWVX_SWAPTRACE");
        init = 1;
        if (e != NULL) {
            traceOff = (u32)strtoul(e, NULL, 16);
        }
    }
    if (traceOff == ~0u || off > traceOff || traceOff - off >= n) {
        return;
    }
    count = backtrace(frames, 8);
    Port_Log("swaptrace: %s off 0x%X n %u kind %u", g_portSwapPackName ? g_portSwapPackName : "?", off, n, kind);
    for (k = 1; k < count; k++) {
        Port_Log("  %p", frames[k]);
    }
}
#endif

static void Claim(Swapper *s, u32 off, u32 n, u8 kind) {
    u32 i;

    if (!InBlob(s, off, n)) {
        return;
    }
#ifdef HWVX_SWAPCHECK
    Claim_Trace(s, off, n, kind);
#endif
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

/* Strings: a pointer target that looks like text: at least 3 printable
 * characters and a NUL, starting with a letter, digit, path character or
 * text punctuation ('|' is the fonts' line break: "|A valid Memory Card...";
 * '+' starts the time bonus "+10 Sec"),
 * and mostly letters and digits (floats like 0.7f read as "?333", so '?' and
 * '@' can't start one). Runs of 8+ printable characters are text whatever
 * they hold ("New File      --/--/---- 00:00 PM"); float data never makes
 * runs that long. */
static int IsWordChar(u8 c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/* A string known to be one from where it is referenced: any length. */
static void Claim_CString(Swapper *s, u32 off) {
    u32 n = 0;

    if (off == ~0u) {
        return;
    }
    while (off + n < s->size && s->base[off + n] != 0) {
        n++;
    }
    if (off + n < s->size) {
        ClaimBytes(s, off, n + 1);
    }
}

static void Claim_StringTargets(Swapper *s, const u32 *pointers, u32 numPointers) {
    u32 i;

    for (i = 0; i < numPointers; i++) {
        u32 t = Target(s, pointers[i]);
        u32 n = 0, word = 0;
        u8 c0;

        if (t == ~0u || s->claim[t] != CLAIM_DEFAULT) {
            continue;
        }
        c0 = s->base[t];
        if (!IsWordChar(c0) && (c0 == 0 || strchr("\\/.|[(\"'-+#*<!", c0) == NULL)) {
            continue;
        }
        while (t + n < s->size && s->base[t + n] >= 0x20 && s->base[t + n] < 0x7F) {
            word += IsWordChar(s->base[t + n]);
            n++;
        }
        if (n >= 3 && (word * 2 > n || n >= 8) && t + n < s->size && s->base[t + n] == 0) {
            ClaimBytes(s, t, n + 1);
        }
    }
}

/* ---- MCP (mcp.xgc; State_GameManager.c, GameSettings.c) ------------------
 *   0x00 u8 x4 (game type at 0x01)   0x04 menu name (string)
 *   0x28 menu file   0x2C numSteps   0x30 steps (0xC each: u8 type, movie
 *   name, pack ref)   0x34/0x38 count + table of s32 */

/* Progression script (State_ProgressionManager.c): { count, levels (0x20
 * each: numSteps, steps, pack, world, win { count, seqs }, lose { count,
 * seqs }) }; a sequence (0xC) is { numSteps, steps, u16 advance }; a step
 * (8) is { u8 type (0 movie, 1 interface pack), data }. */
static void Claim_ProgSteps(Swapper *s, u32 steps, u32 n) {
    u32 i;

    for (i = 0; steps != ~0u && i < n && i < 0x100; i++) {
        ClaimBytes(s, steps + i * 8, 4);
    }
}

static void Claim_ProgScript(Swapper *s, u32 script) {
    u32 levels, n, i, k, j;

    if (script == ~0u) {
        return;
    }
    levels = Target(s, script + 4);
    n = Word(s, script);
    for (i = 0; levels != ~0u && i < n && i < 0x100; i++) {
        u32 lv = levels + i * 0x20;
        Claim_ProgSteps(s, Target(s, lv + 4), Word(s, lv));
        for (k = 0; k < 2; k++) {   /* win 0x10, lose 0x18 */
            u32 seqs = Target(s, lv + 0x14 + k * 8), m = Word(s, lv + 0x10 + k * 8);
            for (j = 0; seqs != ~0u && j < m && j < 0x40; j++) {
                Claim_ProgSteps(s, Target(s, seqs + j * 0xC + 4), Word(s, seqs + j * 0xC));
                ClaimU16(s, seqs + j * 0xC + 8);
                ClaimU16(s, seqs + j * 0xC + 0xA);
            }
        }
    }
}

static void Claim_MCP(Swapper *s, u32 off) {
    u32 steps, n, i;

    ClaimBytes(s, off, 4);
    steps = Target(s, off + 0x30);
    n = Word(s, off + 0x2C);
    for (i = 0; steps != ~0u && i < n; i++) {
        ClaimBytes(s, steps + i * 0xC, 4);  /* u8 type + padding */
    }
    /* progression scripts 0x34/0x38 (and the demo script, 0x24 -> 0x8) */
    steps = Target(s, off + 0x38);
    n = Word(s, off + 0x34);
    for (i = 0; steps != ~0u && i < n && i < 0x100; i++) {
        Claim_ProgScript(s, Target(s, steps + i * 4));
    }
    steps = Target(s, off + 0x24);
    if (steps != ~0u) {
        Claim_ProgScript(s, Target(s, steps + 8));
    }
    /* control bindings: numBindings 0x3C, 0x40 -> table -> slot -> binding
     * { name?, 0x4 -> 16 settings bytes, 0x8 -> KeyBinding (16 bytes) } */
    steps = Target(s, off + 0x40);
    n = Word(s, off + 0x3C);
    for (i = 0; steps != ~0u && i < n && i < 0x40; i++) {
        u32 slot = Target(s, steps + i * 4);
        u32 cb = slot != ~0u ? Target(s, slot) : ~0u;
        u32 p;
        if (cb == ~0u) {
            continue;
        }
        p = Target(s, cb + 4);
        if (p != ~0u) {
            ClaimBytes(s, p, 0x10);
        }
        p = Target(s, cb + 8);
        if (p != ~0u) {
            ClaimBytes(s, p, 0x10);
        }
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
    u32 i, w;

    for (i = 0; keys != ~0u && i < n && i < 0x10000; i++) {
        u32 key = keys + i * stride;
        ClaimBytes(s, key + bytesAt, 4);
        /* the rest are f32/u32 values: claim them, or the string heuristic
         * can take a key for text (725.0f is "D5@\0") and leave it unswapped */
        for (w = 0; w < stride; w += 4) {
            if (w != bytesAt && InBlob(s, key + w, 4) && s->claim[key + w] == CLAIM_DEFAULT) {
                Claim(s, key + w, 4, CLAIM_U32);
            }
        }
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
enum { KEYS_ACTIVE, KEYS_VEC4, KEYS_COLOR, KEYS_UV, KEYS_F32, KEYS_POINT };

static void Claim_Track(Swapper *s, u32 track, int kind) {
    switch (kind) {
    case KEYS_ACTIVE: Claim_KeyTrack(s, track, 0x08, 0x00); break; /* u8 value, active; f32 duration */
    case KEYS_VEC4: Claim_KeyTrack(s, track, 0x20, 0x10); break;   /* Vector4; u8 active, interp */
    case KEYS_UV: Claim_KeyTrack(s, track, 0x28, 0x20); break;     /* 8 f32; u8 active, interp */
    case KEYS_F32: Claim_KeyTrack(s, track, 0x0C, 0x04); break;    /* f32; u8 active, interp */
    case KEYS_POINT: Claim_KeyTrack(s, track, 0x10, 0x0C); break;  /* f32 x, y, duration; u8 active, interp (ExtractPointKey) */
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
    Claim_SoundSeq(s, Target(s, off + 0x14));   /* sound played with the event */
    /* args 0x8: a table of pointers to { s8, u8, u16 } values; type 0x11
     * points at the value itself (InterfaceManager_PlayEvent) */
    args = Target(s, off + 8);
    if (args != ~0u) {
        if (s->base[off + 5] == 0x11) {
            Claim_VarValue(s, args);
        } else {
            Claim_VarValue(s, Target(s, args));
            Claim_VarValue(s, Target(s, args + 4));
        }
    }
    /* bytes 0x18: u8 arguments passed to the interface callback (types 0xD,
     * 0xE: e.g. the vehicle select's selection and car object ids) */
    ClaimBytes(s, Target(s, off + 0x18), 4);
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

/* ObjDef / IMObject (0x6C): u8 kind 0x0, type 0x1, 0x2-0x7, active 0x8, input 0x9, u16 id 0xA,
 * s16 0xC, sub 0x14 (u8 type, u8 at 0x0), anim 0x60, data 0x44. */
/* Briefing dialogue (object type 2; InterfaceManager_MoveDialogSystem):
 * { count, entries (0x20 each) }. Entry: u8 active, id, special, specialKey;
 * s32 time 0x4; sound 0x8/0xC; text 0x10; character 0x14; actions 0x18.
 * Character: info 0x8, colour 0xC -> RGBA8 (copied into a colour key),
 * idle 0x10/0x14, talk 0x18/0x1C (counts, lists), u8 state 0x20, special
 * anims 0x30, quad 0x34. Anims are texture key animations, 0x10 each
 * (numKeys, keys, frameTime, u8 0xC, u8 0xD, u16 0xE). */
static void Claim_DialogAnims(Swapper *s, u32 list, u32 n) {
    u32 i;

    for (i = 0; list != ~0u && i < n && i < 0x100; i++) {
        ClaimBytes(s, list + i * 0x10 + 0xC, 2);
        ClaimU16(s, list + i * 0x10 + 0xE);
    }
}

static void Claim_DialogList(Swapper *s, u32 off) {
    u32 entries, n, i;

    if (off == ~0u) {
        return;
    }
    entries = Target(s, off + 4);
    n = Word(s, off);
    for (i = 0; entries != ~0u && i < n && i < 0x400; i++) {
        u32 e = entries + i * 0x20;
        u32 chr = Target(s, e + 0x14);

        ClaimBytes(s, e, 4);
        Claim_ActionList(s, e + 0x18);
        if (chr == ~0u || s->claim[chr + 0x20] == CLAIM_BYTES) {
            continue;   /* characters are shared between entries */
        }
        ClaimBytes(s, chr + 0x20, 4);
        {
            u32 colour = Target(s, chr + 0xC);
            if (colour != ~0u) {
                ClaimBytes(s, colour, 4);
            }
        }
        Claim_DialogAnims(s, Target(s, chr + 0x14), Word(s, chr + 0x10));
        Claim_DialogAnims(s, Target(s, chr + 0x1C), Word(s, chr + 0x18));
        /* special anims: indexed by the entries' specialKey; no count */
        {
            u32 sp = Target(s, chr + 0x30), k, maxKey = 0;
            for (k = 0; sp != ~0u && k < n && k < 0x400; k++) {
                u32 ek = entries + k * 0x20;
                if (Target(s, ek + 0x14) == chr && s->base[ek + 2] && s->base[ek + 3] + 1u > maxKey) {
                    maxKey = s->base[ek + 3] + 1u;
                }
            }
            Claim_DialogAnims(s, sp, maxKey);
        }
    }
}

static void Claim_IMObject(Swapper *s, u32 off) {
    u32 sub;

    ClaimBytes(s, off, 10);
    ClaimU16(s, off + 0xA);   /* id (InterfaceManager_MoveObject: timers, goals) */
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
        case 2:
            Claim_DialogList(s, sub);
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
            {
                /* the text: 0x0 -> string slots, slot 0 shown (AddObject).
                 * Short ones ("No" in the quit menu) are below
                 * Claim_StringTargets' length limit. */
                u32 str = Target(s, sub), t = str != ~0u ? Target(s, str) : ~0u, n = 0;
                while (t != ~0u && t + n < s->size && s->base[t + n] >= 0x20 && s->base[t + n] < 0x7F) {
                    n++;
                }
                if (t != ~0u && n > 0 && t + n < s->size && s->base[t + n] == 0) {
                    ClaimBytes(s, t, n + 1);
                }
            }
            if (s->base[sub + 0x10] == 1) {
                /* string list (MoveObject): 0x8 -> { count, texts 0x4 (slots
                 * -> text: string slots), u8 0x8, u16 variable 0xA }. The
                 * strings can be one character (the name entry's letters),
                 * too short for Claim_StringTargets, so claim them here. */
                u32 list = Target(s, sub + 8);
                if (list != ~0u) {
                    u32 texts = Target(s, list + 4), k;
                    ClaimBytes(s, list + 8, 2);
                    ClaimU16(s, list + 0xA);
                    for (k = 0; texts != ~0u && k < Word(s, list) && k < 0x400; k++) {
                        u32 text = Target(s, texts + k * 4);
                        if (text != ~0u) {
                            Claim_CString(s, Target(s, text));
                        }
                    }
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
/* Camera sub-mode list { count; list -> 0xC each: u8 type, params 0x4 }
 * (CameraManager.c CameraSubMode); bone params (type 4) have u16 bone 0x14. */
static void Claim_CameraList(Swapper *s, u32 off) {
    u32 list = Target(s, off + 4), n = Word(s, off), i;

    for (i = 0; list != ~0u && i < n && i < 0x40; i++) {
        u32 e = list + i * 0xC;
        ClaimBytes(s, e, 4);
        if (s->base[e] == 4) {
            u32 bp = Target(s, e + 4);
            if (bp != ~0u) {
                ClaimU16(s, bp + 0x14);
                ClaimBytes(s, bp + 0x16, 2);
            }
        }
    }
}

/* Static object record (common.h ThingRecord): geometry 0x0, u8 noDraw,
 * collides, hasDrawDist, isStatic 0x4-0x7, info 0x10 -> DestructibleInfo
 * (u8 type, takesDamage 0x10), shared slaves 0x18 (count, list). */
static void Claim_SlaveList(Swapper *s, u32 off);
static void Claim_MotionAnim(Swapper *s, u32 off);
static void Claim_ThingRecord(Swapper *s, u32 rec) {
    u32 info;

    if (rec == ~0u || s->claim[rec + 4] == CLAIM_BYTES) {
        return;
    }
    if (s->base[rec + 4] == 1) {
        /* noDraw 1: an animated model, run as a motion slave (Destructible_Init) */
        Claim_MotionAnim(s, Target(s, rec));
    }
    ClaimBytes(s, rec + 4, 4);
    info = Target(s, rec + 0x10);
    if (info != ~0u) {
        ClaimBytes(s, info + 0x10, 2);
    }
    Claim_SlaveList(s, rec + 0x18);
}

/* Volume holder (VolumeManager.c), world 0x4: cells 0x10 (7 lists each of
 * { count, volumes -> volume slots }), cell index 0x14 (s32 per grid cell),
 * grid numX/Y/Z 0x2C-0x34.  A volume's data (0x2C) depends on the list it is
 * in: 1 force box { Vector4, s8 oneShot, s8 carOnly, u16 type }, 2 reverb
 * { u32 type, u16 volume, u16 feedback }, 4 fog { u8 type, f32 near, f32 far,
 * GXColor }. */
static void Claim_VolumeHolder(Swapper *s, u32 holder) {
    u32 cells, index, total, numCells = 0, i, t, j;

    if (holder == ~0u) {
        return;
    }
    cells = Target(s, holder + 0x10);
    index = Target(s, holder + 0x14);
    total = Word(s, holder + 0x2C) * Word(s, holder + 0x30) * Word(s, holder + 0x34);
    for (i = 0; index != ~0u && i < total && i < 0x100000; i++) {
        u32 c = Word(s, index + i * 4);
        if (c + 1 > numCells && c < 0x10000) {
            numCells = c + 1;
        }
    }
    for (i = 0; cells != ~0u && i < numCells; i++) {
        for (t = 0; t < 7; t++) {
            u32 list = cells + i * 0x38 + t * 8;
            u32 vols = Target(s, list + 4), n = Word(s, list);
            for (j = 0; vols != ~0u && j < n && j < 0x1000; j++) {
                u32 v = Target(s, vols + j * 4), d;
                if (v == ~0u || (d = Target(s, v + 0x2C)) == ~0u) {
                    continue;
                }
                switch (t) {
                case 1:
                    ClaimBytes(s, d + 0x10, 2);
                    ClaimU16(s, d + 0x12);
                    break;
                case 2:
                    ClaimU16(s, d + 4);
                    ClaimU16(s, d + 6);
                    break;
                case 4:
                    ClaimBytes(s, d, 4);
                    ClaimBytes(s, d + 0xC, 4);
                    break;
                }
            }
        }
    }
}

static f32 BEFloatAt(Swapper *s, u32 off) {
    union { u32 u; f32 f; } v;

    v.u = Word(s, off);
    return v.f;
}

/* Quadrant BSP (BSPN0.c): nodes of 0x20 { plane, u8 type 0x10, u16 next
 * 0x12 }; type & 1: front child is the next node, type & 2: back child is
 * `next` nodes on. */
static void Claim_BSPN0(Swapper *s, u32 root) {
    u32 *stack, sp = 0, cap = 64;

    if (root == ~0u) {
        return;
    }
    stack = malloc(cap * sizeof(u32));
    if (stack == NULL) {
        Port_Fatal("out of memory swapping a BSP");
    }
    stack[sp++] = root;
    while (sp != 0) {
        u32 node = stack[--sp];
        u8 type;

        if (!InBlob(s, node, 0x20) || s->claim[node + 0x10] == CLAIM_BYTES) {
            continue;
        }
        type = s->base[node + 0x10];
        ClaimBytes(s, node + 0x10, 2);
        ClaimU16(s, node + 0x12);
        if (sp + 2 > cap) {
            u32 *grown = realloc(stack, (cap *= 2) * sizeof(u32));
            if (grown == NULL) {
                free(stack);
                Port_Fatal("out of memory swapping a BSP");
            }
            stack = grown;
        }
        if (type & 1) {
            stack[sp++] = node + 0x20;
        }
        if (type & 2) {
            stack[sp++] = node + Port_ReadBE16(s->base + node + 0x12) * 0x20;
        }
    }
    free(stack);
}

/* Collision pack (type 5; CollisionTerrain.c), at the base: TerrainGrid
 * { origin, cells 0x20 (0x50 each), size 0x28-0x30, numX/Y/Z 0x34-0x3C,
 * cell index 0x40 (s32 per grid cell) }, then polys 0x4C, mode 0x58 (1:
 * BSP), edges 0x54, materials 0x5C.  A cell has BSP nodes 0x30 (0x20 each:
 * plane, u32 material, u16 front 0x14, u16 back 0x16; 0xFFFF ends), poly
 * indices 0x38 (u32, count 0x3C) and edge indices 0x44 (u16, count 0x40). */
static void Claim_CollisionBSP(Swapper *s, u32 nodes) {
    u32 *stack, sp = 0, cap = 64;

    stack = malloc(cap * sizeof(u32));
    if (stack == NULL) {
        Port_Fatal("out of memory swapping collision");
    }
    stack[sp++] = 0;
    while (sp != 0) {
        u32 node = nodes + stack[--sp] * 0x20, k;

        if (!InBlob(s, node, 0x20) || s->claim[node + 0x14] == CLAIM_U16) {
            continue;
        }
        ClaimU16(s, node + 0x14);
        ClaimU16(s, node + 0x16);
        if (sp + 2 > cap) {
            u32 *grown = realloc(stack, (cap *= 2) * sizeof(u32));
            if (grown == NULL) {
                free(stack);
                Port_Fatal("out of memory swapping collision");
            }
            stack = grown;
        }
        for (k = 0; k < 2; k++) {
            u16 child = Port_ReadBE16(s->base + node + 0x14 + k * 2);
            if (child != 0xFFFF) {
                stack[sp++] = child;
            }
        }
    }
    free(stack);
}

static void Claim_Collision(Swapper *s) {
    u32 cells = Target(s, 0x20), index = Target(s, 0x40), numCells = 0, total, i, j;

    total = Word(s, 0x34) * Word(s, 0x38) * Word(s, 0x3C);
    for (i = 0; index != ~0u && i < total && i < 0x100000; i++) {
        u32 c = Word(s, index + i * 4);
        if (c + 1 > numCells && c < 0x10000) {
            numCells = c + 1;
        }
    }
    for (i = 0; cells != ~0u && i < numCells; i++) {
        u32 cell = cells + i * 0x50, nodes = Target(s, cell + 0x30), edges = Target(s, cell + 0x44);
        if (nodes != ~0u) {
            Claim_CollisionBSP(s, nodes);
        }
        for (j = 0; edges != ~0u && j < Word(s, cell + 0x40) && j < 0x10000; j++) {
            ClaimU16(s, edges + j * 2);
        }
    }
}

/* Volume shapes (VolumeManager.c IsPointWithinVolume: s32 type 0x8; 0 = BSP
 * at 0x28, 1 = sphere) are referenced from trigger parameter lists and other
 * records, so BSP volumes are found by shape: type 0 and a root node whose
 * plane normal is unit length. */
static int IsDenormalWord(u32 w) {
    return (w & 0x7F800000) == 0 && (w & 0x007FFFFF) != 0;
}

static void Claim_VolumeShapeScan(Swapper *s, const u32 *pointers, u32 numPointers) {
    u32 i;

    for (i = 0; i < numPointers; i++) {
        u32 v = Target(s, pointers[i]), root;
        f32 x, y, z, len;

        if (v == ~0u || !InBlob(s, v, 0x2C) || Word(s, v + 8) != 0) {
            continue;
        }
        root = Target(s, v + 0x28);
        if (root == ~0u || !InBlob(s, root, 0x20) || s->base[root + 0x10] > 3) {
            continue;
        }
        /* a real plane normal has no denormal components; small integers read
         * as floats do (an effect record { 1, 0, 1.0f } passed for a unit
         * normal and the walk claimed its floats as bytes) */
        if (IsDenormalWord(Word(s, root)) || IsDenormalWord(Word(s, root + 4)) ||
            IsDenormalWord(Word(s, root + 8))) {
            continue;
        }
        x = BEFloatAt(s, root);
        y = BEFloatAt(s, root + 4);
        z = BEFloatAt(s, root + 8);
        len = x * x + y * y + z * z;
        if (len > 0.98f && len < 1.02f) {
            Claim_BSPN0(s, root);
        }
    }
}

/* ---- car AI scripts (AI_CarA.c) -------------------------------------------
 * Script: f32 speedScale, gripScale (0x0/0x4), f32s 0xC-0x18, u8 priority 0x26
 * (bytes 0x24-0x27), state tree root at 0x28.
 * State: { info (0x0), u32 numChildren (0x4), children (0x8, 0xC each) }.
 * Info (AI_Car_DetermineTarget): u8 type, subtype, sortMode, pad, pad, unk5,
 * limited, pad (0x0-0x7); f32 ranges 0x14-0x28; s32 timers 0x38-0x4C;
 * limits 0x58 -> { s32 x4, u8 mode 0x10, u8 compare 0x11 }; u32 thing id 0x5C. */
static void Claim_AIState(Swapper *s, u32 st, int depth) {
    u32 info, limits, children, n, i;

    if (st == ~0u || depth > 32) {
        return;
    }
    info = Target(s, st);
    if (info != ~0u) {
        ClaimBytes(s, info, 8);
        limits = Target(s, info + 0x58);
        if (limits != ~0u) {
            ClaimBytes(s, limits + 0x10, 4);
        }
    }
    children = Target(s, st + 0x8);
    n = Word(s, st + 0x4);
    for (i = 0; children != ~0u && i < n && i < 0x100; i++) {
        Claim_AIState(s, children + i * 0xC, depth + 1);
    }
}

static void Claim_AIScript(Swapper *s, u32 script) {
    if (script == ~0u) {
        return;
    }
    ClaimBytes(s, script + 0x24, 4);
    Claim_AIState(s, script + 0x28, 0);
}

static void Claim_Emitter(Swapper *s, u32 off);
static void Claim_World(Swapper *s, u32 off) {
    u32 list, n, i, k;

    list = Target(s, off + 0x0C);
    n = Word(s, off + 0x08);
    for (i = 0; list != ~0u && i < n && i < 0x100; i++) {
        ClaimBytes(s, list + i * 0x60 + 0x34, 4);
        ClaimBytes(s, list + i * 0x60 + 0x58, 8);
        for (k = 0; k < 4; k++) {   /* per-difficulty AI scripts 0x40-0x4C */
            Claim_AIScript(s, Target(s, list + i * 0x60 + 0x40 + k * 4));
        }
    }
    list = Target(s, off + 0x24);
    n = Word(s, off + 0x20);
    for (i = 0; list != ~0u && i < n && i < 0x1000; i++) {
        u32 il = Target(s, list + i * 0x50 + 0x30);
        Claim_BSPN0(s, Target(s, list + i * 0x50 + 0x1C));   /* visibility BSP */
        ClaimBytes(s, list + i * 0x50 + 0x34, 4);   /* s8 loaded, visible (World_Render) */
        if (il != ~0u) {
            u32 items = Target(s, il + 0x18), m = Word(s, il + 0x14), j;
            u32 statics = Target(s, il + 0x8), dyn = Target(s, il + 0x10);
            ClaimBytes(s, il, 4);   /* s8 dirty */
            for (j = 0; items != ~0u && j < m && j < 0x1000; j++) {
                ClaimU16(s, items + j * 0x40 + 0x30);
                ClaimU16(s, items + j * 0x40 + 0x32);
                ClaimBytes(s, items + j * 0x40 + 0x34, 4);
                ClaimBytes(s, items + j * 0x40 + 0x38, 1);
            }
            m = Word(s, il + 0x4);
            for (j = 0; statics != ~0u && j < m && j < 0x1000; j++) {
                ClaimBytes(s, statics + j * 0x40 + 0x34, 2);   /* hasScale, respawn */
                Claim_ThingRecord(s, Target(s, statics + j * 0x40 + 0x24));
            }
            m = Word(s, il + 0xC);
            for (j = 0; dyn != ~0u && j < m && j < 0x1000; j++) {
                ClaimBytes(s, dyn + j * 0x40 + 0x38, 1);       /* hasScale */
                Claim_Emitter(s, Target(s, dyn + j * 0x40 + 0x24));   /* FX point: an emitter */
            }
        }
    }
    /* tile paths 0x10/0x14 (GPTilePath, 0x20 each): record 0xC, u8 0x18-0x1B */
    list = Target(s, off + 0x14);
    n = Word(s, off + 0x10);
    for (i = 0; list != ~0u && i < n && i < 0x400; i++) {
        Claim_ThingRecord(s, Target(s, list + i * 0x20 + 0xC));
        ClaimBytes(s, list + i * 0x20 + 0x18, 4);
    }
    /* nav nodes 0x78 -> { nodes (0x60 each: u8 0x34-0x37, 0x50-0x53), count } */
    list = Target(s, off + 0x78);
    if (list != ~0u) {
        u32 nodes = Target(s, list);
        n = Word(s, list + 4);
        for (i = 0; nodes != ~0u && i < n && i < 0x2000; i++) {
            ClaimBytes(s, nodes + i * 0x60 + 0x34, 4);
            ClaimBytes(s, nodes + i * 0x60 + 0x50, 4);
        }
    }
    /* timer/goal tables 0x48/0x4C and 0x50/0x54: pointer tables to records
     * { u8 x4, f32, f32, 0xC -> { u8 type, u8 } } (InterfaceManager_MoveObject) */
    for (i = 0; i < 2; i++) {
        u32 t = Target(s, off + 0x4C + i * 8), m = Word(s, off + 0x48 + i * 8), j;
        for (j = 0; t != ~0u && j < m && j < 0x100; j++) {
            u32 e = Target(s, t + j * 4), d;
            if (e == ~0u) {
                continue;
            }
            ClaimBytes(s, e, 4);
            d = Target(s, e + 0xC);
            if (d != ~0u) {
                ClaimBytes(s, d, 2);
            }
        }
    }
    /* objectives 0x58/0x5C (8 each: u32, info -> { u8, u8 type, u8 id, u8 })
     * (InterfaceManager_PreprocessInterface, GameSettings_GetCodeVariable) */
    list = Target(s, off + 0x5C);
    n = Word(s, off + 0x58);
    for (i = 0; list != ~0u && i < n && i < 0x400; i++) {
        ClaimBytes(s, Target(s, list + i * 8 + 4), 4);
    }
    Claim_VolumeHolder(s, Target(s, off + 4));
    {
        u32 gd = Target(s, off + 0x9C);
        if (gd != ~0u) {
            u32 font = Target(s, gd + 0xC8);
            u32 fog = Target(s, gd + 0xC0);
            if (font != ~0u) {
                ClaimBytes(s, font + 4, 8);
            }
            ClaimBytes(s, gd, 4);   /* WorldFog: u8 type */
            Claim_IMHeader(s, Target(s, gd + 0xCC));   /* pause menu */
            if (fog != ~0u) {
                ClaimBytes(s, fog, 4);
            }
            Claim_CameraList(s, gd + 0x100);
            Claim_CameraList(s, gd + 0x108);
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
/* A slave trigger's data, by trigger type (ThingManagerList_TriggerSlaves):
 * 2 emitter (EmitterData: u8 x4), 3 sound sequence, 7 tracer, 5/6 attach (u8 x4,
 * scale keys 0xC and 0x14, color keys 0x1C), 8 motion (animated model),
 * 9 radar blip (u8 x4). */
/* Event sequence (EventSequence.c, slave type 1): script { count, entries
 * (0xC: u32 type, a, b) }.  b is an emitter:
 *   particle emitters (types 1-4, 7, 29: EventSeq_Advanced_GetInitData):
 *     s32 count 0x0-0x4, f32 0x8-0x1C, u8 0x20-0x22, offset 0x28 (kind
 *     0x21: 2 = { f32, f32, u8 0x8, u8 type 0x9, data 0xC: type 1 { Vector4,
 *     u8 0x10 }, 2 { f32 x4, u8 axis/damped/spin 0x10 } }), info 0x2C
 *     { Vector4, u8 0x10 } for velocity kind 0x22 == 4 (else f32 ranges);
 *   position emitters (5, 6, 8, 10, 25, 26): u8 0x18, 0x19, range 0x1C.
 * a by type: 1 particle (u8 0x0), 2 projectile, 3 nested script, 4 emitter (u8 x4),
 * 5 blast (u8 0x0-0x4), 6/7 destructible/physical record, 10 sound (u8 0x0),
 * 26 sound sequence. */
static void Claim_SoundSeq(Swapper *s, u32 off);

/* LineDef (LineManager.c): u8 0x0-0x2, f32 0x4-0x8, length keys 0xC (f32),
 * colour keys 0x14 */
static void Claim_LineDef(Swapper *s, u32 d) {
    if (d == ~0u || s->claim[d] == CLAIM_BYTES) {
        return;
    }
    ClaimBytes(s, d, 4);
    Claim_Track(s, d + 0xC, KEYS_F32);
    Claim_Track(s, d + 0x14, KEYS_COLOR);
}

/* Emitter (Emitter.c, EmitterData): u8 unk0, pickMode, limited, pad;
 * s32 count 0x4, delays 0x8/0xC; f32 range 0x10, speedScale 0x14; fx ids
 * 0x18/0x1C (u32); sound sequence 0x20 (SoundSeq_Init, thing type 0x10). */
static void Claim_Emitter(Swapper *s, u32 off) {
    if (off == ~0u || s->claim[off] == CLAIM_BYTES) {
        return;
    }
    ClaimBytes(s, off, 4);
    Claim_SoundSeq(s, Target(s, off + 0x20));
}

static void Claim_EventSeq(Swapper *s, u32 script) {
    static int depth;   /* type 3 entries nest scripts; data can cycle */
    u32 entries, n, i;

    if (script == ~0u || depth > 4) {
        return;
    }
    depth++;
    entries = Target(s, script + 4);
    n = Word(s, script);
    for (i = 0; entries != ~0u && i < n && i < 0x400; i++) {
        u32 e = entries + i * 0xC;
        u32 a = Target(s, e + 4), b = Target(s, e + 8);

        switch (Word(s, e)) {
        case 1: case 2: case 3: case 4: case 7: case 29:
            if (b != ~0u && s->claim[b + 0x20] != CLAIM_BYTES) {
                u32 adv = Target(s, b + 0x28), info = Target(s, b + 0x2C);
                ClaimBytes(s, b + 0x20, 4);
                if (s->base[b + 0x21] == 2 && adv != ~0u) {
                    u32 d = Target(s, adv + 0xC);
                    ClaimBytes(s, adv + 8, 4);
                    if (d != ~0u && (s->base[adv + 9] == 1 || s->base[adv + 9] == 2)) {
                        ClaimBytes(s, d + 0x10, 4);
                    }
                }
                if (info != ~0u && s->base[b + 0x22] == 4) {   /* else a range of f32s */
                    ClaimBytes(s, info + 0x10, 4);
                }
            }
            if (a != ~0u) {
                switch (Word(s, e)) {
                case 3:   /* a nested event sequence (EventSeq_Init: data 0x40) */
                    Claim_EventSeq(s, a);
                    break;
                case 4:   /* an emitter */
                    Claim_Emitter(s, a);
                    break;
                case 7:   /* a destructible / physical object record */
                    Claim_ThingRecord(s, a);
                    break;
                }
            }
            if (Word(s, e) == 2 && a != ~0u && s->claim[a] != CLAIM_BYTES) {
                /* projectile (Projectile.c): u8 0x3-0x5, 0x8; scale keys 0x8C,
                 * colour keys 0x94; slaves 0x9C (Projectile_Init:
                 * Thing_InitAllSlavesNow); smart target u16s 0xA8 */
                u32 tgt = Target(s, a + 0xA8);
                ClaimBytes(s, a, 0xC);
                Claim_Track(s, a + 0x8C, KEYS_VEC4);
                Claim_Track(s, a + 0x94, KEYS_COLOR);
                Claim_SlaveList(s, a + 0x9C);
                if (tgt != ~0u) {
                    ClaimU16(s, tgt);
                    ClaimU16(s, tgt + 2);
                }
            }
            if (Word(s, e) == 1 && a != ~0u) {
                /* particle (ParticleManager.c): u8 kind 0x0 (0 GX particle,
                 * 1 sprite, 2 line), anim 0x4 -> { u8 x4, size keys 0x28:
                 * Vector4 for particles, f32 for sprites }, colour keys 0x14 */
                u32 anim = Target(s, a + 4);
                u8 kind = s->base[a];
                ClaimBytes(s, a, 4);
                if (kind <= 2) {
                    Claim_Track(s, a + 0x14, KEYS_COLOR);
                }
                if (kind == 2) {
                    /* line (LineManager.c): 0x4 is a LineDef, plus LineDefs
                     * in the fx table 0x20 (count 0x1C) */
                    u32 fx = Target(s, a + 0x20), k;
                    Claim_LineDef(s, anim);
                    for (k = 0; fx != ~0u && k < Word(s, a + 0x1C) && k < 0x40; k++) {
                        Claim_LineDef(s, Target(s, fx + k * 4));
                    }
                } else if (anim != ~0u) {
                    ClaimBytes(s, anim, 4);
                    if (kind <= 1) {
                        Claim_Track(s, anim + 0x28, kind == 0 ? KEYS_VEC4 : KEYS_F32);
                    }
                }
            }
            break;
        case 5: case 6: case 8: case 10: case 25: case 26:
            if (b != ~0u) {
                ClaimBytes(s, b + 0x18, 4);
            }
            if (a != ~0u) {
                switch (Word(s, e)) {
                case 5:   /* blast: u8 0x0-0x4; damage 0x1C -> { u8 flags, u8 };
                           * flash (FlashDef): colour 0x20 -> { u8 type, colour
                           * keys 0x4 }, point keys 0x2C (also ForceFeedback) */
                    ClaimBytes(s, a, 8);
                    ClaimBytes(s, Target(s, a + 0x1C), 4);
                    {
                        u32 c = Target(s, a + 0x20);
                        if (c != ~0u) {
                            ClaimBytes(s, c, 4);
                            Claim_Track(s, c + 4, KEYS_COLOR);
                        }
                    }
                    Claim_Track(s, a + 0x2C, KEYS_POINT);
                    break;
                case 6:   /* a destructible record (Destructible_Init) */
                    Claim_ThingRecord(s, a);
                    break;
                case 10: ClaimBytes(s, a, 4); break;
                case 26: Claim_SoundSeq(s, a); break;
                }
            }
            break;
        }
    }
    depth--;
}

/* Event sequences are also referenced from records walked nowhere else
 * (dynamic world objects' shared slaves), so they are found by shape too. */
static int IsSlotOrNull(Swapper *s, u32 off) {
    return InBlob(s, off, 4) && (s->claim[off] == CLAIM_DONE || Word(s, off) == 0);
}

static int IsEventSeq(Swapper *s, u32 script) {
    u32 entries, n, i, emitters = 0;

    if (script == ~0u || !InBlob(s, script, 8)) {
        return 0;
    }
    n = Word(s, script);
    entries = Target(s, script + 4);
    if (n == 0 || n > 0x100 || entries == ~0u || !InBlob(s, entries, n * 0xC)) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        u32 e = entries + i * 0xC, type = Word(s, e);
        if (type > 40) {
            return 0;
        }
        switch (type) {   /* other entries hold values (repeat counts, times) */
        case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8:
        case 10: case 25: case 26: case 29:
            if (!IsSlotOrNull(s, e + 4) || Target(s, e + 8) == ~0u) {
                return 0;
            }
            emitters++;
            break;
        }
    }
    return emitters != 0;
}

static void Claim_EventSeqScan(Swapper *s, const u32 *pointers, u32 numPointers) {
    u32 i;

    for (i = 0; i < numPointers; i++) {
        u32 t = Target(s, pointers[i]);
        if (IsEventSeq(s, t)) {
            Claim_EventSeq(s, t);
        }
    }
}

static void Claim_SlaveData(Swapper *s, u8 type, u32 data) {
    if (data == ~0u) {
        return;
    }
    switch (type) {
    case 1:
        Claim_EventSeq(s, data);
        break;
    case 2:
    case 9:
        ClaimBytes(s, data, 4);
        break;
    case 3:
        Claim_SoundSeq(s, data);
        break;
    case 5:
    case 6:
        ClaimBytes(s, data, 4);
        Claim_Track(s, data + 0xC, KEYS_VEC4);
        Claim_Track(s, data + 0x14, KEYS_VEC4);
        Claim_Track(s, data + 0x1C, KEYS_COLOR);
        break;
    case 7:   /* tracer (Tracer.c): u8 blend 0x20, axis 0x21; colour keys 0x44, 0x4C */
        ClaimBytes(s, data + 0x20, 4);
        Claim_Track(s, data + 0x44, KEYS_COLOR);
        Claim_Track(s, data + 0x4C, KEYS_COLOR);
        break;
    case 8:
        Claim_MotionAnim(s, data);
        break;
    case 10:  /* shared slaves (SharedSlaveHolder_Get): { u32 active, slave list 0x4 } */
        Claim_SlaveList(s, data + 4);
        break;
    }
}

static void Claim_SlaveList(Swapper *s, u32 off) {
    static int depth;   /* shared slaves (type 10) nest slave lists */
    u32 t = Target(s, off + 4), n = Word(s, off), i;

    if (depth > 8) {
        return;
    }
    depth++;
    for (i = 0; t != ~0u && i < n && i < 0x400; i++) {
        u32 e = t + i * 0x20;
        if (s->claim[e + 0x10] == CLAIM_BYTES) {
            continue;   /* already walked (lists are shared) */
        }
        ClaimBytes(s, e + 0x10, 2);
        ClaimU16(s, e + 0x12);
        ClaimBytes(s, e + 0x1C, 4);   /* 0x1D: shared slave holder index, set at runtime */
        Claim_SlaveData(s, s->base[e + 0x10], Target(s, e + 0x18));
    }
    depth--;
}

/* Keyframe animation (Anim.c: Anim): s32 flags 0x4, numBones 0x10,
 * mask 0x14 (u32 bits), u16 per-bone channel flags at 0x1C. */
static void Claim_KeyAnim(Swapper *s, u32 off) {
    u32 flags, n, i;

    if (off == ~0u) {
        return;
    }
    flags = Target(s, off + 0x1C);
    n = Word(s, off + 0x10);
    for (i = 0; flags != ~0u && i < n && i < 0x100; i++) {
        ClaimU16(s, flags + i * 2);
    }
}

/* Animated model (AnimHandler.c: the motion state's model):
 *   numGeom 0x8, geom 0xC (8 each: geometry, u16 matrix, u16)
 *   skeleton 0x14 -> { numBones 0x0, base pose 0x10 (0x20 per bone: rotation,
 *   position, u8 numChildren/pop/next/pad at 0x1C) }
 *   keyframes 0x10 (Anim, MotionState_ExtractBlendedKeyFrame)
 *   u8 loop[3]/flags 0x1C-0x1F
 *   slave triggers 0x2C { count, triggers } (MotionState_BlendTriggerEvents) */
static void Claim_MotionAnim(Swapper *s, u32 off) {
    u32 p, n, i;

    if (off == ~0u || s->claim[off + 0x1C] == CLAIM_BYTES) {
        return;
    }
    ClaimBytes(s, off + 0x1C, 4);
    Claim_KeyAnim(s, Target(s, off + 0x10));
    Claim_SlaveList(s, off + 0x2C);
    p = Target(s, off + 0xC);
    n = Word(s, off + 0x8);
    for (i = 0; p != ~0u && i < n && i < 0x400; i++) {
        ClaimU16(s, p + i * 8 + 4);
        ClaimU16(s, p + i * 8 + 6);
    }
    p = Target(s, off + 0x14);
    if (p != ~0u) {
        u32 bones = Target(s, p + 0x10);
        n = Word(s, p);
        for (i = 0; bones != ~0u && i < n && i < 0x100; i++) {
            ClaimBytes(s, bones + i * 0x20 + 0x1C, 4);
        }
    }
}

/* Animated models are referenced from many object types (cars, item infos,
 * wildcards, scenery), so they are also found by shape: a model whose 0x14
 * points at a skeleton { numBones, ..., base pose 0x10 }.  A skeleton's bones
 * have unit quaternions, and their child counts (u8 0x1C) add up to
 * numBones - 1 with the root holding the rest of the tree. */
static f32 BEFloat(Swapper *s, u32 off) {
    union { u32 u; f32 f; } v;

    v.u = Word(s, off);
    return v.f;
}

static int IsSkeleton(Swapper *s, u32 skel) {
    u32 n, bones, i, children = 0;

    if (skel == ~0u || !InBlob(s, skel, 0x14)) {
        return 0;
    }
    n = Word(s, skel);
    bones = Target(s, skel + 0x10);
    if (n == 0 || n > 0x100 || bones == ~0u || !InBlob(s, bones, n * 0x20)) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        u32 b = bones + i * 0x20;
        f32 x = BEFloat(s, b), y = BEFloat(s, b + 4), z = BEFloat(s, b + 8), w = BEFloat(s, b + 12);
        f32 len = x * x + y * y + z * z + w * w;
        if (!(len > 0.9f && len < 1.1f)) {
            return 0;
        }
        children += s->base[b + 0x1C];
    }
    return children == n - 1;
}

/* Other records point at skeletons too (a destructible's motion record has
 * one at 0x14 as well), so a model must also have a frame rate at 0x18. */
static int IsMotionAnim(Swapper *s, u32 m) {
    f32 fps;

    if (m == ~0u || !InBlob(s, m, 0x34) || Word(s, m) > 0x100 || Word(s, m + 8) > 0x400) {
        return 0;
    }
    fps = BEFloat(s, m + 0x18);
    return fps >= 1.0f && fps <= 1000.0f && IsSkeleton(s, Target(s, m + 0x14));
}

static void Claim_ModelScan(Swapper *s, const u32 *pointers, u32 numPointers) {
    u32 i;

    for (i = 0; i < numPointers; i++) {
        u32 m = Target(s, pointers[i]);
        if (IsMotionAnim(s, m)) {
            Claim_MotionAnim(s, m);
        }
    }
}

/* Keyframe animations hang off many records (movers' tracks, cars, models),
 * so they are also found by shape: numBones 0x10, pointer slots for flags
 * 0x1C, times 0x20 and data 0x24, a time range 0x8-0xC and a frame count
 * 0x28. */
static int IsKeyAnim(Swapper *s, u32 a) {
    u32 bones, frames;
    f32 t0, t1;

    if (a == ~0u || !InBlob(s, a, 0x34)) {
        return 0;
    }
    bones = Word(s, a + 0x10);
    frames = Word(s, a + 0x28);
    t0 = BEFloat(s, a + 0x8);
    t1 = BEFloat(s, a + 0xC);
    return bones != 0 && bones <= 0x100 && frames != 0 && frames <= 0x10000 &&
           t0 > -1e7f && t1 < 1e7f && t0 <= t1 &&
           Target(s, a + 0x1C) != ~0u && Target(s, a + 0x20) != ~0u && Target(s, a + 0x24) != ~0u &&
           InBlob(s, Target(s, a + 0x1C), bones * 2);
}

static void Claim_KeyAnimScan(Swapper *s, const u32 *pointers, u32 numPointers) {
    u32 i;

    for (i = 0; i < numPointers; i++) {
        u32 a = Target(s, pointers[i]);
        if (IsKeyAnim(s, a)) {
            Claim_KeyAnim(s, a);
        }
    }
}

static void Claim_CarAInfo(Swapper *s, u32 off) {
    u32 p, n, i;

    for (i = 0; i < 4; i++) {   /* per-difficulty AI scripts 0x90-0x9C */
        Claim_AIScript(s, Target(s, off + 0x90 + i * 4));
    }
    ClaimBytes(s, off + 0x10, 4);
    ClaimU16(s, off + 0x14);
    ClaimU16(s, off + 0x16);
    p = Target(s, off + 0x6C);
    if (p != ~0u) {
        ClaimBytes(s, p, 4);
    }
    p = Target(s, off + 0x78);   /* modelRef: model 0x10, additive anims 0x14-0x1C */
    if (p != ~0u) {
        u32 m = Target(s, p + 0x10);
        Claim_MotionAnim(s, m);
        /* the model's optional parts 0x38 -> { count, parts (0x1C each: u8 flags,
         * u8 xform, u16 matrix 0x4, geometry 0x8, offset 0xC) } (CarA_DrawScript) */
        if (m != ~0u) {
            u32 list = Target(s, m + 0x38), parts;
            if (list != ~0u && (parts = Target(s, list + 4)) != ~0u) {
                n = Word(s, list);
                for (i = 0; i < n && i < 0x100; i++) {
                    ClaimBytes(s, parts + i * 0x1C, 4);
                    ClaimU16(s, parts + i * 0x1C + 4);
                    ClaimU16(s, parts + i * 0x1C + 6);
                }
            }
        }
        Claim_KeyAnim(s, Target(s, p + 0x14));
        Claim_KeyAnim(s, Target(s, p + 0x18));
        Claim_KeyAnim(s, Target(s, p + 0x1C));
    }
    Claim_KeyAnim(s, Target(s, off + 0x48));   /* steering animation */
    p = Target(s, off + 0x80);   /* engine: cameras 0x80 (1P), 0x88 (split) */
    if (p != ~0u) {
        Claim_CameraList(s, p + 0x80);
        Claim_CameraList(s, p + 0x88);
    }
    p = Target(s, off + 0x84);   /* HUD interfaces: 1P 0x10, 2P 0x18, 3-4P 0x1C */
    if (p != ~0u) {
        ClaimBytes(s, p + 0x34, 4);   /* u8 stat bars (GSI_VehicleSelectionInfo) */
        Claim_IMHeader(s, Target(s, p + 0x10));
        Claim_IMHeader(s, Target(s, p + 0x18));
        Claim_IMHeader(s, Target(s, p + 0x1C));
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
    for (i = 0; p != ~0u && i < 11; i++) {
        /* 12-byte records with a slave list at +4: info->slaves + 0x4 ... 0x7C
         * (MotionState_InitAllSlavesNow from CarA.c, CarA_HW.c, CarA_Suspension.c) */
        Claim_SlaveList(s, p + i * 0xC + 4);
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
/* ---- geometry (Render_Geometry_.c, Gfx_GeometryHandler.c) ----------------
 * Geometry { u16 0x0, u16 numLODs 0x2, u16 0x4, u16 0x6, lods 0x8,
 *   lodDistances 0xC (f32[]), f32 bbox/center 0x10.. }
 * LOD (0x18 each) { u16 0x0, u16 numGroups 0x2, u16 vtxFlags 0x4, u16 0x6,
 *   groups 0x8, vertex buffer 0xC, u32 0x10, u32 0x14 }
 * Group (0x20 each) { u16 0x0, u16 flags 0x2, GXColor 0x4, u32 0x8,
 *   uvScroll 0xC -> { f32 u, f32 v, GXColor 0x8, u32 flags }, dlSize 0x10,
 *   u16 0x14, u16 0x16, u16 texture 0x18, u16 texture 0x1A, displayList 0x1C }
 * Vertex buffer { uvOff, colorOff, normalOff, size; data 0x10: f32 positions,
 *   f32 UVs, RGBA8 colors, f32 normals }.  Display lists stay big-endian
 *   (gx_fifo.c decodes them as the hardware would).
 * Geometry is referenced from many object types, so it is found by shape:
 * every level has to have pointer slots where the layout puts them. */
static u16 Half(Swapper *s, u32 off) {
    return InBlob(s, off, 2) ? Port_ReadBE16(s->base + off) : 0;
}

static int IsGeometry(Swapper *s, u32 g) {
    u32 n = Half(s, g + 2), lods = Target(s, g + 8), i, j;

    if (n == 0 || n > 8 || lods == ~0u || Target(s, g + 0xC) == ~0u) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        u32 lod = lods + i * 0x18;
        u32 ng = Half(s, lod + 2), grp = Target(s, lod + 8);
        if (ng == 0 || ng > 0x400 || grp == ~0u || Target(s, lod + 0xC) == ~0u) {
            return 0;
        }
        for (j = 0; j < ng; j++) {
            u32 g2 = grp + j * 0x20, dl = Target(s, g2 + 0x1C), size = Word(s, g2 + 0x10);
            if (dl == ~0u || size == 0 || !InBlob(s, dl, size)) {
                return 0;
            }
        }
    }
    return 1;
}

static void Claim_VertexBuffer(Swapper *s, u32 vb) {
    u32 colors = Word(s, vb + 4), normals = Word(s, vb + 8), end = Word(s, vb + 0xC);

    if (vb == ~0u || s->claim[vb] == CLAIM_U32) {
        return;
    }
    Claim(s, vb, 0x10, CLAIM_U32);
    if (colors != 0) {
        if (normals > colors && normals < end) {
            end = normals;
        }
        if (end > colors) {
            ClaimBytes(s, vb + 0x10 + colors, end - colors);
        }
    }
}

/* Shadow volume model (Render_DrawShadows): same header as a geometry, but
 * 0x8 -> groups (0x20 each: u16 0x0, u16 flags 0x2 (0x4000), u8 colour 0x4-0x7,
 * numEdges 0x10, edges 0x1C) and 0xC -> f32[3] positions. Edges (0x20): f32
 * normal 0x0, u16 v0 0xC, u16 v1 0xE, f32 normal 0x10, s32 type 0x1C. */
static int IsShadowGeometry(Swapper *s, u32 g) {
    u32 groups = Target(s, g + 8);
    return groups != ~0u && (Half(s, groups + 2) & 0x4000) != 0;
}

static void Claim_ShadowGeometry(Swapper *s, u32 g) {
    u32 n = Half(s, g + 2), groups = Target(s, g + 8), i, j;

    ClaimU16(s, g);
    ClaimU16(s, g + 2);
    ClaimU16(s, g + 4);
    ClaimU16(s, g + 6);
    for (i = 0; groups != ~0u && i < n && i < 0x100; i++) {
        u32 grp = groups + i * 0x20, edges = Target(s, grp + 0x1C), ne = Word(s, grp + 0x10);
        ClaimU16(s, grp);
        ClaimU16(s, grp + 2);
        ClaimBytes(s, grp + 4, 4);
        for (j = 0; edges != ~0u && j < ne && j < 0x10000; j++) {
            ClaimU16(s, edges + j * 0x20 + 0xC);
            ClaimU16(s, edges + j * 0x20 + 0xE);
        }
    }
}

static void Claim_Geometry(Swapper *s, u32 g) {
    u32 n, lods, i, j;

    if (s->claim[g] == CLAIM_U16) {
        return;
    }
    if (IsShadowGeometry(s, g)) {
        Claim_ShadowGeometry(s, g);
        return;
    }
    ClaimU16(s, g);
    ClaimU16(s, g + 2);
    ClaimU16(s, g + 4);
    ClaimU16(s, g + 6);
    n = Half(s, g + 2);
    lods = Target(s, g + 8);
    for (i = 0; i < n; i++) {
        u32 lod = lods + i * 0x18;
        u32 ng = Half(s, lod + 2), grp = Target(s, lod + 8);
        /* a car's shadow model is a geometry whose LOD level is the shadow
         * volume (groups flagged 0x4000: edge lists, not display lists) */
        if (IsShadowGeometry(s, lod)) {
            Claim_ShadowGeometry(s, lod);
            continue;
        }
        ClaimU16(s, lod);
        ClaimU16(s, lod + 2);
        ClaimU16(s, lod + 4);
        ClaimU16(s, lod + 6);
        Claim_VertexBuffer(s, Target(s, lod + 0xC));
        for (j = 0; j < ng; j++) {
            u32 g2 = grp + j * 0x20, uv = Target(s, g2 + 0xC);
            ClaimU16(s, g2);
            ClaimU16(s, g2 + 2);
            ClaimBytes(s, g2 + 4, 4);
            ClaimU16(s, g2 + 0x14);
            ClaimU16(s, g2 + 0x16);
            ClaimU16(s, g2 + 0x18);   /* no-ops where a texture slot sits */
            ClaimU16(s, g2 + 0x1A);
            if (uv != ~0u) {
                ClaimBytes(s, uv + 8, 4);
            }
            ClaimBytes(s, Target(s, g2 + 0x1C), Word(s, g2 + 0x10));
        }
    }
}

static void Claim_GeometryScan(Swapper *s, const u32 *pointers, u32 numPointers) {
    u32 i;

    for (i = 0; i < numPointers; i++) {
        u32 g = Target(s, pointers[i]);
        if (g != ~0u && IsGeometry(s, g)) {
            Claim_Geometry(s, g);
        }
    }
}

/* f32 LOD distances (geometry 0xC), one per level: claim them, or the string
 * heuristic can take one for text (1e9f is "Nnk(\0" in a weapon flame). Run
 * after the geometry walk and only over unclaimed bytes: a LOD level can pass
 * IsGeometry too, and its 0xC is a vertex buffer that must keep its claims. */
static void Claim_LODDistances(Swapper *s, const u32 *pointers, u32 numPointers) {
    u32 i, k;

    for (i = 0; i < numPointers; i++) {
        /* every pointer slot that is some geometry's 0xC, however the
         * geometry itself is reached (pointer, export reloc, import) */
        u32 g = pointers[i] - 0xC, dist;
        if (pointers[i] < 0xC || !IsGeometry(s, g) || (dist = Target(s, g + 0xC)) == ~0u) {
            continue;
        }
        for (k = 0; k < Half(s, g + 2); k++) {
            if (InBlob(s, dist + k * 4, 4) && s->claim[dist + k * 4] == CLAIM_DEFAULT) {
                Claim(s, dist + k * 4, 4, CLAIM_U32);
            }
        }
    }
}

/* Local relocations (slot, id with pack id 0) are pointers too: the export
 * table at the start of a shared geometry pack (type 8, HW_Cars/HW_share:
 * geometry offsets that other packs import by index) is relocated this way
 * rather than through the pointer list. */
static void Claim_GeometryScanRelocs(Swapper *s, const u32 *relocs, u32 numRelocs) {
    u32 i;

    for (i = 0; i < numRelocs; i++) {
        u32 g;
        if (relocs[i * 2 + 1] & 0xFFFF0000) {
            continue;  /* another pack's export: the value is an index */
        }
        g = Target(s, relocs[i * 2]);
        if (g != ~0u && IsGeometry(s, g)) {
            Claim_Geometry(s, g);
        }
    }
}

/* ---- triggers and actions (Trigger.c, Action.c; pack triggerOffset) ------
 * Trigger (0x2C): type 0x0, param 0x4, conditions 0xC (count 0x10; 0x2C-byte
 * triggers whose type/param are tested), actions 0x14 / else-actions 0x18
 * (counts 0x1C / 0x20), state 0x24 (written at run time), u8 mode 0x28.
 * Action (0x18): type 0x0, data 0x4, delay 0x14.  Data by action type
 * (pActionFuncArray): 4 InterfacePlay { header }, 27-30 interface packs
 * { u32 id, char name[] }, 31 EndInterface { u32, header 0x4, 0x8 }. */
static void Claim_TACActions(Swapper *s, u32 list, u32 n) {
    u32 i;

    for (i = 0; list != ~0u && i < n && i < 0x100; i++) {
        u32 a = list + i * 0x18, data = Target(s, a + 4), k;

        if (data == ~0u) {
            continue;
        }
        switch (Word(s, a)) {
        case 4:
            Claim_IMHeader(s, Target(s, data));
            break;
        case 31:
            Claim_IMHeader(s, Target(s, data + 4));
            break;
        case 27:
        case 28:
        case 29:
        case 30:
            for (k = 4; k < 0x40 && InBlob(s, data + k, 1) && s->base[data + k] != 0; k++) {
            }
            ClaimBytes(s, data + 4, (k - 4 + 4) & ~3u);
            break;
        }
    }
}

static void Claim_TACTrigger(Swapper *s, u32 t, int depth) {
    u32 conds = Target(s, t + 0xC), n = Word(s, t + 0x10), i;

    ClaimBytes(s, t + 0x28, 4);
    if (Word(s, t) == 7) {  /* InterfaceEnd: param -> { header } */
        u32 p = Target(s, t + 4);
        if (p != ~0u) {
            Claim_IMHeader(s, Target(s, p));
        }
    }
    Claim_TACActions(s, Target(s, t + 0x14), Word(s, t + 0x1C));
    Claim_TACActions(s, Target(s, t + 0x18), Word(s, t + 0x20));
    for (i = 0; depth < 2 && conds != ~0u && i < n && i < 0x40; i++) {
        Claim_TACTrigger(s, conds + i * 0x2C, depth + 1);
    }
}

static void Claim_TACTriggers(Swapper *s, u32 off, u32 count) {
    u32 i;

    for (i = 0; i < count && InBlob(s, off + i * 0x2C, 0x2C); i++) {
        Claim_TACTrigger(s, off + i * 0x2C, 0);
    }
}

/* ---- items (type 2 packs: the root is the item's ItemInfo) ----------------
 * 0x0 u8 type, 0x1 u8, 0x2 u16 value (CarA_ProcessItemPickup); 0x30 -> weapon /
 * wildcard data (card 0x0: a motion animation; u8 spinType 0x2C, spinLimit
 * 0x2D, u16 spinBone 0x2E; u8 priority 0x34); shared slaves
 * { count, list } at 0x38 (Item_Init, Item_Draw). */
static void Claim_ItemInfo(Swapper *s, u32 off) {
    u32 sub;

    ClaimBytes(s, off, 2);
    ClaimU16(s, off + 2);
    sub = Target(s, off + 0x30);
    if (sub != ~0u) {
        /* 0x0 -> the wildcard card: a motion animation whose slave triggers
         * (0x2C) fire the weapon (WildcardState_Heartbeat ->
         * MotionState_BlendTriggerEvents): event sequences with the projectile */
        u32 card = Target(s, sub);
        if (card != ~0u && IsMotionAnim(s, card)) {
            Claim_MotionAnim(s, card);
        }
        ClaimBytes(s, sub + 0x2C, 2);   /* u8 spinType, spinLimit (WildCard.c) */
        ClaimU16(s, sub + 0x2E);        /* spinBone */
        ClaimBytes(s, sub + 0x34, 4);   /* u8 priority (AI weapons) */
    }
    Claim_SlaveList(s, off + 0x38);
}

static void Claim_Roots(Swapper *s) {
    u32 table = Target(s, 4);
    u32 n = Word(s, 0);
    u32 i;

    if (s->packType == 4) {
        Claim_World(s, 0);  /* world packs: the world itself is at the base */
        return;
    }
    if (s->packType == 5) {
        Claim_Collision(s);  /* collision packs: the grid is at the base */
        return;
    }
    if (s->packType == 8) {
        return;  /* shared packs start with an export table instead */
    }
    for (i = 0; table != ~0u && i < n && InBlob(s, table + i * 8, 8); i++) {
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
        case 2: /* items */
            Claim_ItemInfo(s, obj);
            break;
        case 9: /* audio packs: each root is a sound sequence (briefing dialogue,
                 * InterfaceManager_MoveDialogSystem -> SoundSequence_Play) */
            Claim_SoundSeq(s, obj);
            break;

        default:
            break;
        }
    }
}

/* ---- entry point -------------------------------------------------------- */

static void Swapper_Init(Swapper *s, u8 *base, u32 size, s32 packType) {
    s->base = base;
    s->size = size;
    s->packType = packType;
    s->claim = calloc(size ? size : 1, 1);
    if (s->claim == NULL) {
        Port_Fatal("out of memory swapping a data pack");
    }
}

/* Slots the loader rewrites (pointers relocated, texture ids filled in):
 * their blob values are offsets or ids, read big-endian now. */
static void Swapper_MarkSlots(Swapper *s, const u32 *offs, u32 count, u32 stride) {
    u32 i;

    for (i = 0; i < count; i++) {
        u32 off = offs[i * stride];
        if (InBlob(s, off, 4)) {
            *(u32 *)(s->base + off) = Port_ReadBE32(s->base + off);
            memset(s->claim + off, CLAIM_DONE, 4);
        }
    }
}

/* Swap everything by its claim; unclaimed words get the 32-bit default. */
static void Swapper_Finish(Swapper *s, const char *name) {
    u32 i;

    for (i = 0; i + 4 <= s->size; i += 4) {
        u8 *c = s->claim + i;
        u8 *p = s->base + i;

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
    PortSwapCheck_Register(s->base, s->size, s->claim, name, s->packType);
}

void PortSwap_PackData(u8 *relocTables, u32 relocTableWords, u8 *base, u32 dataSize,
                       u32 *texRelocs, u32 numTexRelocs, u32 *pointers, u32 numPointers,
                       u32 textureDataOffset, u32 numTextures, u32 shaderOffset, u32 numShaders,
                       u32 triggerOffset, u32 numTriggers) {
    Swapper s;

    Port_SwapArray32(relocTables, relocTableWords);
    Port_SwapArray32(texRelocs, numTexRelocs * 2);
    Port_SwapArray32(pointers, numPointers);

    Swapper_Init(&s, base, dataSize, g_portSwapPackType);
    /* pointer slots: blob offsets, relocated by the caller afterwards;
     * texture-id slots: overwritten, or relocated like pointers */
    Swapper_MarkSlots(&s, pointers, numPointers, 1);
    Swapper_MarkSlots(&s, texRelocs, numTexRelocs, 2);

    if (numTextures != 0) {
        Claim_Textures(&s, textureDataOffset, numTextures);
    }
    Claim_GeometryScan(&s, pointers, numPointers);
    Claim_GeometryScanRelocs(&s, texRelocs, numTexRelocs);
    Claim_ModelScan(&s, pointers, numPointers);
    Claim_EventSeqScan(&s, pointers, numPointers);
    Claim_KeyAnimScan(&s, pointers, numPointers);
    Claim_VolumeShapeScan(&s, pointers, numPointers);
    Claim_Roots(&s);
    Claim_TACTriggers(&s, triggerOffset, numTriggers);
    Claim_Shaders(&s, shaderOffset, numShaders);
    Claim_LODDistances(&s, pointers, numPointers);
    Claim_StringTargets(&s, pointers, numPointers);
    Swapper_Finish(&s, g_portSwapPackName);
}

/* ---- quad packs (DataGamePackLoad.c: DataPack_LoadQuadPack) ----------------
 * A world quadrant's geometry, streamed from the world's _geo file:
 * header 0x54 (QuadPackHeader: 11 words, then two 0x14-byte pack refs),
 * relocs (numRelocs pairs: texture-id slot, texture), shader list (pairs),
 * pointer relocs (numTexRelocs pairs: slot, pack id << 16 | index),
 * pointers (numPointers), dataSize bytes, then the GX data the slots are
 * relative to.  `size` is the whole pack. */
void PortSwap_QuadPack(u8 *data, u32 size, u32 index) {
    char name[32];
    u32 *hdr = (u32 *)data;
    u32 nRelocs, nShaders, nTexRelocs, nPointers, offset;
    u32 *relocs, *texRelocs, *pointers;
    Swapper s;

    if (size < 0x54) {
        return;
    }
    PortSwap_PackHeader(hdr, 11, 2, 0x14);
    if (hdr[0] == 0) {
        return;  /* not valid: the loader stops here */
    }
    nPointers = hdr[1];
    nRelocs = hdr[2];
    nShaders = hdr[7];
    nTexRelocs = hdr[10];
    offset = 0x54;
    relocs = (u32 *)(data + offset);
    offset += nRelocs * 8;
    offset += nShaders * 8;
    texRelocs = (u32 *)(data + offset);
    offset += nTexRelocs * 8;
    pointers = (u32 *)(data + offset);
    offset += nPointers * 4;
    if (offset > size) {
        Port_Fatal("quad pack %u: tables overrun the pack", index);
    }
    Port_SwapArray32(relocs, (nRelocs + nShaders + nTexRelocs) * 2 + nPointers);
    offset += hdr[9];  /* dataSize */
    if (offset > size) {
        Port_Fatal("quad pack %u: data overruns the pack", index);
    }

    Swapper_Init(&s, data + offset, size - offset, 7);
    Swapper_MarkSlots(&s, pointers, nPointers, 1);
    Swapper_MarkSlots(&s, texRelocs, nTexRelocs, 2);
    Swapper_MarkSlots(&s, relocs, nRelocs, 2);
    Claim_GeometryScan(&s, pointers, nPointers);
    Claim_GeometryScanRelocs(&s, texRelocs, nTexRelocs);
    Claim_LODDistances(&s, pointers, nPointers);
    Claim_StringTargets(&s, pointers, nPointers);
    snprintf(name, sizeof(name), "quad#%u", index);
    Swapper_Finish(&s, name);
}

/* ---- music (MIDI.c: CreateMIDI) -------------------------------------------
 * Called on CreateMIDI's heap copies; the pack keeps the big-endian original.
 * Song: RIFX 'MIDI' around a standard MIDI file. The players read the RIFX
 * size (0x4) and the MThd fields (size 0x10, format/tracks/division 0x14-
 * 0x18) as words; MTrk (0x1A) event data is a byte stream.
 * Bank: RIFX 'sndb': u32 size 0x4, version 0xC, instrument count 0x10, table
 * size 0x14 (sample data follows the table), 0x18, 0x1C; instruments (0x20
 * each, MIDI_INST / MIDISample_): u32 sample address, length, loop, rate
 * (0x0-0xC); u8 0x10, bits 0x11, 0x12, 0x13, program 0x14, 0x15, key range
 * 0x16/0x17; u16 envelope 0x18, 0x1A, sustain 0x1C, 0x1E. Sample data stays
 * big-endian, like the hardware reads it from ARAM. */
void PortSwap_MIDI(u8 *song, u8 *bank) {
    u32 i, n;

    *(u32 *)(song + 0x4) = Port_ReadBE32(song + 0x4);
    *(u32 *)(song + 0x10) = Port_ReadBE32(song + 0x10);
    for (i = 0x14; i <= 0x18; i += 2) {
        *(u16 *)(song + i) = Port_ReadBE16(song + i);
    }
    *(u32 *)(song + 0x1E) = Port_ReadBE32(song + 0x1E);

    for (i = 0x4; i < 0x20; i += 4) {
        if (i != 0x8) {   /* 0x8 is the 'sndb' id, compared as bytes */
            *(u32 *)(bank + i) = Port_ReadBE32(bank + i);
        }
    }
    n = *(u32 *)(bank + 0x10);
    for (i = 0; i < n && 0x20 + (i + 1) * 0x20 <= *(u32 *)(bank + 0x14); i++) {
        u8 *e = bank + 0x20 + i * 0x20;
        Port_SwapArray32(e, 4);
        *(u16 *)(e + 0x18) = Port_ReadBE16(e + 0x18);
        *(u16 *)(e + 0x1A) = Port_ReadBE16(e + 0x1A);
        *(u16 *)(e + 0x1C) = Port_ReadBE16(e + 0x1C);
        *(u16 *)(e + 0x1E) = Port_ReadBE16(e + 0x1E);
    }
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

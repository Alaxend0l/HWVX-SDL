/* Mixed builds (Port_Mix.h): a pack's textures and sounds from the releases
 * the recipe names, as the pack loads from its base release. Port-only file.
 *
 * The other releases' copies are read straight from their files (TOC, pack
 * header, the blob's texture table or the sound data's descriptors; nothing
 * of them is loaded as a pack), paired through Port_MixPairs.c, and checked
 * against the base's copy before they are used: an asset that doesn't pair,
 * doesn't read or doesn't look alike stays the base's.
 *
 * A texture keeps the base's header slot (so every reference to it holds)
 * and gets the chosen release's format, size, levels and pixels, in a block
 * the pack keeps: the GameCube's as stored (CMPR, RGBA8), the PC's as stored
 * (PORT_TEX_PC | format), the PS2's as RGBA8 (its palettes expanded). In the
 * PC look every GameCube texture left is stored as RGBA8 in the PC's colours
 * (sqrt(255 v)): the port squares PC textures for the GameCube look, never
 * the reverse.
 *
 * A sound keeps the base's descriptor and gets the chosen release's PCM16
 * samples, length, rate and loop (GameCube or PC; the PS2's ADPCM is never
 * the better copy). Song banks and songs stay the base's.
 *
 * The PS2 prototype is a fourth source (PORT_MIX_PROTO), in its import mode
 * "replace" only (Port_MixProtoReplace, no recipe): each pack loads as its
 * release has it and takes the prototype's copy of every texture and sound
 * the pairing table pairs with it (its column of Port_MixPairs.c), read
 * from its file in its own layout (version 177/179: TOC offsets from the
 * end of each entry, no sector padding, the pointer tables before the blob,
 * a level count one too many, RGB24 textures) and checked as any other's.
 * Its sounds are SPU-ADPCM: decoded to PCM16 (Port_PS2.c). Its textures are
 * in the PC's colours, as the PS2's; the base's stay as they are. */
#include "common.h"
#include "Voice.h"
#include "Port_Swap.h"
#include "xdl.h"
#include "Port_Mix.h"
#include "Port_IfaceScan.h"
#include "Interface.h"

static int InBlobOf(const PortIfaceScan *s, u32 off, u32 n) {
    return off <= s->size && n <= s->size - off;
}

enum { GXF_RGBA8 = 6, GXF_CMPR = 14 };
enum { HDR_WORDS = 20, HDR_SIZE = HDR_WORDS * 4 + 2 * sizeof(DataPackRef), TOC_ENTRY = 0x18 };
/* DataPackFileHeader's words (DataGamePackLoad.c) */
enum { H_DATASIZE, H_SOUNDSIZE, H_NUMSOUNDPTRS, H_NUMPTRS, H_NUMRELOCS, H_NUMTEX, H_TEXOFF, H_NUMSOUNDRELOCS,
       H_NUMSOUNDS, H_SFXOFF, H_PALETTES, H_PALOFF, H_NUMSHADERS, H_NUMGX, H_NUMTEXRELOCS };

#define TEX_LIMIT 24.0f     /* thumbnail distance under which two textures are the same art (pairing.md §4) */
#define FONT_LIMIT 12.0f    /* GlyphDistance under which two font pages have the same glyphs */
#define MAX_TEX 0x400

static const char *const s_typeNames[11] = { "car", "interface", "item", "link", "world", "collision",
                                             "world_texture", "geometry", "share", "audio", "music" };
static const char *const s_relNames[PORT_MIX_RELS] = { "gc", "pc", "ps2", "proto" };

/* The PS2 release, or the prototype: textures and sounds of the PS2's kinds. */
static int IsPS2(int rel) {
    return rel == PORT_DATA_PS2 || rel == PORT_MIX_PROTO;
}

static int Mixing(void) {
    return Port_MixActive() || Port_MixProtoReplace();
}

/* The pack being loaded; packs load inside packs (the ones one refers to). */
typedef struct MixPack {
    char file[0xA0];        /* as DVDOpen gets it: lower case, '/', ".xgc" */
    char archive[0x40];
    u32 type, id;
    const PortMixPairPack *pairs;
    u32 protoTex, protoSfx;     /* assets taken from the prototype (mode "replace") */
} MixPack;

/* per thread: the loading thread loads its own packs */
static _Thread_local MixPack s_stack[8];
static _Thread_local int s_depth;

static struct {
    u32 packs, texFrom[PORT_MIX_RELS], texKept, texRejected, texConverted;
    /* interface art (Port_IfaceTextures, or a recipe rule "interface") and font pages, by release;
     * art the set has no copy of (the base's kept), font pages whose PC copy has other glyphs,
     * interface textures of packs that aren't interface packs (some are world geometry's too) */
    u32 artFrom[PORT_MIX_RELS], fontFrom[PORT_MIX_RELS], artFallback, artByShape, artByObject, fontOther, uiElsewhere, uiShared;
    u32 sfxFrom[PORT_MIX_RELS], sfxKept, sfxRejected;
} s_stats;

/* Textures of share packs that other packs' interfaces show (the car packs'
 * HUDs draw HW_Cars share_2's gauges): found by the builder's first pass,
 * which loads every pack and only looks (s_collect), since a share pack is
 * mixed before the packs that refer to it load. */
typedef struct UiRef {
    char archive[0x10];
    u32 id;
    u16 index;
    u8 what;    /* PORT_IFACE_ART or _FONT */
} UiRef;

static UiRef *s_uiRefs;
static u32 s_numUiRefs, s_capUiRefs;
static int s_collect;

static const char *StemOf(const char *name) {
    const char *stem = name, *c;

    for (c = name; *c; c++) {
        if (*c == '/' || *c == '\\') {
            stem = c + 1;
        }
    }
    return stem;
}

/* `index`: a texture's, or for PORT_IFACE_GEOM the word index of the slot holding a geometry */
static UiRef *FindUiRef(const char *archive, u32 id, u32 index, int geom) {
    u32 i;

    for (i = 0; i < s_numUiRefs; i++) {
        if (s_uiRefs[i].id == id && s_uiRefs[i].index == index && (s_uiRefs[i].what == PORT_IFACE_GEOM) == geom &&
            strcasecmp(s_uiRefs[i].archive, archive) == 0) {
            return &s_uiRefs[i];
        }
    }
    return NULL;
}

typedef struct OtherCtx {
    DataPack *pack;
} OtherCtx;

static void NoteUiRef(void *user, u32 packId, u32 index, u8 what) {
    DataPack *pack = ((OtherCtx *)user)->pack;
    const DataPackRef *ref = pack->refs[0].id == packId ? &pack->refs[0] : pack->refs[1].id == packId ? &pack->refs[1] : NULL;
    char name[0x10];
    UiRef *r;

    if (ref == NULL) {
        return;
    }
    memcpy(name, ref->name, 0xC);
    name[0xC] = 0;
    if ((r = FindUiRef(StemOf(name), packId, index, what == PORT_IFACE_GEOM)) != NULL) {
        if (what == PORT_IFACE_ART) {
            r->what = what;     /* art wins over font, as in one pack */
        }
        return;
    }
    if (s_numUiRefs == s_capUiRefs) {
        UiRef *n = realloc(s_uiRefs, (s_capUiRefs * 2 + 64) * sizeof(UiRef));

        if (n == NULL) {
            return;
        }
        s_uiRefs = n;
        s_capUiRefs = s_capUiRefs * 2 + 64;
    }
    r = &s_uiRefs[s_numUiRefs++];
    snprintf(r->archive, sizeof(r->archive), "%s", StemOf(name));
    r->id = packId;
    r->index = (u16)index;
    r->what = what;
}

static MixPack *Cur(void) {
    return Mixing() && s_depth > 0 && s_depth <= 8 ? &s_stack[s_depth - 1] : NULL;
}

void PortMix_BeginPack(const char *name, u32 type, u32 id) {
    MixPack *p;
    const char *stem = name, *c;
    u32 i;

    if (!Mixing()) {
        return;
    }
    if (++s_depth > 8) {
        return;
    }
    p = &s_stack[s_depth - 1];
    snprintf(p->file, sizeof(p->file), "%s.xgc", name);
    for (i = 0; p->file[i]; i++) {
        p->file[i] = p->file[i] == '\\' ? '/' : (char)tolower((u8)p->file[i]);
    }
    for (c = name; *c; c++) {
        if (*c == '/' || *c == '\\') {
            stem = c + 1;
        }
    }
    snprintf(p->archive, sizeof(p->archive), "%s", stem);
    p->type = type;
    p->id = id;
    p->pairs = NULL;
    p->protoTex = p->protoSfx = 0;
    for (i = 0; i < g_portMixNumPairPacks; i++) {
        const PortMixPairPack *pp = &g_portMixPairPacks[i];

        if (pp->type == type && pp->id == id && strcasecmp(pp->archive, stem) == 0) {
            p->pairs = pp;
            snprintf(p->archive, sizeof(p->archive), "%s", pp->archive);   /* as ids spell it */
            break;
        }
    }
    s_stats.packs++;
}

void PortMix_EndPack(void) {
    if (Mixing() && s_depth > 0) {
        MixPack *p = &s_stack[s_depth - 1];

        if (s_depth <= 8 && (p->protoTex || p->protoSfx)) {
            Port_Log("prototype: %s %s_%u: %u textures and %u sounds are the prototype's", p->archive,
                     s_typeNames[p->type < 11 ? p->type : 0], (unsigned)p->id, (unsigned)p->protoTex,
                     (unsigned)p->protoSfx);
        }
        s_depth--;
    }
}

/* ---- another release's copy of the pack -------------------------------------- */

static u32 Rd32(const u8 *p, int be) {
    return be ? (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]
              : (u32)p[3] << 24 | (u32)p[2] << 16 | (u32)p[1] << 8 | p[0];
}

static u32 Rd16LE(const u8 *p) {
    return (u32)p[1] << 8 | p[0];
}

typedef struct RelPack {
    int rel, ok;
    u32 hdr[HDR_WORDS];
    char refName[2][0xC];       /* the packs it refers to (DataPackRef) */
    u32 refId[2];
    u64 soundOff, blobOff;      /* in the file: the sound data's pointer table; the blob */
    u8 *blob;                   /* dataSize bytes, read when asked for */
    u8 *sound;                  /* the pointer table and sound data */
    u32 soundBase;              /* offset of the sound data in `sound` */
} RelPack;

/* Find the pack in release `rel`'s copy of the archive and read its header. */
static void RelPack_Open(RelPack *rp, const MixPack *mp, int rel) {
    int be = rel == PORT_DATA_GC, proto = rel == PORT_MIX_PROTO;
    u8 head[0x10], raw[HDR_SIZE], *toc;
    u32 count, i, ver;
    u64 pos = 0;

    memset(rp, 0, sizeof(*rp));
    rp->rel = rel;
    if (Port_MixRoot(rel) == NULL || Port_MixRead(rel, mp->file, 0, head, sizeof(head)) != sizeof(head) ||
        Rd32(head, be) != 0x020100A0) {
        return;
    }
    ver = Rd32(head + 8, be);
    if (proto ? ver != 177 && ver != 179 : ver != 0xB7) {
        return;
    }
    count = Rd32(head + 12, be);
    if (count == 0 || count > 0x10000 || (toc = malloc(count * TOC_ENTRY)) == NULL) {
        return;
    }
    if (Port_MixRead(rel, mp->file, 0x10, toc, count * TOC_ENTRY) == count * TOC_ENTRY) {
        for (i = 0; i < count; i++) {
            const u8 *e = toc + i * TOC_ENTRY;

            if (Rd32(e + 4, be) == mp->type && Rd32(e + 8, be) == mp->id && Rd32(e, be) == ver) {
                /* the prototype's offsets count from the end of their own entry */
                pos = (u64)Rd32(e + 0x14, be) + 0x10 + (u64)(proto ? i + 1 : count) * TOC_ENTRY;
                break;
            }
        }
    }
    free(toc);
    if (pos == 0 || Port_MixRead(rel, mp->file, pos, raw, HDR_SIZE) != HDR_SIZE) {
        return;
    }
    for (i = 0; i < HDR_WORDS; i++) {
        rp->hdr[i] = Rd32(raw + i * 4, be);
    }
    for (i = 0; i < 2; i++) {
        const u8 *ref = raw + HDR_WORDS * 4 + i * sizeof(DataPackRef);

        memcpy(rp->refName[i], ref, 0xC);
        rp->refName[i][0xB] = 0;
        rp->refId[i] = Rd32(ref + 0xC, be);
    }
    pos += HDR_SIZE;
    if (proto) {
        /* unpadded: the sound pointers and data, the relocation tables, the
         * pointer tables, then the blob (DataGamePackLoad.c) */
        if (rp->hdr[H_SOUNDSIZE] != 0) {
            rp->soundOff = pos;
            rp->soundBase = rp->hdr[H_NUMSOUNDPTRS] * 4;
            pos += (u64)rp->soundBase + rp->hdr[H_SOUNDSIZE];
        }
        rp->blobOff = pos + ((u64)rp->hdr[H_NUMRELOCS] + rp->hdr[H_NUMGX] + rp->hdr[H_NUMSOUNDRELOCS]) * 8 +
                      (u64)rp->hdr[H_NUMTEXRELOCS] * 8 + (u64)rp->hdr[H_NUMPTRS] * 4;
        rp->ok = rp->hdr[H_DATASIZE] < 0x10000000 && rp->hdr[H_SOUNDSIZE] < 0x10000000 &&
                 rp->hdr[H_NUMSOUNDPTRS] < 0x1000000 && rp->hdr[H_NUMTEXRELOCS] < 0x1000000 &&
                 rp->hdr[H_NUMPTRS] < 0x1000000;
        return;
    }
    if (rel == PORT_DATA_PS2) {
        pos = (pos + PORT_PS2_SECTOR - 1) & ~(u64)(PORT_PS2_SECTOR - 1);
    }
    if (rp->hdr[H_SOUNDSIZE] != 0) {
        rp->soundOff = pos;
        rp->soundBase = (rp->hdr[H_NUMSOUNDPTRS] * 4 + 0x1F) & ~0x1Fu;
        pos += (u64)rp->soundBase + rp->hdr[H_SOUNDSIZE];
        if (rel == PORT_DATA_PS2) {
            pos = (pos + PORT_PS2_SECTOR - 1) & ~(u64)(PORT_PS2_SECTOR - 1);
        }
    }
    rp->blobOff = pos + ((((u64)rp->hdr[H_NUMRELOCS] + rp->hdr[H_NUMGX] + rp->hdr[H_NUMSOUNDRELOCS]) * 8 + 0x1F) &
                         ~(u64)0x1F);
    rp->ok = rp->hdr[H_DATASIZE] < 0x10000000 && rp->hdr[H_SOUNDSIZE] < 0x10000000 &&
             rp->hdr[H_NUMSOUNDPTRS] < 0x1000000;
}

static const u8 *RelPack_Blob(RelPack *rp, const MixPack *mp) {
    if (rp->ok && rp->blob == NULL && rp->hdr[H_DATASIZE] != 0) {
        rp->blob = malloc(rp->hdr[H_DATASIZE]);
        if (rp->blob != NULL &&
            Port_MixRead(rp->rel, mp->file, rp->blobOff, rp->blob, rp->hdr[H_DATASIZE]) != rp->hdr[H_DATASIZE]) {
            free(rp->blob);
            rp->blob = NULL;
        }
        if (rp->blob == NULL) {
            rp->ok = 0;
        }
    }
    return rp->blob;
}

static const u8 *RelPack_Sound(RelPack *rp, const MixPack *mp) {
    u32 size = rp->soundBase + rp->hdr[H_SOUNDSIZE];

    if (rp->ok && rp->sound == NULL && rp->hdr[H_SOUNDSIZE] != 0) {
        rp->sound = malloc(size);
        if (rp->sound != NULL && Port_MixRead(rp->rel, mp->file, rp->soundOff, rp->sound, size) != size) {
            free(rp->sound);
            rp->sound = NULL;
        }
    }
    return rp->sound;
}

static void RelPack_Close(RelPack *rp) {
    free(rp->blob);
    free(rp->sound);
    memset(rp, 0, sizeof(*rp));
}

/* ---- ids and pairs -------------------------------------------------------------- */

/* The pair record holding index `idx` of release `rel`, among the pack's
 * textures (`sound` 0) or sounds; NULL: it pairs with nothing. */
static const u16 *FindPair(const MixPack *mp, int sound, int rel, u32 idx) {
    const PortMixPairPack *pp = mp->pairs;
    u32 first, n, i;

    if (pp == NULL) {
        return NULL;
    }
    first = pp->first + (sound ? pp->numTextures : 0);
    n = sound ? pp->numSounds : pp->numTextures;
    for (i = 0; i < n; i++) {
        if (g_portMixPairs[first + i][rel] == idx) {
            return g_portMixPairs[first + i];
        }
    }
    return NULL;
}

/* base://<archive>/<type>_<id>/<kind>/<index>: the index in the first
 * release that has the asset (the GameCube's bare, pairing.md §1). */
static void AssetId(char *id, u32 size, const MixPack *mp, const char *kind, const u16 *pair, int rel, u32 idx) {
    int r = rel;
    u32 i = idx;

    if (pair != NULL) {
        for (r = 0; r < PORT_MIX_RELS - 1 && pair[r] == 0xFFFF; r++) {
        }
        i = pair[r];
    }
    if (r == PORT_DATA_GC) {
        snprintf(id, size, "base://%s/%s_%u/%s/%u", mp->archive, s_typeNames[mp->type], (unsigned)mp->id, kind,
                 (unsigned)i);
    } else {
        snprintf(id, size, "base://%s/%s_%u/%s/%s:%u", mp->archive, s_typeNames[mp->type], (unsigned)mp->id, kind,
                 s_relNames[r], (unsigned)i);
    }
}

static u64 Fnv64(const u8 *p, u32 n) {
    u64 h = 0xCBF29CE484222325ull;

    while (n--) {
        h = (h ^ *p++) * 0x100000001B3ull;
    }
    return h;
}

/* ---- textures ------------------------------------------------------------------- */

/* One release's copy of a texture, in its file's form. */
typedef struct RelTex {
    int ok;
    u32 fmt, w, h, levels;      /* fmt as the release's header has it */
    const u8 *pix, *clut;
    u32 size;                   /* bytes of all levels at pix */
    int gx;                     /* the format gx_tex.c decodes it as; 0: the PS2's */
    u8 *rgba;                   /* level 0, decoded (malloc) */
    int alpha;                  /* some texel isn't opaque */
} RelTex;

static u32 CSM1(u32 i) {
    return (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1);
}

/* Bytes of level (w, h) as stored. */
static u32 LevelBytes(int rel, const RelTex *t, u32 w, u32 h) {
    if (IsPS2(rel)) {
        return w * h * (t->fmt == 2 ? 1 : t->fmt == 0 ? 3 : 4);    /* PSMT8, the prototype's RGB24, PSMCT32 */
    }
    return GX_PortLevelSize(t->gx, (int)w, (int)h);
}

/* The blob offset of texture idx's header (the GameCube pads its 0x10-byte
 * headers to 0x20). */
static u32 TexHeader(int rel, u32 off, u32 idx) {
    u32 i;

    for (i = 0; i < idx; i++) {
        off += 0x10;
        if (rel == PORT_DATA_GC && (off & 0x1F)) {
            off = (off + 0x1F) & ~0x1Fu;
        }
    }
    return off;
}

/* A copy from its header's values (t->fmt, w, h, levels set) and its pixels at
 * blob offset `data`; h: the header as the file has it (the PS2's palette). */
static void RelTex_Fill(RelTex *t, int rel, const u32 *hdr, const u8 *blob, u32 size, const u8 *h, u32 data) {
    u32 w, ht, l;
    u64 total = 0;

    if (t->w == 0 || t->h == 0 || t->w > MAX_TEX || t->h > MAX_TEX || t->levels > 11 || data >= size) {
        return;
    }
    switch (rel) {
    case PORT_DATA_GC:
        t->gx = t->fmt == 4 ? GXF_CMPR : t->fmt == 1 ? GXF_RGBA8 : 0;
        if (t->fmt == 1) {
            t->levels = 1;      /* as Render_CreateTextureSlot draws it */
        }
        break;
    case PORT_DATA_PC:
        t->gx = t->fmt == PORT_TEX_PC_RGB8 || t->fmt == PORT_TEX_PC_RGBA8 || t->fmt == PORT_TEX_PC_DXT1 ||
                t->fmt == PORT_TEX_PC_DXT5 ? Port_PCTextureGXFormat(t->fmt) : 0;
        break;
    default:
        if (t->fmt == 2) {
            u32 clut = Rd32(h + 0xC, 0);

            if (clut >= hdr[H_PALETTES] || (u64)hdr[H_PALOFF] + ((u64)clut + 1) * 0x400 > size) {
                return;
            }
            t->clut = blob + hdr[H_PALOFF] + clut * 0x400;
        } else if (t->fmt != 1 && !(rel == PORT_MIX_PROTO && t->fmt == 0)) {
            return;
        }
        break;
    }
    if (!IsPS2(rel) && t->gx == 0) {
        return;
    }
    for (l = 0, w = t->w, ht = t->h; l < t->levels; l++) {
        total += LevelBytes(rel, t, w ? w : 1, ht ? ht : 1);
        w >>= 1;
        ht >>= 1;
    }
    if (total > size - data) {
        return;
    }
    t->pix = blob + data;
    t->size = (u32)total;
    t->ok = 1;
}

/* A u16 as LoadBTF_RAM reads the header's (byte-reversed, as PC_Textures leaves them). */
static u16 Rev16(u32 v) {
    return (u16)((v & 0xFF) << 8 | (v >> 8 & 0xFF));
}

/* Texture `idx` of the release's pack, from its file's blob. */
static void RelTex_Read(RelTex *t, RelPack *rp, const MixPack *mp, u32 idx) {
    const u8 *blob = RelPack_Blob(rp, mp), *h;
    u32 size = rp->hdr[H_DATASIZE], off = TexHeader(rp->rel, rp->hdr[H_TEXOFF], idx);

    memset(t, 0, sizeof(*t));
    if (blob == NULL || idx >= rp->hdr[H_NUMTEX] || (u64)off + 0x10 > size) {
        return;
    }
    h = blob + off;
    t->fmt = Rd16LE(h);
    t->w = Rd16LE(h + 2);
    t->h = Rd16LE(h + 4);
    t->levels = Rd16LE(h + 6) ? Rd16LE(h + 6) : 1;
    if (rp->rel == PORT_MIX_PROTO) {
        t->levels = t->levels <= 2 ? 1 : t->levels - 1;     /* its level value m: m - 1 stored (Port_PS2.c) */
    }
    RelTex_Fill(t, rp->rel, rp->hdr, blob, size, h, Rd32(h + 8, rp->rel == PORT_DATA_GC));
}

/* Texture `idx` of the pack being loaded (GameCube or PC), from its blob in
 * memory: the headers as the loader left them (u16s byte-reversed, the PC's
 * format | PORT_TEX_PC, the pixel pointer fixed up), the pixels as the file
 * has them. */
static void RelTex_Base(RelTex *t, int rel, const u8 *base, u32 size, u32 texOff, u32 idx) {
    u32 off = TexHeader(rel, texOff, idx);
    const u16 *h;
    const u8 *pix;

    memset(t, 0, sizeof(*t));
    if ((u64)off + 0x10 > size) {
        return;
    }
    h = (const u16 *)(base + off);
    t->fmt = Rev16(h[0]) & ~(u32)(rel == PORT_DATA_PC ? PORT_TEX_PC : 0);
    t->w = Rev16(h[1]);
    t->h = Rev16(h[2]);
    t->levels = Rev16(h[3]) ? Rev16(h[3]) : 1;
    pix = SLOT_LOAD(const u8 *, h + 4);
    if (pix < base || pix >= base + size) {
        return;
    }
    RelTex_Fill(t, rel, NULL, base, size, NULL, (u32)(pix - base));
}

/* Level `l` (w x h, at src) of a copy as RGBA8. */
static void DecodeLevel(int rel, const RelTex *t, const u8 *src, u32 w, u32 h, u8 *out) {
    u32 n = w * h, k;

    if (!IsPS2(rel)) {
        GX_PortDecodeLevel(t->gx, src, (int)w, (int)h, out);
    } else if (t->fmt == 2) {
        for (k = 0; k < n; k++) {
            memcpy(out + k * 4, t->clut + CSM1(src[k]) * 4, 4);
        }
    } else if (t->fmt == 0) {
        for (k = 0; k < n; k++) {
            out[k * 4] = src[k * 3];
            out[k * 4 + 1] = src[k * 3 + 1];
            out[k * 4 + 2] = src[k * 3 + 2];
            out[k * 4 + 3] = 0xFF;
        }
    } else {
        memcpy(out, src, n * 4);
    }
}

static u8 s_sqrt[256];      /* a GameCube colour in the PC's space: sqrt(255 v) */

static void InitSqrt(void) {
    u32 v, r;

    if (s_sqrt[255] == 0) {
        for (v = 0; v < 256; v++) {
            for (r = 0; (r + 1) * (r + 1) <= 255 * v; r++) {
            }
            s_sqrt[v] = (u8)((255 * v - r * r) > ((r + 1) * (r + 1) - 255 * v) ? r + 1 : r);
        }
    }
}

static void ToPCSpace(u8 *rgba, u32 n) {
    u32 k;

    InitSqrt();
    for (k = 0; k < n; k++, rgba += 4) {
        rgba[0] = s_sqrt[rgba[0]];
        rgba[1] = s_sqrt[rgba[1]];
        rgba[2] = s_sqrt[rgba[2]];
    }
}

/* Level 0 decoded, in the PC's colours whatever the release. */
static void RelTex_Decode(RelTex *t, int rel) {
    u32 n = t->w * t->h, k;

    if (!t->ok || t->rgba != NULL) {
        return;
    }
    t->rgba = malloc((size_t)n * 4);
    if (t->rgba == NULL) {
        t->ok = 0;
        return;
    }
    DecodeLevel(rel, t, t->pix, t->w, t->h, t->rgba);
    if (rel == PORT_DATA_GC) {
        ToPCSpace(t->rgba, n);
    }
    for (k = 0; k < n; k++) {
        if (t->rgba[k * 4 + 3] != 255) {
            t->alpha = 1;
            break;
        }
    }
}

static int CmpU32(const void *a, const void *b) {
    u32 x = *(const u32 *)a, y = *(const u32 *)b;

    return x < y ? -1 : x > y;
}

/* Whether the copy has more than `limit` distinct texels. */
static int MoreColours(const RelTex *t, u32 limit) {
    u32 n = t->w * t->h, k, distinct = 1;
    u32 *v;

    if (n <= limit || (v = malloc((size_t)n * 4)) == NULL) {
        return 0;
    }
    memcpy(v, t->rgba, (size_t)n * 4);
    qsort(v, n, 4, CmpU32);
    for (k = 1; k < n && distinct <= limit; k++) {
        distinct += v[k] != v[k - 1];
    }
    free(v);
    return distinct > limit;
}

/* A 16x16 box thumbnail of premultiplied RGB. */
static void Thumb(const RelTex *t, f32 out[16 * 16 * 3]) {
    u32 cx, cy, x, y, c;

    for (cy = 0; cy < 16; cy++) {
        for (cx = 0; cx < 16; cx++) {
            u32 x0 = cx * t->w / 16, x1 = (cx + 1) * t->w / 16, y0 = cy * t->h / 16, y1 = (cy + 1) * t->h / 16;
            f32 sum[3] = { 0, 0, 0 };

            x1 = x1 > x0 ? x1 : x0 + 1;
            y1 = y1 > y0 ? y1 : y0 + 1;
            for (y = y0; y < y1; y++) {
                for (x = x0; x < x1; x++) {
                    const u8 *p = t->rgba + (y * t->w + x) * 4;

                    for (c = 0; c < 3; c++) {
                        sum[c] += (f32)p[c] * (f32)p[3] / 255.0f;
                    }
                }
            }
            for (c = 0; c < 3; c++) {
                out[(cy * 16 + cx) * 3 + c] = sum[c] / (f32)((x1 - x0) * (y1 - y0));
            }
        }
    }
}

static f32 ThumbDistance(const RelTex *a, const RelTex *b) {
    static f32 ta[16 * 16 * 3], tb[16 * 16 * 3];
    f32 d = 0;
    u32 i;

    Thumb(a, ta);
    Thumb(b, tb);
    for (i = 0; i < 16 * 16 * 3; i++) {
        d += ta[i] > tb[i] ? ta[i] - tb[i] : tb[i] - ta[i];
    }
    return d / (16 * 16 * 3);
}

/* Two font pages, the larger box-filtered down to the smaller's size: the mean
 * absolute difference of premultiplied RGB and alpha (0-255). Pages of the same
 * glyphs at two resolutions stay low; another layout or other glyphs don't. */
static f32 GlyphDistance(const RelTex *a, const RelTex *b) {
    const RelTex *big = a->w * a->h >= b->w * b->h ? a : b, *small = big == a ? b : a;
    u32 x, y, c, sx, sy;
    f64 d = 0;

    if (small->w == 0 || small->h == 0 || big->w % small->w || big->h % small->h) {
        return 255.0f;
    }
    sx = big->w / small->w;
    sy = big->h / small->h;
    for (y = 0; y < small->h; y++) {
        for (x = 0; x < small->w; x++) {
            const u8 *ps = small->rgba + (y * small->w + x) * 4;
            f32 sum[4] = { 0, 0, 0, 0 }, n = (f32)(sx * sy);
            u32 i, j;

            for (j = 0; j < sy; j++) {
                for (i = 0; i < sx; i++) {
                    const u8 *pb = big->rgba + ((y * sy + j) * big->w + x * sx + i) * 4;

                    for (c = 0; c < 3; c++) {
                        sum[c] += (f32)pb[c] * (f32)pb[3] / 255.0f;
                    }
                    sum[3] += pb[3];
                }
            }
            for (c = 0; c < 4; c++) {
                f32 v = c < 3 ? (f32)ps[c] * (f32)ps[3] / 255.0f : (f32)ps[3];

                d += fabsf(sum[c] / n - v);
            }
        }
    }
    return (f32)(d / ((f64)small->w * small->h * 4));
}

/* Interface art the pairing table leaves single because the releases drew it
 * differently (the GameCube's flame logo, the PS2's chrome one): the texture
 * of release `rp` that pairs with no texture of the base, of the same size
 * and the same shape (its alpha within SHAPE_LIMIT of `own`'s, clearly closer
 * than any other, or else the one at the base's own index `idx` within the
 * limit: the PS2 has two logos, chrome and blue, where the GameCube has its
 * flame one). Its index, read into *out, or ~0. Platform glyphs (pad
 * buttons) have other shapes and stay the base's. */
#define SHAPE_LIMIT 24.0f

static u32 ShapePair(RelPack *rp, const MixPack *mp, int fmt, u32 idx, const RelTex *own, const u8 *taken,
                     RelTex *out) {
    u32 n = own->w * own->h, holes = 0, j, k, best = ~0u;
    f32 d1 = 1e9f, d2 = 1e9f, dSame = 1e9f;

    for (k = 0; own->rgba != NULL && k < n; k++) {
        holes += own->rgba[k * 4 + 3] < 128;
    }
    /* an opaque texture (or nearly) has no shape to go by: the set's single texture in the same
     * place, of the same size (the target arrow's flat red and yellow, the PS2's blues) */
    if (!rp->ok) {
        return ~0u;
    }
    if (holes < n / 50 || holes > n - n / 50) {
        const u16 *pair = FindPair(mp, 0, rp->rel, idx);

        if ((pair != NULL && pair[fmt] != 0xFFFF) || idx >= MAX_TEX || taken[idx]) {
            return ~0u;
        }
        RelTex_Read(out, rp, mp, idx);
        if (!out->ok || out->w != own->w || out->h != own->h) {
            memset(out, 0, sizeof(*out));
            return ~0u;
        }
        RelTex_Decode(out, rp->rel);
        return out->ok ? idx : ~0u;
    }
    for (j = 0; j < rp->hdr[H_NUMTEX] && j < MAX_TEX; j++) {
        const u16 *pair = FindPair(mp, 0, rp->rel, j);
        RelTex c;
        f32 d = 0;

        if ((pair != NULL && pair[fmt] != 0xFFFF) || taken[j]) {
            continue;
        }
        RelTex_Read(&c, rp, mp, j);
        if (c.ok && c.w == own->w && c.h == own->h) {
            RelTex_Decode(&c, rp->rel);
            for (k = 0; c.ok && k < n; k++) {
                d += (f32)abs((int)c.rgba[k * 4 + 3] - (int)own->rgba[k * 4 + 3]);
            }
            d = c.ok ? d / (f32)n : 1e9f;
            if (j == idx) {
                dSame = d;
            }
            if (d < d1) {
                d2 = d1;
                d1 = d;
                best = j;
            } else if (d < d2) {
                d2 = d;
            }
        }
        free(c.rgba);
    }
    if (best != ~0u && d2 < d1 * 2 + 4) {
        best = dSame < SHAPE_LIMIT ? idx : ~0u;     /* two alike: the one in the same place */
    }
    if (best == ~0u || d1 >= SHAPE_LIMIT) {
        return ~0u;
    }
    RelTex_Read(out, rp, mp, best);
    RelTex_Decode(out, rp->rel);
    return out->ok ? best : ~0u;
}

/* ultimate.md §1.6 (tex_survey.rule): the release with the best copy. */
static int BestTexture(RelTex t[PORT_DATA_COUNT], int lookGC) {
    RelTex *gc = &t[PORT_DATA_GC], *pc = &t[PORT_DATA_PC], *ps2 = &t[PORT_DATA_PS2];
    int r, first = -1, same = 1, alpha = 0;

    for (r = 0; r < PORT_DATA_COUNT; r++) {
        if (t[r].ok) {
            if (first < 0) {
                first = r;
            }
            same = same && t[r].w == t[first].w && t[r].h == t[first].h;
            alpha = alpha || t[r].alpha;
        }
    }
    if (!same) {
        for (r = 0; r < PORT_DATA_COUNT; r++) {
            if (t[r].ok && t[r].w * t[r].h > t[first].w * t[first].h) {
                first = r;
            }
        }
        return first;
    }
    if (ps2->ok && ps2->fmt == 1) {
        return PORT_DATA_PS2;       /* PSMCT32: lossless */
    }
    if (gc->ok && gc->fmt == 1) {   /* RGBA8: exact in its own colours */
        return lookGC || !ps2->ok ? PORT_DATA_GC : PORT_DATA_PS2;
    }
    if (alpha || (pc->ok && pc->fmt == PORT_TEX_PC_DXT5)) {
        if (ps2->ok && !(pc->ok && MoreColours(pc, 1024))) {
            return PORT_DATA_PS2;
        }
        return pc->ok ? PORT_DATA_PC : first;
    }
    if (lookGC && gc->ok) {
        return PORT_DATA_GC;
    }
    return pc->ok ? PORT_DATA_PC : gc->ok ? PORT_DATA_GC : PORT_DATA_PS2;
}

/* What a texture becomes: its header's values and its pixels. */
typedef struct NewTex {
    u8 *pix;                /* malloc; NULL: the base's stays */
    u32 size, fmt, w, h, levels;
} NewTex;

/* The copy as the port stores it; `toPC`: a GameCube copy as RGBA8 in the PC's colours. */
static void MakeTex(NewTex *nt, int rel, const RelTex *t, int toPC) {
    u32 l, w, h;
    u64 total = 0;
    u8 *out;
    const u8 *src = t->pix;

    nt->w = t->w;
    nt->h = t->h;
    nt->levels = t->levels > 1 ? t->levels : 0;
    if ((rel == PORT_DATA_GC && !toPC) || rel == PORT_DATA_PC || (IsPS2(rel) && t->fmt == 1)) {
        nt->fmt = rel == PORT_DATA_GC ? t->fmt : rel == PORT_DATA_PC ? (PORT_TEX_PC | t->fmt)
                                                                   : (PORT_TEX_PC | PORT_TEX_PC_RGBA8);
        nt->size = t->size;
        nt->pix = malloc(t->size);
        if (nt->pix != NULL) {
            memcpy(nt->pix, t->pix, t->size);
        }
        return;
    }
    for (l = 0, w = t->w, h = t->h; l < t->levels; l++, w >>= 1, h >>= 1) {
        total += (u64)(w ? w : 1) * (h ? h : 1) * 4;
    }
    nt->fmt = PORT_TEX_PC | PORT_TEX_PC_RGBA8;
    nt->size = (u32)total;
    nt->pix = out = malloc(total);
    if (out == NULL) {
        return;
    }
    for (l = 0, w = t->w, h = t->h; l < t->levels; l++, w >>= 1, h >>= 1) {
        u32 lw = w ? w : 1, lh = h ? h : 1;

        DecodeLevel(rel, t, src, lw, lh, out);
        if (rel == PORT_DATA_GC) {
            ToPCSpace(out, lw * lh);
        }
        src += LevelBytes(rel, t, lw, lh);
        out += lw * lh * 4;
    }
}

/* ---- interface colours ------------------------------------------------------------ */

/* The silver set (Port_MixInterface PS2) is the PS2's textures *and* the
 * PS2's interface colours: the base's interface data keeps its layout,
 * events and scripts, but the colours its objects are drawn in (colour key
 * tracks of the default and state animations, polygon points, briefing text
 * colours) are the PS2's. Interfaces pair by order among those of the same
 * shape (object count and types: a longest common subsequence of the two
 * packs' headers); a track is taken only when its key count matches. */
typedef struct IfaceSide {
    PortIfaceScan scan;
    u8 *base;
    u32 *hdrs, numHdrs;
    u32 *slotTex;       /* per blob word: 1 + the pack's own texture its relocation names, 0: none */
} IfaceSide;

/* The texture each base texture's paired PS2 object shows (an image's, a texture key's): the
 * PS2's own choice where it has two copies of a design (its chrome and blue logos), and its
 * copy of art the pairing table leaves single. 0xFFFF: none. Filled by MixIfaceColours. */
static _Thread_local u16 *s_objTex;
static _Thread_local u32 s_numObjTex;

static void SlotTex(IfaceSide *sd, const u32 *texRelocs, u32 n) {
    u32 words = sd->scan.size / 4, i;

    sd->slotTex = calloc(words + 1, 4);
    for (i = 0; sd->slotTex != NULL && i < n; i++) {
        u32 slot = texRelocs[i * 2] & ~PORT_TEXRELOC_U16, id = texRelocs[i * 2 + 1];

        if ((id >> 16) == 0 && slot / 4 <= words) {
            sd->slotTex[slot / 4] = (id & 0xFFFF) + 1;
        }
    }
}

static void MapTex(IfaceSide *b, u32 sb, IfaceSide *p, u32 sp) {
    u32 tb, tp;

    if (b->slotTex == NULL || p->slotTex == NULL || !InBlobOf(&b->scan, sb, 4) || !InBlobOf(&p->scan, sp, 4)) {
        return;
    }
    tb = b->slotTex[sb / 4];
    tp = p->slotTex[sp / 4];
    /* one base texture shown by two objects the PS2 gives two textures (its title's logo: the
     * blue one fading in, then the chrome one): the one not in the base's own place wins, the
     * PS2's deliberate choice; the pack has no texture to spare for the other */
    if (tb != 0 && tp != 0 && tb - 1 < s_numObjTex &&
        (s_objTex[tb - 1] == 0xFFFF || (s_objTex[tb - 1] == tb - 1 && tp != tb))) {
        s_objTex[tb - 1] = (u16)(tp - 1);
    }
}

/* Texture key animations (IfaceTexAnim: count, keys -> texture slots) of the same length. */
static void MapTexAnim(IfaceSide *b, u32 ab, IfaceSide *p, u32 ap) {
    u32 nb, np, kb, kp, i;

    if (ab == ~0u || ap == ~0u || !InBlobOf(&b->scan, ab, 8) || !InBlobOf(&p->scan, ap, 8)) {
        return;
    }
    memcpy(&nb, b->base + ab, 4);
    memcpy(&np, p->base + ap, 4);
    kb = Port_IfaceTo(&b->scan, ab + 4);
    kp = Port_IfaceTo(&p->scan, ap + 4);
    for (i = 0; nb == np && kb != ~0u && kp != ~0u && i < nb && i < 0x400; i++) {
        MapTex(b, kb + i * 4, p, kp + i * 4);
    }
}

static u32 HdrSig(const PortIfaceScan *s, u32 h) {
    const InterfaceHeader *hdr = (const InterfaceHeader *)(s->base + h);
    u32 objs = Port_IfaceTo(s, h + 0x10), sig = 2166136261u, i;

    for (i = 0; i <= hdr->numObjects; i++) {
        u32 v = i < hdr->numObjects ? s->base[objs + i * 0x6C + 1] : 0x100 + hdr->numObjects;

        sig = (sig ^ v) * 16777619u;
    }
    return sig;
}

static int IfaceSide_Init(IfaceSide *sd, u8 *base, u32 size, u32 bias, const u32 *pointers, u32 numPointers,
                          const u32 *refs, u32 numRefs) {
    u8 *flags;
    u32 count, i, n = 0;

    memset(sd, 0, sizeof(*sd));
    sd->base = base;
    if (!Port_IfaceScanInit(&sd->scan, base, size, bias, pointers, numPointers, refs, numRefs)) {
        return 0;
    }
    flags = Port_IfaceHeaders(&sd->scan, &count);
    sd->hdrs = flags != NULL && count != 0 ? malloc(count * sizeof(u32)) : NULL;
    for (i = 0; sd->hdrs != NULL && i <= size / 4; i++) {
        if (flags[i] && n < count) {
            sd->hdrs[n++] = i * 4;
        }
    }
    sd->numHdrs = n;
    free(flags);
    return 1;
}

static void IfaceSide_Free(IfaceSide *sd) {
    Port_IfaceScanFree(&sd->scan);
    free(sd->hdrs);
    free(sd->slotTex);
}

static struct {
    u32 packs, headers, headersByObject, headersUnpaired, tracks, tracksByTime, blends, ps2Art, keys, tracksSkipped, points, dialog;
} s_colStats;

/* Colour track at `to` (base) from `from` (PS2): IfaceKeyTrack, keys 12 bytes, RGBA at 0. */
static void CopyColorTrack(IfaceSide *b, u32 to, IfaceSide *p, u32 from) {
    u32 nb, np, kb, kp, i;

    if (to == ~0u || from == ~0u || !InBlobOf(&b->scan, to, 8) || !InBlobOf(&p->scan, from, 8)) {
        return;
    }
    memcpy(&nb, b->base + to, 4);
    memcpy(&np, p->base + from, 4);
    if (nb == 0 && np == 0) {
        return;
    }
    kb = Port_IfaceTo(&b->scan, to + 4);
    kp = Port_IfaceTo(&p->scan, from + 4);
    if (nb == 0 || np == 0 || nb > 0x1000 || np > 0x1000 || kb == ~0u || kp == ~0u ||
        !InBlobOf(&b->scan, kb, nb * 12) || !InBlobOf(&p->scan, kp, np * 12)) {
        s_colStats.tracksSkipped++;
        return;
    }
    if (nb != np) {
        /* other keys: each of the base's takes the colour the PS2's track has at its start
         * (the key whose time span holds it; durations at 8, ms) */
        f32 tb = 0;

        for (i = 0; i < nb; i++) {
            f32 tp = 0, d;
            u32 k;

            for (k = 0; k + 1 < np; k++) {
                memcpy(&d, p->base + kp + k * 12 + 8, 4);
                if (!(d >= 0.0f && d < 1e7f) || tp + d > tb) {
                    break;
                }
                tp += d;
            }
            s_colStats.keys += memcmp(b->base + kb + i * 12, p->base + kp + k * 12, 4) != 0;
            memcpy(b->base + kb + i * 12, p->base + kp + k * 12, 4);
            memcpy(&d, b->base + kb + i * 12 + 8, 4);
            tb += d >= 0.0f && d < 1e7f ? d : 0.0f;
        }
        s_colStats.tracksByTime++;
        return;
    }
    for (i = 0; i < nb; i++) {
        s_colStats.keys += memcmp(b->base + kb + i * 12, p->base + kp + i * 12, 4) != 0;
        memcpy(b->base + kb + i * 12, p->base + kp + i * 12, 4);
    }
    s_colStats.tracks++;
}

static void CopyObjectColours(IfaceSide *b, u32 db, IfaceSide *p, u32 dp) {
    u32 ab = Port_IfaceTo(&b->scan, db + 0x60), ap = Port_IfaceTo(&p->scan, dp + 0x60);
    u32 nsb, nsp, sb, sp, i, ub, up;

    if (ab != ~0u && ap != ~0u) {
        CopyColorTrack(b, ab + 0x18, p, ap + 0x18);     /* IfaceObjAnim.color */
    }
    memcpy(&nsb, b->base + db + 0x64, 4);
    memcpy(&nsp, p->base + dp + 0x64, 4);
    sb = Port_IfaceTo(&b->scan, db + 0x68);
    sp = Port_IfaceTo(&p->scan, dp + 0x68);
    for (i = 0; sb != ~0u && sp != ~0u && i < nsb && i < 64 && InBlobOf(&b->scan, sb + i * 0xC, 0xC); i++) {
        /* the state in the same place, else the PS2's first of the same type */
        u32 k = i, tb, tp;

        if (nsb != nsp || b->base[sb + i * 0xC] != p->base[sp + i * 0xC]) {
            for (k = 0; k < nsp && k < 64 && p->base[sp + k * 0xC] != b->base[sb + i * 0xC]; k++) {
            }
        }
        if (k >= nsp || k >= 64 || !InBlobOf(&p->scan, sp + k * 0xC, 0xC)) {
            s_colStats.tracksSkipped++;
            continue;
        }
        tb = Port_IfaceTo(&b->scan, sb + i * 0xC + 8);
        tp = Port_IfaceTo(&p->scan, sp + k * 0xC + 8);
        if (tb != ~0u && tp != ~0u) {
            CopyColorTrack(b, tb + 0x10, p, tp + 0x10);     /* IfaceStateAnim.color */
        }
    }
    ub = Port_IfaceTo(&b->scan, db + 0x14);
    up = Port_IfaceTo(&p->scan, dp + 0x14);
    if (ub == ~0u || up == ~0u) {
        return;
    }
    switch (b->base[db + 1]) {
    case IFACE_DEF_IMAGE:       /* how it blends (the PS2's empty meters: 3 where the GameCube has 2) */
        if (InBlobOf(&b->scan, ub, sizeof(IfaceImageDef)) && InBlobOf(&p->scan, up, sizeof(IfaceImageDef)) &&
            b->base[ub] == p->base[up]) {
            if (b->base[ub + 1] != p->base[up + 1]) {
                b->base[ub + 1] = p->base[up + 1];
                s_colStats.blends++;
            }
            if (b->base[ub] == 0) {
                MapTex(b, ub + 4, p, up + 4);
            } else if (b->base[ub] == 3) {
                MapTexAnim(b, Port_IfaceTo(&b->scan, ub + 0x10), p, Port_IfaceTo(&p->scan, up + 0x10));
            }
        }
        break;
    case IFACE_DEF_TEXT:
    case IFACE_DEF_DEBUG_TEXT:
        if (InBlobOf(&b->scan, ub, sizeof(IfaceText)) && InBlobOf(&p->scan, up, sizeof(IfaceText)) &&
            b->base[ub + 0xE] != p->base[up + 0xE]) {
            b->base[ub + 0xE] = p->base[up + 0xE];
            s_colStats.blends++;
        }
        break;
    case IFACE_DEF_POLY: {
        u32 nb, np, pb = Port_IfaceTo(&b->scan, ub + 4), pp = Port_IfaceTo(&p->scan, up + 4);

        memcpy(&nb, b->base + ub, 4);
        memcpy(&np, p->base + up, 4);
        for (i = 0; nb == np && pb != ~0u && pp != ~0u && i < nb && i < 0x100 &&
                    InBlobOf(&b->scan, pb + i * 0x20, 0x20) && InBlobOf(&p->scan, pp + i * 0x20, 0x20); i++) {
            memcpy(b->base + pb + i * 0x20 + 0x10, p->base + pp + i * 0x20 + 0x10, 4);
            s_colStats.points++;
        }
        break;
    }
    case IFACE_DEF_DIALOG: {
        u32 nb, np, eb = Port_IfaceTo(&b->scan, ub + 4), ep = Port_IfaceTo(&p->scan, up + 4);

        memcpy(&nb, b->base + ub, 4);
        memcpy(&np, p->base + up, 4);
        for (i = 0; nb == np && eb != ~0u && ep != ~0u && i < nb && i < 0x400 &&
                    InBlobOf(&b->scan, eb + i * 0x20, 0x20) && InBlobOf(&p->scan, ep + i * 0x20, 0x20); i++) {
            u32 cb = Port_IfaceTo(&b->scan, eb + i * 0x20 + 0x14), cp = Port_IfaceTo(&p->scan, ep + i * 0x20 + 0x14);
            u32 xb = cb != ~0u ? Port_IfaceTo(&b->scan, cb + 0xC) : ~0u, xp = cp != ~0u ? Port_IfaceTo(&p->scan, cp + 0xC) : ~0u;

            if (xb != ~0u && xp != ~0u && InBlobOf(&b->scan, xb, 4) && InBlobOf(&p->scan, xp, 4)) {
                memcpy(b->base + xb, p->base + xp, 4);
                s_colStats.dialog++;
            }
        }
        break;
    }
    }
}

/* Two interfaces of nearly the same shape (the car HUD: the PS2 has one image
 * more, the GameCube one text more): objects aligned by a longest common
 * subsequence of (type, role, id); the pair is taken when all but 3 of the
 * smaller's objects align. Returns the objects matched, 0 if refused. */
static u32 PairObjects(IfaceSide *b, u32 hb, IfaceSide *p, u32 hp) {
    u32 nb = ((const InterfaceHeader *)(b->base + hb))->numObjects, np = ((const InterfaceHeader *)(p->base + hp))->numObjects;
    u32 ob = Port_IfaceTo(&b->scan, hb + 0x10), op = Port_IfaceTo(&p->scan, hp + 0x10), i, j, W = np + 1, n;
    u16 *t;
    u32 sb[200], sp[200];

    if (ob == ~0u || op == ~0u || nb > 200 || np > 200 || (t = calloc((size_t)(nb + 1) * W, 2)) == NULL) {
        return 0;
    }
    for (i = 0; i < nb; i++) {
        const u8 *d = b->base + ob + i * 0x6C;
        u16 id;

        memcpy(&id, d + 0xA, 2);
        sb[i] = d[1] | (u32)d[0] << 8 | (u32)id << 16;
    }
    for (j = 0; j < np; j++) {
        const u8 *d = p->base + op + j * 0x6C;
        u16 id;

        memcpy(&id, d + 0xA, 2);
        sp[j] = d[1] | (u32)d[0] << 8 | (u32)id << 16;
    }
    for (i = nb; i-- > 0;) {
        for (j = np; j-- > 0;) {
            t[i * W + j] = sb[i] == sp[j] ? t[(i + 1) * W + j + 1] + 1
                           : t[(i + 1) * W + j] > t[i * W + j + 1] ? t[(i + 1) * W + j] : t[i * W + j + 1];
        }
    }
    n = t[0];
    if (n + 3 < (nb < np ? nb : np)) {
        free(t);
        return 0;
    }
    for (i = 0, j = 0; i < nb && j < np;) {
        if (sb[i] == sp[j]) {
            CopyObjectColours(b, ob + i * 0x6C, p, op + j * 0x6C);
            i++;
            j++;
        } else if (t[(i + 1) * W + j] >= t[i * W + j + 1]) {
            i++;
        } else {
            j++;
        }
    }
    free(t);
    return n;
}

/* The PS2's own art in an interface the GameCube shares (HW_IPack's title: the "MAXIMUM
 * JUSTICE" subtitle, its flares and its TM): images shown only where the platform code
 * variable (0x258, GameSettings.c: GameCube 2, PS2 1, PC 0) is the PS2's, and what is
 * anchored to them. Their condition is made the base's platform value, so they show as on the
 * PS2. Text so conditioned stays as it is (the PS2's "PRESS START BUTTON" wording). */
static void ShowPS2Art(IfaceSide *b, u32 h, int fmt) {
    const InterfaceHeader *hdr = (const InterfaceHeader *)(b->base + h);
    u32 objs = Port_IfaceTo(&b->scan, h + 0x10), vl = Port_IfaceTo(&b->scan, h + 0x1C), vars = ~0u, nv = 0, i;
    s32 own = fmt == PORT_DATA_PS2 ? 1 : fmt == PORT_DATA_PC ? 0 : 2;
    u8 sel[200];
    u32 cond[200];

    if (objs == ~0u || vl == ~0u || hdr->numObjects > 200 || !InBlobOf(&b->scan, vl, 8)) {
        return;
    }
    memcpy(&nv, b->base + vl, 4);
    vars = Port_IfaceTo(&b->scan, vl + 4);
    memset(sel, 0, sizeof(sel));
    for (i = 0; vars != ~0u && i < hdr->numObjects; i++) {
        u32 d = objs + i * 0x6C, cl = Port_IfaceTo(&b->scan, d + 0x48), cn, c, data, v, code;
        s32 value, id;

        cond[i] = ~0u;
        if (cl == ~0u || !InBlobOf(&b->scan, cl, 12) || b->base[cl] != 1) {
            continue;   /* not "show if all" */
        }
        memcpy(&cn, b->base + cl + 4, 4);
        c = Port_IfaceTo(&b->scan, cl + 8);
        if (cn != 1 || c == ~0u || !InBlobOf(&b->scan, c, 0x1C) || b->base[c] != 1) {
            continue;   /* not one "variable == value" */
        }
        memcpy(&value, b->base + c + 4, 4);
        data = Port_IfaceTo(&b->scan, c + 8);
        if (value != 1 || data == ~0u || !InBlobOf(&b->scan, data, 4) || b->base[data] != 0) {
            continue;
        }
        {
            u16 v16;

            memcpy(&v16, b->base + data + 2, 2);   /* IfaceVarValue.value: the variable */
            v = v16;
        }
        if (v >= nv || !InBlobOf(&b->scan, vars + v * 0x34, 0x34) || b->base[vars + v * 0x34] != IFACE_VAR_CODE) {
            continue;
        }
        code = Port_IfaceTo(&b->scan, vars + v * 0x34 + 0x10);
        if (code == ~0u || !InBlobOf(&b->scan, code, 4)) {
            continue;
        }
        memcpy(&id, b->base + code, 4);
        if (id != 0x258) {
            continue;
        }
        cond[i] = c + 4;
        sel[i] = b->base[d + 1] == IFACE_DEF_IMAGE;
    }
    for (i = 0; i < hdr->numObjects; i++) {     /* what is anchored to one of them */
        s16 anc;

        memcpy(&anc, b->base + objs + i * 0x6C + 0xC, 2);
        if (!sel[i] && cond[i] != ~0u && anc != 0 && (s32)i + anc >= 0 && (u32)((s32)i + anc) < hdr->numObjects &&
            sel[i + anc]) {
            sel[i] = 1;
        }
    }
    for (i = 0; i < hdr->numObjects; i++) {
        if (sel[i]) {
            memcpy(b->base + cond[i], &own, 4);
            s_colStats.ps2Art++;
            Port_Log("mix: interface @%x object %u: the PS2's own art, shown", (unsigned)h, (unsigned)i);
        }
    }
}

static void MixIfaceColours(MixPack *mp, s32 fmt, u8 *base, u32 dataSize, const u32 *pointers, u32 numPointers,
                            const u32 *refs, u32 numRefs, const u32 *texRelocs, u32 numTexRelocs) {
    RelPack rp;
    IfaceSide b, p;
    u32 *tables = NULL, nt, np, i, j, *lcs = NULL, pairs = 0;
    const u8 *blob;

    RelPack_Open(&rp, mp, PORT_DATA_PS2);
    blob = RelPack_Blob(&rp, mp);
    if (blob == NULL) {
        RelPack_Close(&rp);
        return;
    }
    nt = rp.hdr[H_NUMTEXRELOCS];
    np = rp.hdr[H_NUMPTRS];
    if (nt < 0x1000000 && np < 0x1000000 && (tables = malloc(((size_t)nt * 2 + np + 1) * 4)) != NULL &&
        Port_MixRead(PORT_DATA_PS2, mp->file, rp.blobOff + rp.hdr[H_DATASIZE], tables, (nt * 2 + np) * 4) !=
            (nt * 2 + np) * 4) {
        free(tables);
        tables = NULL;
    }
    if (tables == NULL || !IfaceSide_Init(&b, base, dataSize, (u32)HND(base), pointers, numPointers, refs, numRefs)) {
        free(tables);
        RelPack_Close(&rp);
        return;
    }
    if (!IfaceSide_Init(&p, rp.blob, rp.hdr[H_DATASIZE], 0, tables + nt * 2, np, tables, nt)) {
        IfaceSide_Free(&b);
        free(tables);
        RelPack_Close(&rp);
        return;
    }
    if (s_objTex != NULL) {
        /* the PS2's texture relocations: the first table before its blob */
        u32 nr = rp.hdr[H_NUMRELOCS], gx = rp.hdr[H_NUMGX], sr = rp.hdr[H_NUMSOUNDRELOCS];
        u64 tsize = (((u64)nr + gx + sr) * 8 + 0x1F) & ~(u64)0x1F;
        u32 *pr = nr < 0x100000 && tsize <= rp.blobOff ? malloc((size_t)nr * 8 + 8) : NULL;

        SlotTex(&b, texRelocs, numTexRelocs);
        if (pr != NULL && Port_MixRead(PORT_DATA_PS2, mp->file, rp.blobOff - tsize, pr, nr * 8) == nr * 8) {
            SlotTex(&p, pr, nr);
        }
        free(pr);
    }
    if (b.numHdrs != 0 && p.numHdrs != 0 &&
        (lcs = calloc(((size_t)b.numHdrs + 1) * (p.numHdrs + 1), sizeof(u32))) != NULL) {
        u32 W = p.numHdrs + 1, *sb = malloc(b.numHdrs * 4), *sp = malloc(p.numHdrs * 4);

        for (i = 0; sb != NULL && i < b.numHdrs; i++) {
            sb[i] = HdrSig(&b.scan, b.hdrs[i]);
        }
        for (j = 0; sp != NULL && j < p.numHdrs; j++) {
            sp[j] = HdrSig(&p.scan, p.hdrs[j]);
        }
        for (i = b.numHdrs; sb != NULL && sp != NULL && i-- > 0;) {
            for (j = p.numHdrs; j-- > 0;) {
                lcs[i * W + j] = sb[i] == sp[j] ? lcs[(i + 1) * W + j + 1] + 1
                                 : lcs[(i + 1) * W + j] > lcs[i * W + j + 1] ? lcs[(i + 1) * W + j] : lcs[i * W + j + 1];
            }
        }
        for (i = 0, j = 0; sb != NULL && sp != NULL && (i < b.numHdrs || j < p.numHdrs);) {
            if (i == b.numHdrs || j == p.numHdrs || sb[i] != sp[j]) {
                /* a gap: the headers up to the next pair, matched object by object when as many on both sides */
                u32 i1 = i, j1 = j, k;

                while (i1 < b.numHdrs || j1 < p.numHdrs) {
                    if (i1 < b.numHdrs && j1 < p.numHdrs && sb[i1] == sp[j1] &&
                        lcs[i1 * W + j1] == lcs[(i1 + 1) * W + j1 + 1] + 1) {
                        break;
                    }
                    if (j1 == p.numHdrs || (i1 < b.numHdrs && lcs[(i1 + 1) * W + j1] >= lcs[i1 * W + j1 + 1])) {
                        i1++;
                    } else {
                        j1++;
                    }
                }
                for (k = 0; i1 - i == j1 - j && k < i1 - i; k++) {
                    if (PairObjects(&b, b.hdrs[i + k], &p, p.hdrs[j + k]) != 0) {
                        pairs++;
                        s_colStats.headersByObject++;
                    }
                }
                i = i1;
                j = j1;
                continue;
            }
            if (sb[i] == sp[j]) {
                const InterfaceHeader *hb = (const InterfaceHeader *)(b.base + b.hdrs[i]);
                u32 ob = Port_IfaceTo(&b.scan, b.hdrs[i] + 0x10), op = Port_IfaceTo(&p.scan, p.hdrs[j] + 0x10), k;

                for (k = 0; ob != ~0u && op != ~0u && k < hb->numObjects; k++) {
                    CopyObjectColours(&b, ob + k * 0x6C, &p, op + k * 0x6C);
                }
                ShowPS2Art(&b, b.hdrs[i], fmt);
                pairs++;
                i++;
                j++;
            } else if (lcs[(i + 1) * W + j] >= lcs[i * W + j + 1]) {
                i++;
            } else {
                j++;
            }
        }
        free(sb);
        free(sp);
    }
    s_colStats.packs += pairs != 0;
    s_colStats.headers += pairs;
    s_colStats.headersUnpaired += b.numHdrs - pairs;
    if (b.numHdrs != pairs) {
        Port_Log("mix: %s %s_%u: %u of %u interfaces paired with the PS2's (%u there)", mp->archive,
                 s_typeNames[mp->type < 11 ? mp->type : 0], (unsigned)mp->id, (unsigned)pairs, (unsigned)b.numHdrs,
                 (unsigned)p.numHdrs);
    }
    free(lcs);
    IfaceSide_Free(&b);
    IfaceSide_Free(&p);
    free(tables);
    RelPack_Close(&rp);
}

void PortMix_Textures(DataPack *pack, s32 fmt, u8 *base, u32 dataSize, u32 texOff, u32 numTex, const u32 *pointers,
                      u32 numPointers, const u32 *refs, u32 numRefs) {
    MixPack *mp = Cur();
    RelPack rp[PORT_MIX_RELS];
    NewTex *nt;

    free(s_objTex);     /* a load that returned early left its map */
    s_objTex = NULL;
    u8 *block, *out, *hp, *ui = NULL, *taken = NULL;
    int set = Port_MixInterface();
    u32 i, total = 0, changed = 0;
    /* mode "replace": the base's textures stay as they are, in its look */
    int r, lookGC = Port_MixProtoReplace() || Port_MixLookGC(), others = 0, paired;

    if (mp == NULL || numTex == 0 || numTex > 0x10000 || fmt < 0 || fmt >= PORT_DATA_COUNT) {
        return;
    }
    if (s_collect) {
        /* the builder's first pass: which other packs' textures this one's interfaces show */
        PortIfaceScan scan;
        OtherCtx ctx;
        u8 *used = calloc(numTex, 1);

        ctx.pack = pack;
        if (used != NULL && Port_IfaceScanInit(&scan, base, dataSize, (u32)HND(base), pointers, numPointers, refs,
                                               numRefs)) {
            Port_IfaceTexturesAll(&scan, (const u32 *)GETP(pack->relocs), pack->numRelocs, used, numTex, NoteUiRef,
                                  &ctx);
            Port_IfaceScanFree(&scan);
        }
        free(used);
        return;
    }
    if (Port_MixActive() && set == PORT_DATA_PS2 && fmt != PORT_DATA_PS2 && Port_MixRoot(PORT_DATA_PS2) != NULL) {
        s_numObjTex = numTex < 0x10000 ? numTex : 0;
        s_objTex = s_numObjTex != 0 ? malloc(numTex * sizeof(u16)) : NULL;
        if (s_objTex != NULL) {
            memset(s_objTex, 0xFF, numTex * sizeof(u16));
        }
        MixIfaceColours(mp, fmt, base, dataSize, pointers, numPointers, refs, numRefs, (const u32 *)GETP(pack->relocs),
                        pack->numRelocs);
    }
    for (r = 0; r < PORT_MIX_RELS; r++) {
        others += r != fmt && Port_MixRoot(r) != NULL;
    }
    /* one release, or nothing to do to it: the pack as it is */
    if (others == 0 && (fmt != PORT_DATA_GC || lookGC)) {
        return;
    }
    /* the other releases' copies only for a pack the pairing table has; the
     * base's own from memory (the PS2's from its file: it's converted by now) */
    paired = mp->pairs != NULL && mp->pairs->numTextures != 0;
    if (!paired && (fmt != PORT_DATA_GC || lookGC)) {
        s_stats.texKept += numTex;
        return;
    }
    nt = calloc(numTex, sizeof(NewTex));
    if (nt == NULL) {
        return;
    }
    /* the textures the pack's interfaces show: one release's set, fonts the PC's */
    if (Port_MixActive() && paired && (ui = calloc(numTex, 1)) != NULL && set != fmt) {
        taken = calloc(MAX_TEX, 1);     /* the set's textures given to a texture of the base by shape */
    }
    if (ui != NULL) {
        PortIfaceScan scan;

        int scanned = Port_IfaceScanInit(&scan, base, dataSize, (u32)HND(base), pointers, numPointers, refs, numRefs);

        if (scanned) {
            Port_IfaceTextures(&scan, (const u32 *)GETP(pack->relocs), pack->numRelocs, ui, numTex);
        }
        if (mp->type == 8) {    /* a share pack: what other packs' interfaces show of it */
            for (i = 0; i < s_numUiRefs; i++) {
                const UiRef *u = &s_uiRefs[i];

                if (u->id != mp->id || strcasecmp(u->archive, mp->archive) != 0) {
                    continue;
                }
                if (u->what == PORT_IFACE_GEOM && set == PORT_DATA_PS2) {
                    s_stats.uiShared += scanned ? Port_IfaceGeometryTextures(&scan, u->index * 4,
                                                                             (const u32 *)GETP(pack->relocs),
                                                                             pack->numRelocs, ui, numTex) : 0;
                } else if (u->what != PORT_IFACE_GEOM && u->index < numTex && ui[u->index] != PORT_IFACE_ART) {
                    s_stats.uiShared += ui[u->index] == 0;
                    ui[u->index] = u->what;
                }
            }
        }
        if (scanned) {
            Port_IfaceScanFree(&scan);
        }
        for (i = 0; i < numTex; i++) {
            if (ui[i] == PORT_IFACE_GEOMTEX) {
                ui[i] = set == PORT_DATA_PS2 ? PORT_IFACE_ART : 0;
            }
        }
    }
    for (r = 0; r < PORT_MIX_RELS; r++) {
        memset(&rp[r], 0, sizeof(rp[r]));
        if ((r == fmt && fmt == PORT_DATA_PS2) || (r != fmt && paired)) {
            RelPack_Open(&rp[r], mp, r);
        }
    }
    rp[fmt].ok |= fmt != PORT_DATA_PS2;
    for (i = 0; i < numTex && rp[fmt].ok; i++) {
        const u16 *pair = FindPair(mp, 0, fmt, i);
        RelTex t[PORT_MIX_RELS];
        int order[PORT_MIX_RELS], n, pick = fmt, k, kind;
        u32 setIdx = pair != NULL && set >= 0 ? pair[set] : 0xFFFF;
        char id[0xC0];

        AssetId(id, sizeof(id), mp, "texture", pair, fmt, i);
        n = Port_MixAssetRule(id, order);
        kind = ui != NULL ? ui[i] : 0;
        if (kind == 0 && n == PORT_MIX_FROM_IFACE) {
            kind = PORT_IFACE_ART;      /* the recipe names it interface art */
        }
        for (r = 0; r < PORT_MIX_RELS; r++) {
            u32 idx = r == fmt ? i : pair != NULL && (kind || n != PORT_MIX_FROM_BASE) ? pair[r] : 0xFFFF;

            memset(&t[r], 0, sizeof(t[r]));
            if (r == fmt && fmt != PORT_DATA_PS2) {
                RelTex_Base(&t[r], r, base, dataSize, texOff, idx);
                RelTex_Decode(&t[r], r);
            } else if (idx != 0xFFFF && rp[r].ok) {
                RelTex_Read(&t[r], &rp[r], mp, idx);
                RelTex_Decode(&t[r], r);
            }
        }
        if (t[fmt].ok && kind) {
            /* interface art: the set's copy, else the base's own (never another release's);
             * a font page: the PC's when it has the same glyphs, else as art */
            if (set != fmt && !t[set].ok && taken != NULL &&
                (setIdx = ShapePair(&rp[set], mp, fmt, i, &t[fmt], taken, &t[set])) != ~0u) {
                taken[setIdx] = 1;
                s_stats.artByShape++;
                Port_Log("mix: %s: the %s's %u by its shape", id, s_relNames[set], (unsigned)setIdx);
            }
            if (kind == PORT_IFACE_ART && set == PORT_DATA_PS2 && set != fmt && s_objTex != NULL &&
                i < s_numObjTex && s_objTex[i] != 0xFFFF && s_objTex[i] != setIdx && rp[set].ok && t[set].ok) {
                /* the set has a copy (paired, or by shape): the one the paired PS2 object shows, where
                 * the PS2 has two (its chrome and blue logos); art the PS2 has none of (pad glyphs)
                 * stays the base's */
                RelTex c;

                RelTex_Read(&c, &rp[set], mp, s_objTex[i]);
                RelTex_Decode(&c, set);
                if (c.ok && c.w == t[fmt].w && c.h == t[fmt].h) {   /* another copy of the art, not other art */
                    free(t[set].rgba);
                    t[set] = c;
                    Port_Log("mix: %s: the ps2's %u, as its object shows (not %u)", id, (unsigned)s_objTex[i],
                             (unsigned)setIdx);
                    setIdx = s_objTex[i];
                    s_stats.artByObject++;
                } else {
                    free(c.rgba);
                }
            }
            pick = set != fmt && t[set].ok && t[set].w <= 0x200 && t[set].h <= 0x200 ? set : fmt;
            if (kind == PORT_IFACE_FONT && pick != PORT_DATA_PC && t[PORT_DATA_PC].ok &&
                t[PORT_DATA_PC].w <= 0x200 && t[PORT_DATA_PC].h <= 0x200) {
                f32 d = GlyphDistance(&t[pick], &t[PORT_DATA_PC]);

                if (d < FONT_LIMIT) {
                    pick = PORT_DATA_PC;
                } else {
                    s_stats.fontOther++;
                    Port_Log("mix: %s: the PC's font page has other glyphs (%ux%u vs %ux%u, distance %.1f), the "
                             "%s's kept", id, (unsigned)t[PORT_DATA_PC].w, (unsigned)t[PORT_DATA_PC].h,
                             (unsigned)t[pick].w, (unsigned)t[pick].h, (double)d, s_relNames[pick]);
                }
            }
            if (kind == PORT_IFACE_ART && pick != set) {
                s_stats.artFallback++;
            }
            s_stats.uiElsewhere += mp->type != 1;
            if (pick != fmt) {
                MakeTex(&nt[i], pick, &t[pick], !lookGC);
            } else if (fmt == PORT_DATA_GC && !lookGC) {
                MakeTex(&nt[i], fmt, &t[fmt], 1);
                s_stats.texConverted += nt[i].pix != NULL;
            }
            if (pick != fmt && nt[i].pix == NULL) {
                pick = fmt;     /* out of memory: the base's stays */
            }
            if (kind == PORT_IFACE_FONT) {
                s_stats.fontFrom[pick]++;
            } else {
                s_stats.artFrom[pick]++;
            }
            Port_MixManifestUI(id, pick, pick == fmt ? i : pick == set ? setIdx : pair[pick], Fnv64(t[pick].pix, t[pick].size),
                               kind == PORT_IFACE_FONT ? "font" : "art");
        } else if (t[fmt].ok) {
            if (n == PORT_MIX_FROM_BEST) {
                pick = BestTexture(t, lookGC);
            } else if (n > 0) {
                for (k = 0; k < n; k++) {
                    if (t[order[k]].ok) {
                        pick = order[k];
                        break;
                    }
                }
            }
            if (pick != fmt && (t[pick].w > 0x200 || t[pick].h > 0x200 ||   /* Render_CreateTextureSlot's limit */
                                ThumbDistance(&t[fmt], &t[pick]) >= TEX_LIMIT)) {
                s_stats.texRejected++;
                pick = fmt;
            }
            if (pick != fmt) {
                MakeTex(&nt[i], pick, &t[pick], !lookGC);
                if (nt[i].pix != NULL) {
                    s_stats.texFrom[pick]++;
                    mp->protoTex += pick == PORT_MIX_PROTO;
                    Port_MixManifest(id, pick, pair[pick], Fnv64(t[pick].pix, t[pick].size));
                }
            } else if (fmt == PORT_DATA_GC && !lookGC) {
                MakeTex(&nt[i], fmt, &t[fmt], 1);
                s_stats.texConverted += nt[i].pix != NULL;
            } else {
                s_stats.texKept++;
            }
        } else {
            s_stats.texKept++;
        }
        for (r = 0; r < PORT_MIX_RELS; r++) {
            free(t[r].rgba);
        }
        if (nt[i].pix != NULL) {
            total += ((nt[i].size + 0x1F) & ~0x1Fu) + 0x20;
            changed++;
        }
    }
    for (r = 0; r < PORT_MIX_RELS; r++) {
        RelPack_Close(&rp[r]);
    }
    block = changed != 0 && total < 0x10000000 ? Allocate(total + 0x20, 0, "") : NULL;
    if (block != NULL) {
        Xdl_NoteBlock(XDL_SEG_BLOCK, block, total + 0x20);     /* and its pointers: for the pack cache (xdl.c) */
        PortGeometry_Keep(pack, block);
        out = (u8 *)PTR((HND(block) + 0x1F) & ~0x1Fu);
        hp = base + texOff;
        for (i = 0; i < numTex && (u32)(hp - base) + 0x10 <= dataSize; i++) {
            u16 *h = (u16 *)hp;

            if (nt[i].pix != NULL) {
                memcpy(out, nt[i].pix, nt[i].size);
                memset(out + nt[i].size, 0, 0x20);
                h[0] = Rev16(nt[i].fmt);
                h[1] = Rev16(nt[i].w);
                h[2] = Rev16(nt[i].h);
                h[3] = Rev16(nt[i].levels);
                SLOT_STORE(h + 4, out);
                Xdl_NoteSlot(h + 4);
                out += ((nt[i].size + 0x1F) & ~0x1Fu) + 0x20;
            }
            /* as DataPack_RefreshTextures steps */
            hp += 0x10;
            if (fmt == PORT_DATA_GC && (HND(hp) & 0x1F)) {
                hp += 0x20 - (HND(hp) & 0x1F);
            }
        }
    }
    for (i = 0; i < numTex; i++) {
        free(nt[i].pix);
    }
    free(nt);
    free(ui);
    free(taken);
    free(s_objTex);
    s_objTex = NULL;
}

/* ---- sound effects ------------------------------------------------------------------ */

typedef struct NewSfx {
    u8 *pcm;                /* PCM16, host order (malloc) */
    u32 length, loop, rate;
} NewSfx;

static _Thread_local NewSfx *s_sfx;
static _Thread_local u32 s_numSfx;
static _Thread_local void *s_sfxBlock;

/* The base's descriptors, before its own conversion: big-endian on GameCube. */
void PortMix_SFXPrepare(s32 fmt, const u8 *desc, u32 count) {
    MixPack *mp = Cur();
    RelPack rp[PORT_MIX_RELS];
    int opened[PORT_MIX_RELS] = { 0 }, r;
    u32 i;

    s_sfx = NULL;
    s_numSfx = 0;
    if (s_collect || mp == NULL || mp->pairs == NULL || mp->pairs->numSounds == 0 || count == 0 || count > 0x10000 ||
        fmt < 0 || fmt >= PORT_DATA_COUNT) {
        return;
    }
    for (i = 0; i < count; i++) {
        const u16 *pair = FindPair(mp, 1, fmt, i);
        const u8 *d = desc + i * sizeof(SFX), *snd, *dd;
        int order[PORT_MIX_RELS], n, pick = fmt, k, be = fmt == PORT_DATA_GC;
        u32 len = Rd32(d + 4, be), rate = Rd32(d + 0xC, be), bits = d[0x11], dlen, drate, ddata, samples;
        f32 dur, ddur;
        char id[0xC0];

        if (pair == NULL) {
            s_stats.sfxKept++;
            continue;
        }
        AssetId(id, sizeof(id), mp, "sound", pair, fmt, i);
        n = Port_MixAssetRule(id, order);
        if (n == PORT_MIX_FROM_BEST) {      /* ultimate.md §3: PCM16, the same on both */
            order[0] = PORT_DATA_GC;
            order[1] = PORT_DATA_PC;
            n = 2;
        }
        for (k = 0; k < n; k++) {
            /* the PS2's ADPCM is never the better copy; the prototype's is its own sound */
            if (order[k] == fmt || (order[k] != PORT_DATA_PS2 && pair[order[k]] != 0xFFFF &&
                                    Port_MixRoot(order[k]) != NULL)) {
                pick = order[k];
                break;
            }
        }
        if (pick == fmt) {
            s_stats.sfxKept++;
            continue;
        }
        if (!opened[pick]) {
            RelPack_Open(&rp[pick], mp, pick);
            opened[pick] = 1;
        }
        snd = RelPack_Sound(&rp[pick], mp);
        if (snd == NULL || pair[pick] >= rp[pick].hdr[H_NUMSOUNDS] ||
            (u64)rp[pick].hdr[H_SFXOFF] + ((u64)pair[pick] + 1) * sizeof(SFX) > rp[pick].hdr[H_SOUNDSIZE]) {
            s_stats.sfxRejected++;
            continue;
        }
        be = pick == PORT_DATA_GC;
        dd = snd + rp[pick].soundBase + rp[pick].hdr[H_SFXOFF] + pair[pick] * sizeof(SFX);
        ddata = Rd32(dd, be);
        dlen = Rd32(dd + 4, be);
        drate = Rd32(dd + 0xC, be);
        /* the same sound: PCM16 inside the pack (the prototype's ADPCM), as
         * long as the base's within 15% */
        samples = fmt == PORT_DATA_PS2 ? (len >= 16 ? (len / 16 - 1) * 28 : 0) : len / (bits == 16 ? 2 : 1);
        dur = rate ? (f32)samples / (f32)rate : 0;
        if (pick == PORT_MIX_PROTO) {
            ddur = drate && dlen >= 32 ? (f32)((dlen / 16 - 1) * 28) / (f32)drate : 0;
        } else {
            ddur = drate ? (f32)(dlen / 2) / (f32)drate : 0;
        }
        if ((pick == PORT_MIX_PROTO ? dlen < 32 || (dlen & 15) : dd[0x11] != 16 || dlen == 0 || (dlen & 1)) ||
            ddata > rp[pick].hdr[H_SOUNDSIZE] ||
            dlen > rp[pick].hdr[H_SOUNDSIZE] - ddata || dur <= 0 || ddur < dur * 0.85f - 0.05f ||
            ddur > dur * 1.15f + 0.05f) {
            s_stats.sfxRejected++;
            continue;
        }
        if (s_sfx == NULL) {
            s_sfx = calloc(count, sizeof(NewSfx));
            if (s_sfx == NULL) {
                break;
            }
            s_numSfx = count;
        }
        if (pick == PORT_MIX_PROTO) {
            /* SPU-ADPCM: block 0 is silence; the length and loop in PCM bytes */
            s_sfx[i].pcm = PortPS2_DecodeSound(snd + rp[pick].soundBase + ddata, dlen, Rd32(dd + 8, 0),
                                               &s_sfx[i].length, &s_sfx[i].loop);
            if (s_sfx[i].pcm == NULL) {
                continue;
            }
        } else {
            s_sfx[i].pcm = malloc(dlen);
            if (s_sfx[i].pcm == NULL) {
                continue;
            }
            memcpy(s_sfx[i].pcm, snd + rp[pick].soundBase + ddata, dlen);
            if (be) {
                Port_SwapArray16(s_sfx[i].pcm, dlen / 2);
            }
            s_sfx[i].length = dlen;
            s_sfx[i].loop = Rd32(dd + 8, be);
        }
        s_sfx[i].rate = drate;
        s_stats.sfxFrom[pick]++;
        mp->protoSfx += pick == PORT_MIX_PROTO;
        Port_MixManifest(id, pick, pair[pick], Fnv64(snd + rp[pick].soundBase + ddata, dlen));
    }
    for (r = 0; r < PORT_MIX_RELS; r++) {
        if (opened[r]) {
            RelPack_Close(&rp[r]);
        }
    }
}

/* A mod's own sound for sound `index` of the pack (Port_ModsSFX), over what
 * PortMix_SFXPrepare chose: pcm (malloc'd PCM16, host order) is taken. */
void PortMix_SFXSet(u32 index, u32 count, u8 *pcm, u32 length, u32 loop, u32 rate) {
    if (s_sfx == NULL && count != 0 && count <= 0x10000) {
        s_sfx = calloc(count, sizeof(NewSfx));
        s_numSfx = s_sfx != NULL ? count : 0;
    }
    if (index >= s_numSfx) {
        free(pcm);
        return;
    }
    free(s_sfx[index].pcm);
    s_sfx[index].pcm = pcm;
    s_sfx[index].length = length;
    s_sfx[index].loop = loop;
    s_sfx[index].rate = rate;
}

u32 PortMix_SFXLength(u32 index) {
    return index < s_numSfx && s_sfx[index].pcm != NULL ? s_sfx[index].length : 0;
}

void PortMix_SFXFill(u32 index, void *desc, u8 *out) {
    SFX *sfx = desc;
    NewSfx *n = &s_sfx[index];

    memcpy(out, n->pcm, n->length);
    memset(out + n->length, 0, 0x20);       /* CreateSFX_ copies up to 32 bytes past the end */
    sfx->data = HND(out);
    Xdl_NoteSlot(&sfx->data);
    sfx->length = n->length;
    sfx->loop = n->loop;
    sfx->rate = n->rate;
    sfx->unused10 = 0;
    sfx->bits = 16;
}

void PortMix_SFX(s32 fmt, u8 *desc, u32 count) {
    SFX *sfx = (SFX *)desc;
    u32 i, total = 0;
    u8 *out;

    if (s_numSfx == 0 || fmt == PORT_DATA_PS2) {   /* the PS2's went into its own block (PortPS2_SFX) */
        return;
    }
    for (i = 0; i < count && i < s_numSfx; i++) {
        if (s_sfx[i].pcm != NULL) {
            total += ((s_sfx[i].length + 0x1F) & ~0x1Fu) + 0x20;
        }
    }
    s_sfxBlock = total && total < 0x10000000 ? Allocate(total + 0x20, 0, "") : NULL;
    Xdl_NoteBlock(XDL_SEG_SFX, s_sfxBlock, total + 0x20);
    if (s_sfxBlock == NULL) {
        return;
    }
    out = (u8 *)PTR((HND(s_sfxBlock) + 0x1F) & ~0x1Fu);
    for (i = 0; i < count && i < s_numSfx; i++) {
        if (s_sfx[i].pcm != NULL) {
            PortMix_SFXFill(i, &sfx[i], out);
            out += ((s_sfx[i].length + 0x1F) & ~0x1Fu) + 0x20;
        }
    }
}

void PortMix_SFXDone(void) {
    u32 i;

    if (s_sfxBlock != NULL) {
        Xdl_BlockDone(s_sfxBlock);
        Deallocate(s_sfxBlock);
        s_sfxBlock = NULL;
    }
    for (i = 0; i < s_numSfx; i++) {
        free(s_sfx[i].pcm);
    }
    free(s_sfx);
    s_sfx = NULL;
    s_numSfx = 0;
}

/* ---- the builder ------------------------------------------------------------------------ */

void PortMix_LogStats(void) {
    if (Port_MixInterface() == PORT_DATA_PS2) {
        Port_Log("mix: interface colours from the ps2: %u packs, %u interfaces paired (%u of them object by object; %u "
                 "not), %u colour tracks "
                 "(%u keys changed), %u with other keys taken by time, %u skipped, %u blend modes, %u polygon points, "
                 "%u briefing colours; %u objects of the PS2's own art shown (its title's subtitle)",
                 (unsigned)s_colStats.packs, (unsigned)s_colStats.headers, (unsigned)s_colStats.headersByObject,
                 (unsigned)s_colStats.headersUnpaired,
                 (unsigned)s_colStats.tracks, (unsigned)s_colStats.keys, (unsigned)s_colStats.tracksByTime,
                 (unsigned)s_colStats.tracksSkipped, (unsigned)s_colStats.blends,
                 (unsigned)s_colStats.points, (unsigned)s_colStats.dialog, (unsigned)s_colStats.ps2Art);
    }
    Port_Log("mix: %u packs; textures from gc %u, pc %u, ps2 %u, prototype %u, base's own %u (%u of them not like "
             "their pair), to the PC's colours %u; sounds from gc %u, pc %u, prototype %u, base's own %u (%u not like "
             "their pair)",
             (unsigned)s_stats.packs, (unsigned)s_stats.texFrom[0], (unsigned)s_stats.texFrom[1],
             (unsigned)s_stats.texFrom[2], (unsigned)s_stats.texFrom[PORT_MIX_PROTO],
             (unsigned)(s_stats.texKept + s_stats.texRejected), (unsigned)s_stats.texRejected,
             (unsigned)s_stats.texConverted, (unsigned)s_stats.sfxFrom[0], (unsigned)s_stats.sfxFrom[1],
             (unsigned)s_stats.sfxFrom[PORT_MIX_PROTO], (unsigned)(s_stats.sfxKept + s_stats.sfxRejected),
             (unsigned)s_stats.sfxRejected);
    Port_Log("mix: interface %s: art from gc %u, pc %u, ps2 %u (%u the paired PS2 object's, %u paired by shape; %u the base's own: the set has no "
             "copy); font pages "
             "from gc %u, pc %u, ps2 %u (%u with other glyphs on the PC); %u of them in packs other than interface "
             "packs; %u of them shown by other packs' interfaces (%u found)", Port_MixInterface() == PORT_DATA_PS2 ? "silver (ps2)" : "yellow (gc)",
             (unsigned)s_stats.artFrom[0], (unsigned)s_stats.artFrom[1], (unsigned)s_stats.artFrom[2],
             (unsigned)s_stats.artByObject, (unsigned)s_stats.artByShape, (unsigned)s_stats.artFallback,
             (unsigned)s_stats.fontFrom[0], (unsigned)s_stats.fontFrom[1],
             (unsigned)s_stats.fontFrom[2], (unsigned)s_stats.fontOther, (unsigned)s_stats.uiElsewhere,
             (unsigned)s_stats.uiShared, (unsigned)s_numUiRefs);
}

typedef struct Build {
    u32 archives, packs, failed;
    u32 total, done;            /* the packs to load (a first pass counts them), and loaded so far */
} Build;

/* The packs `type`/`id` of archive `name` refers to, loaded first, as the
 * game has them by the time it loads a pack: DataPack_LoadDataPack keeps its
 * header in a static, so a pack loading inside another's load would leave
 * the outer one reading the inner one's counts. They stay loaded. */
static void LoadRefs(const char *name, u32 type, u32 id, int depth) {
    static char names[16][0xC];
    static u32 numNames;
    MixPack mp;
    RelPack rp;
    u32 i, k;
    int c, rel;

    memset(&mp, 0, sizeof(mp));
    snprintf(mp.file, sizeof(mp.file), "%s.xgc", name);
    for (i = 0; mp.file[i]; i++) {
        mp.file[i] = mp.file[i] == '\\' ? '/' : (char)tolower((u8)mp.file[i]);
    }
    mp.type = type;
    mp.id = id;
    memset(&rp, 0, sizeof(rp));
    for (c = 0; !rp.ok && (rel = Port_MixCandidate(mp.file, c)) >= 0; c++) {
        RelPack_Open(&rp, &mp, rel);
    }
    for (i = 0; rp.ok && i < 2 && depth < 4; i++) {
        s32 slot;

        if (rp.refName[i][0] == 0 || FileStreaming_GetPackID(rp.refName[i], 8, rp.refId[i]) >= 0) {
            continue;
        }
        LoadRefs(rp.refName[i], 8, rp.refId[i], depth + 1);
        for (k = 0; k < numNames && strcmp(names[k], rp.refName[i]) != 0; k++) {
        }
        if (k == numNames) {
            if (numNames == 16) {
                continue;
            }
            memcpy(names[numNames++], rp.refName[i], 0xC);
        }
        slot = FileStreaming_LoadDataPack(names[k], 8, rp.refId[i], 0);   /* the slot keeps the name's pointer */
        if (slot >= 0) {
            FileStreaming_InitializeDataPack((u32)slot, 8, 1);
        }
    }
}

/* Archive `name`'s TOC (malloc'd) from the release the game will open it
 * in; its entry count, 0 without one. */
static u32 ReadToc(const char *name, MixPack *mp, u8 **toc, int *be) {
    u8 head[0x10];
    u32 count, i;
    int k, rel = -1;

    *toc = NULL;
    memset(mp, 0, sizeof(*mp));
    snprintf(mp->file, sizeof(mp->file), "%s.xgc", name);
    for (i = 0; mp->file[i]; i++) {
        mp->file[i] = mp->file[i] == '\\' ? '/' : (char)tolower((u8)mp->file[i]);
    }
    for (k = 0; (rel = Port_MixCandidate(mp->file, k)) >= 0; k++) {
        if (Port_MixRead(rel, mp->file, 0, head, sizeof(head)) == sizeof(head)) {
            break;
        }
    }
    *be = rel == PORT_DATA_GC;
    if (rel < 0 || Rd32(head, *be) != 0x020100A0 || Rd32(head + 8, *be) != 0xB7 ||
        (count = Rd32(head + 12, *be)) == 0 || count > 0x10000 || (*toc = malloc(count * TOC_ENTRY)) == NULL ||
        Port_MixRead(rel, mp->file, 0x10, *toc, count * TOC_ENTRY) != count * TOC_ENTRY) {
        free(*toc);
        *toc = NULL;
        return 0;
    }
    return count;
}

/* A TOC entry the builder loads: not a world's quadrants, which load with
 * the world (FileStreaming.c). */
static int Buildable(const u8 *toc, u32 i, int be) {
    u32 type = Rd32(toc + i * TOC_ENTRY + 4, be);

    return type < 11 && type != 7;
}

static void CountArchive(const char *name, void *user) {
    Build *b = user;
    MixPack mp;
    u8 *toc;
    int be;
    u32 count = ReadToc(name, &mp, &toc, &be), i;

    for (i = 0; i < count; i++) {
        b->total += (u32)Buildable(toc, i, be);
    }
    free(toc);
}

static void BuildArchive(const char *name, void *user) {
    Build *b = user;
    MixPack mp;
    u8 *toc;
    u32 count, i, done = 0;
    int be;

    count = ReadToc(name, &mp, &toc, &be);
    if (count == 0) {
        Port_Log("mix: %s: no pack archive, skipped", name);
        return;
    }
    b->archives++;
    for (i = 0; i < count; i++) {
        u32 type = Rd32(toc + i * TOC_ENTRY + 4, be), id = Rd32(toc + i * TOC_ENTRY + 8, be);
        DataPack *pack;

        if (!Buildable(toc, i, be)) {
            continue;
        }
        /* progress, for the launcher (as Port_Convert.h's) */
        printf("@convert %u %u %s\n", (unsigned)b->done, (unsigned)b->total, name);
        fflush(stdout);
        b->done++;
        LoadRefs(name, type, id, 0);
        pack = Allocate(sizeof(DataPack), 0, "");
        if (pack == NULL) {
            printf("@error out of memory\n");
            fflush(stdout);
            Port_Fatal("mix: out of memory");
        }
        memset(pack, 0, sizeof(DataPack));
        if (DataPack_LoadFile((char *)name, HND(pack), type, id) && GETP(pack->soundIDs) != NULL) {
            done++;
        } else {
            b->failed++;
            Port_Log("mix: %s %s_%u did not load", name, s_typeNames[type], (unsigned)id);
        }
        /* nothing of it was created (textures, shaders, triggers), so there
         * is only memory to free */
        pack->numSounds = pack->numGX = pack->numShaders = pack->numTextures = pack->numTriggers = 0;
        DataPack_ReleaseDataPack(pack);
        Deallocate(pack);
    }
    free(toc);
    DataPack_CloseFiles();
    b->packs += done;
    Port_Log("mix: [%u] %s: %u packs", (unsigned)b->archives, name, (unsigned)done);
}

u32 PortMix_BuildAll(u32 *failed) {
    Build b;
    u32 i;

    memset(&b, 0, sizeof(b));
    DataPack_EnableLoadingSounds(0);    /* converted and cached, not created */
    Port_MixArchives(CountArchive, &b);
    /* first pass, nothing cached: what interfaces show of other packs */
    b.total *= 2;
    s_collect = 1;
    Xdl_SetModes(0, 0);
    Port_MixArchives(BuildArchive, &b);
    s_collect = 0;
    Xdl_SetModes(1, 1);
    Port_Log("mix: %u textures of share packs shown by other packs' interfaces", (unsigned)s_numUiRefs);
    for (i = 0; i < s_numUiRefs; i++) {
        Port_Log("mix:   %s share_%u texture %u (%s)", s_uiRefs[i].archive, (unsigned)s_uiRefs[i].id,
                 (unsigned)s_uiRefs[i].index, s_uiRefs[i].what == PORT_IFACE_FONT ? "font"
                 : s_uiRefs[i].what == PORT_IFACE_GEOM ? "geometry slot" : "art");
    }
    b.archives = b.packs = b.failed = 0;
    memset(&s_stats, 0, sizeof(s_stats));
    memset(&s_colStats, 0, sizeof(s_colStats));
    Port_MixArchives(BuildArchive, &b);
    PortMix_LogStats();
    if (b.failed) {
        Port_Log("mix: %u packs did not load", (unsigned)b.failed);
    }
    *failed = b.failed;
    return b.packs;
}

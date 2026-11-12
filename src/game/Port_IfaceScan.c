/* A pack's interfaces, found in its blob: Port_IfaceScan.h. Port-only file.
 *
 * The header test follows InterfaceManager_Create and _AddObject: a header
 * (InterfaceHeader) has 1..200 object definitions (IfaceObjectDef, 0x6C) at
 * a pointer slot, each of a type AddObject knows, its sub-data, default
 * animation and states null or at pointer slots, a child interface's slot a
 * pointer slot. The texture walk follows what draws a pack texture: an
 * image's texture (IfaceImageDef.value), an image's texture key animation
 * (its keys), a text's or box's font (IfaceFont.texture), a briefing
 * character's font and idle, talking and special animations
 * (InterfaceManager_MoveDialogSystem). */
#include "common.h"
#include "Interface.h"
#include "Port_Swap.h"
#include "Port_IfaceScan.h"

#define DEF_SIZE 0x6C

static int InBlob(const PortIfaceScan *s, u32 off, u32 n) {
    return off <= s->size && n <= s->size - off;
}

static u32 Rd(const PortIfaceScan *s, u32 off) {
    u32 v = 0;

    if (InBlob(s, off, 4)) {
        memcpy(&v, s->base + off, 4);
    }
    return v;
}

static int PtrSlot(const PortIfaceScan *s, u32 off) {
    return (off & 3) == 0 && InBlob(s, off, 4) && s->isPtr[off / 4];
}

u32 Port_IfaceTo(const PortIfaceScan *s, u32 off) {
    u32 v = Rd(s, off), t = v - s->bias;

    return v != 0 && t < s->size ? t : ~0u;
}

int Port_IfaceScanInit(PortIfaceScan *s, const u8 *base, u32 size, u32 bias, const u32 *pointers, u32 numPointers,
                       const u32 *refs, u32 numRefs) {
    u32 i;

    s->base = base;
    s->size = size;
    s->bias = bias;
    s->isPtr = calloc(size / 4 + 1, 1);
    if (s->isPtr == NULL) {
        return 0;
    }
    for (i = 0; pointers != NULL && i < numPointers; i++) {
        if (!(pointers[i] & 3) && pointers[i] / 4 <= size / 4) {
            s->isPtr[pointers[i] / 4] = 1;
        }
    }
    for (i = 0; refs != NULL && i < numRefs; i++) {
        if ((refs[i * 2 + 1] >> 16) == 0 && !(refs[i * 2] & 3) && refs[i * 2] / 4 <= size / 4) {
            s->isPtr[refs[i * 2] / 4] = 1;
        }
    }
    return 1;
}

void Port_IfaceScanFree(PortIfaceScan *s) {
    free(s->isPtr);
    s->isPtr = NULL;
}

/* A handle field: null, or a pointer slot to `n` bytes in this pack. */
static int NullOrPtr(const PortIfaceScan *s, u32 off, u32 n) {
    u32 t;

    if (Rd(s, off) == 0) {
        return 1;
    }
    t = Port_IfaceTo(s, off);
    return PtrSlot(s, off) && t != ~0u && InBlob(s, t, n);
}

static int IsDef(const PortIfaceScan *s, u32 d) {
    const IfaceObjectDef *def = (const IfaceObjectDef *)(s->base + d);

    switch (def->type) {
    case 0: case 1: case 2: case 3: case 4: case 6: case 7: case 10: case 11: case 12:
        break;
    default:
        return 0;
    }
    /* the alignment is -1..1 in use; some unused objects have other values, never huge or NaN */
    if (!(def->anchorX >= -1000.0f && def->anchorX <= 1000.0f && def->anchorY >= -1000.0f && def->anchorY <= 1000.0f)) {
        return 0;
    }
    if (def->numStates > 64 || !NullOrPtr(s, d + 0x60, sizeof(IfaceObjAnim)) || !NullOrPtr(s, d + 0x14, 4) ||
        !NullOrPtr(s, d + 0x68, def->numStates * sizeof(IfaceStateDef))) {
        return 0;
    }
    return !(def->type == 6 || def->type == 7) || PtrSlot(s, d + 0x3C);
}

static int IsHeader(const PortIfaceScan *s, u32 h) {
    const InterfaceHeader *hdr;
    u32 objs, i;

    if ((h & 3) || !InBlob(s, h, sizeof(InterfaceHeader))) {
        return 0;
    }
    hdr = (const InterfaceHeader *)(s->base + h);
    if (hdr->numObjects == 0 || hdr->numObjects > 200 || hdr->events.count > 256 || hdr->mode > 4 ||
        hdr->layer > 64 || !PtrSlot(s, h + 0x10) || !NullOrPtr(s, h + 0x18, hdr->events.count * 0xC) ||
        !NullOrPtr(s, h + 0x1C, 8) || !NullOrPtr(s, h + 0x30, 8)) {
        return 0;
    }
    objs = Port_IfaceTo(s, h + 0x10);
    if (objs == ~0u || !InBlob(s, objs, hdr->numObjects * DEF_SIZE)) {
        return 0;
    }
    for (i = 0; i < hdr->numObjects; i++) {
        if (!IsDef(s, objs + i * DEF_SIZE)) {
            return 0;
        }
    }
    return 1;
}

u8 *Port_IfaceHeaders(const PortIfaceScan *s, u32 *count) {
    u32 words = s->size / 4, i, t;
    u8 *seen = s->isPtr != NULL ? calloc(words + 1, 1) : NULL;

    *count = 0;
    for (i = 0; seen != NULL && i < words; i++) {
        if (!s->isPtr[i] || (t = Port_IfaceTo(s, i * 4)) == ~0u || (t & 3) || seen[t / 4] || !IsHeader(s, t)) {
            continue;
        }
        seen[t / 4] = 1;
        (*count)++;
    }
    return seen;
}

/* ---- textures ------------------------------------------------------------ */

/* `what`: PORT_IFACE_ART or PORT_IFACE_FONT; a slot both are drawn through counts as art. */
static void Mark(const PortIfaceScan *s, u8 *mark, u32 off, u8 what) {
    if (off != ~0u && InBlob(s, off & ~3u, 4) && mark[off / 4] != PORT_IFACE_ART) {
        mark[off / 4] = what;
    }
}

static void MarkWord(const PortIfaceScan *s, u8 *mark, u32 off) {
    Mark(s, mark, off, PORT_IFACE_ART);
}

/* A texture key animation (TextureKeyAnim: numKeys, keys -> u32 texture slots). */
static void MarkTexAnim(const PortIfaceScan *s, u8 *mark, u32 anim) {
    u32 keys, n, i;

    if (anim == ~0u || !InBlob(s, anim, sizeof(TextureKeyAnim))) {
        return;
    }
    n = Rd(s, anim);
    keys = Port_IfaceTo(s, anim + 4);
    for (i = 0; keys != ~0u && i < n && i < 0x400; i++) {
        MarkWord(s, mark, keys + i * 4);
    }
}

static void MarkFont(const PortIfaceScan *s, u8 *mark, u32 font) {
    Mark(s, mark, font, PORT_IFACE_FONT);    /* IfaceFont.texture */
}

static void MarkDialog(const PortIfaceScan *s, u8 *mark, u32 list) {
    u32 n = Rd(s, list), entries = Port_IfaceTo(s, list + 4), i, k, j;

    for (i = 0; list != ~0u && entries != ~0u && i < n && i < 0x400 && InBlob(s, entries + i * 0x20, 0x20); i++) {
        u32 chr = Port_IfaceTo(s, entries + i * 0x20 + 0x14), sp, maxKey = 0;
        static const u32 lists[2][2] = { { 0x10, 0x14 }, { 0x18, 0x1C } };   /* idle, talk: count, list */

        if (chr == ~0u || !InBlob(s, chr, sizeof(IfaceDialogChar))) {
            continue;
        }
        MarkFont(s, mark, Port_IfaceTo(s, chr + 0x8));
        for (k = 0; k < 2; k++) {
            u32 cnt = Rd(s, chr + lists[k][0]), l = Port_IfaceTo(s, chr + lists[k][1]);

            for (j = 0; l != ~0u && j < cnt && j < 0x100; j++) {
                MarkTexAnim(s, mark, l + j * sizeof(IfaceTexAnim));
            }
        }
        /* special animations: indexed by the entries' specialKey, no count */
        for (j = 0; j < n && j < 0x400 && InBlob(s, entries + j * 0x20, 0x20); j++) {
            const u8 *e = s->base + entries + j * 0x20;

            if (Port_IfaceTo(s, entries + j * 0x20 + 0x14) == chr && e[2] && e[3] + 1u > maxKey) {
                maxKey = e[3] + 1u;
            }
        }
        sp = Port_IfaceTo(s, chr + 0x30);
        for (j = 0; sp != ~0u && j < maxKey; j++) {
            MarkTexAnim(s, mark, sp + j * sizeof(IfaceTexAnim));
        }
    }
}

static void MarkHeader(const PortIfaceScan *s, u8 *mark, u32 h) {
    const InterfaceHeader *hdr = (const InterfaceHeader *)(s->base + h);
    u32 objs = Port_IfaceTo(s, h + 0x10), i;

    for (i = 0; objs != ~0u && i < hdr->numObjects; i++) {
        u32 d = objs + i * DEF_SIZE, sub = Port_IfaceTo(s, d + 0x14);

        if (sub == ~0u) {
            continue;
        }
        switch (s->base[d + 1]) {   /* IfaceObjectDef.type */
        case 0:
            if (InBlob(s, sub, sizeof(IfaceImageDef))) {
                MarkWord(s, mark, sub + 4);     /* the texture (type 0) */
                if (s->base[sub] == 3) {
                    MarkTexAnim(s, mark, Port_IfaceTo(s, sub + 0x10));
                }
            }
            break;
        case 1:
        case 12:
            MarkFont(s, mark, Port_IfaceTo(s, sub + 4));
            break;
        case 10:
            MarkFont(s, mark, Port_IfaceTo(s, sub + 0x10));
            break;
        case 2:
            MarkDialog(s, mark, sub);
            break;
        }
    }
}

u32 Port_IfaceTextures(const PortIfaceScan *s, const u32 *texRelocs, u32 numTexRelocs, u8 *used, u32 numTex) {
    u32 count = 0, i, words = s->size / 4;
    u8 *headers = Port_IfaceHeaders(s, &count), *mark;

    count = 0;
    mark = headers != NULL ? calloc(words + 1, 1) : NULL;
    for (i = 0; mark != NULL && i < words; i++) {
        if (headers[i]) {
            MarkHeader(s, mark, i * 4);
        }
    }
    for (i = 0; mark != NULL && i < numTexRelocs; i++) {
        u32 slot = texRelocs[i * 2] & ~PORT_TEXRELOC_U16, id = texRelocs[i * 2 + 1];

        if ((id >> 16) == 0 && slot / 4 <= words && mark[slot / 4] && (id & 0xFFFF) < numTex &&
            used[id & 0xFFFF] != PORT_IFACE_ART) {
            count += used[id & 0xFFFF] == 0;
            used[id & 0xFFFF] = mark[slot / 4];
        }
    }
    free(mark);
    free(headers);
    return count;
}

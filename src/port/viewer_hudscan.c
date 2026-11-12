/* Viewer mode: the interfaces of a loaded pack, for the widescreen HUD check
 * (the viewer's "interfaces" command; tools/hud_scan.py runs it over every
 * pack of a release).
 *
 * The headers are found as Port_IfaceScan.c finds them (every pointer
 * target tried as one). For each header it writes the
 * objects as InterfaceManager_AddObject would make them (kind, role, anchor)
 * and their layout position, with Port_HudLayoutX's maths
 * (InterfaceManager.c): the default animation's first position key, or the
 * anchor's, plus the parent placeholder's for a child interface. Child
 * interfaces (definition types 6, 7) are listed with the placeholder they
 * hang from; tools/hud_scan.py adds the parent's x. Nothing is changed. */
#include "viewer_internal.h"
#include "Interface.h"
#include "Port_Swap.h"
#include "Port_IfaceScan.h"

#define MAX_DEPTH 8
#define DEF_SIZE 0x6C

static u8 *Base(VPack *vp) {
    return GETP(vp->pack->base);
}

static int InBlob(VPack *vp, u32 off, u32 n) {
    return off <= vp->size && n <= vp->size - off;
}

/* The in-pack offset a handle field points to, or ~0 (null or elsewhere). */
static u32 To(VPack *vp, u32 off) {
    u32 h, t;

    if (!InBlob(vp, off, 4)) {
        return ~0u;
    }
    SDL_memcpy(&h, Base(vp) + off, 4);
    t = h - (u32)HND(Base(vp));
    return h != 0 && t < vp->size ? t : ~0u;
}

static u32 Rd(VPack *vp, u32 off) {
    u32 v = 0;

    if (InBlob(vp, off, 4)) {
        SDL_memcpy(&v, Base(vp) + off, 4);
    }
    return v;
}

/* InterfaceObject.kind as AddObject sets it (-1: left as it was). */
static int KindOf(VPack *vp, u32 d) {
    IfaceObjectDef *def = (IfaceObjectDef *)(Base(vp) + d);
    u32 sub = To(vp, d + 0x14);

    switch (def->type) {
    case 0:
        if (sub == ~0u || !InBlob(vp, sub, sizeof(IfaceImageDef))) {
            return -1;
        }
        switch (((IfaceImageDef *)(Base(vp) + sub))->type) {
        case 0: return IFACE_KIND_IMAGE;
        case 1: return IFACE_KIND_GEOMETRY;
        case 3: return IFACE_KIND_ANIM_IMAGE;
        default: return -1;
        }
    case 1: return IFACE_KIND_TEXT;
    case 2: return IFACE_KIND_DIALOG;
    case 3: return IFACE_KIND_DEBUG_VECTORS;
    case 4: return IFACE_KIND_POLY;
    case 6: case 7: return IFACE_KIND_CHILD;
    case 10: return IFACE_KIND_BOX;
    case 11: return IFACE_KIND_LINE;
    case 12: return IFACE_KIND_DEBUG_TEXT;
    }
    return -1;
}

/* The first key of the default animation's track at `track` (0x00 pos, 0x10
 * scale), or 0 with *has = 0. */
static int FirstKey(VPack *vp, u32 d, u32 track, f32 *xy) {
    u32 anim = To(vp, d + 0x60), keys;

    if (anim == ~0u || Rd(vp, anim + track) == 0) {
        return 0;
    }
    keys = To(vp, anim + track + 4);
    if (keys == ~0u || !InBlob(vp, keys, 8)) {
        return 0;
    }
    SDL_memcpy(xy, Base(vp) + keys, 8);
    return 1;
}

/* Port_HudLayoutX/Y within the header (the parent's part added by the tool):
 * *flags |= 1 when a position comes from no key (the live object's leftover
 * value at run time), 2 when an anchor points outside the interface. */
static void LayoutXY(VPack *vp, u32 objs, u32 n, u32 i, u32 depth, f32 *xy, u32 *flags) {
    IfaceObjectDef *def = (IfaceObjectDef *)(Base(vp) + objs + i * DEF_SIZE);
    s32 a = def->anchor;

    xy[0] = xy[1] = 0.0f;
    if (depth > MAX_DEPTH) {
        return;
    }
    if (a != 0 && a + (s32)i > 0) {
        if ((u32)(a + (s32)i) >= n) {
            *flags |= 2;
            return;
        }
        LayoutXY(vp, objs, n, (u32)(a + (s32)i), depth + 1, xy, flags);
        return;
    }
    if (!FirstKey(vp, objs + i * DEF_SIZE, 0x00, xy)) {
        *flags |= 1;
    }
}

/* The range of x over every position key the object has: its default
 * animation's and its states' (0x20-byte keys, ExtractScaleKey). 0: none. */
static int KeyRange(VPack *vp, u32 d, f32 *lo, f32 *hi) {
    IfaceObjectDef *def = (IfaceObjectDef *)(Base(vp) + d);
    u32 tracks[65], n = 0, i, k, states = To(vp, d + 0x68), anim = To(vp, d + 0x60);
    int any = 0;

    if (anim != ~0u) {
        tracks[n++] = anim;
    }
    for (i = 0; states != ~0u && i < def->numStates && i < 64 && InBlob(vp, states + i * 0xC, 0xC); i++) {
        u32 sa = To(vp, states + i * 0xC + 8);

        if (sa != ~0u && InBlob(vp, sa, sizeof(IfaceStateAnim))) {
            tracks[n++] = sa + 0x08;
        }
    }
    for (i = 0; i < n; i++) {
        u32 count = Rd(vp, tracks[i]), keys = To(vp, tracks[i] + 4);

        for (k = 0; keys != ~0u && k < count && k < 256 && InBlob(vp, keys + k * 0x20, 4); k++) {
            f32 x;

            SDL_memcpy(&x, Base(vp) + keys + k * 0x20, 4);
            if (!any || x < *lo) {
                *lo = x;
            }
            if (!any || x > *hi) {
                *hi = x;
            }
            any = 1;
        }
    }
    return any;
}

static void WriteText(JsonWriter *w, VPack *vp, u32 d) {
    u32 sub = To(vp, d + 0x14), table, str, k;
    char buf[48];

    if (sub == ~0u || (table = To(vp, sub)) == ~0u || (str = To(vp, table)) == ~0u) {
        return;
    }
    for (k = 0; k + 1 < sizeof(buf) && str + k < vp->size && Base(vp)[str + k] != 0; k++) {
        u8 c = Base(vp)[str + k];

        buf[k] = c >= 0x20 && c < 0x7F ? (char)c : '.';
    }
    buf[k] = '\0';
    Jw_String(w, "text", buf);
}

/* Root marks: how the game reaches a header (bits of seen[]). */
enum {
    R_HEADER = 1,
    R_TAC = 2,       /* a level script's InterfacePlay action: created with no owner */
    R_PAUSE = 4,     /* the world's pause menu: no owner */
    R_HUD = 8,       /* a car's HUD (CarAInfo 0x84: 0x10, 0x18, 0x1C): the player's */
    R_PACK = 0x10,   /* an interface pack's root (PlayInterfacePack, menus): no owner */
    R_END = 0x20,    /* a level script's EndInterface action names it (not a creation) */
};

static void Mark(VPack *vp, u8 *seen, u32 slotOff, u8 bit) {
    u32 h = To(vp, slotOff);

    if (h != ~0u && !(h & 3) && (seen[h / 4] & R_HEADER)) {
        seen[h / 4] |= bit;
    }
}

/* Offset in the blob of a handle value, or ~0. */
static u32 HOff(VPack *vp, u32 h) {
    u32 t = h - (u32)HND(Base(vp));

    return h != 0 && t < vp->size ? t : ~0u;
}

static void MarkActions(VPack *vp, u8 *seen, u32 list, u32 n) {
    u32 i, data;

    for (i = 0; list != ~0u && i < n && i < 0x100 && InBlob(vp, list + i * 0x18, 0x18); i++) {
        if ((data = To(vp, list + i * 0x18 + 4)) == ~0u) {
            continue;
        }
        if (Rd(vp, list + i * 0x18) == 4) {
            Mark(vp, seen, data, R_TAC);
        } else if (Rd(vp, list + i * 0x18) == 31) {
            Mark(vp, seen, data + 4, R_END);
        }
    }
}

static void MarkTrigger(VPack *vp, u8 *seen, const u8 *t, int depth) {
    u32 v[9], i;

    SDL_memcpy(v, t, sizeof(v));   /* 0x0 type, 0xC conditions, 0x10 count, 0x14/0x18 actions, 0x1C/0x20 counts */
    MarkActions(vp, seen, HOff(vp, v[5]), v[7]);
    MarkActions(vp, seen, HOff(vp, v[6]), v[8]);
    for (i = 0; depth < 2 && HOff(vp, v[3]) != ~0u && i < v[4] && i < 0x40 &&
                InBlob(vp, HOff(vp, v[3]) + i * 0x2C, 0x2C); i++) {
        MarkTrigger(vp, seen, Base(vp) + HOff(vp, v[3]) + i * 0x2C, depth + 1);
    }
}

static void MarkRoots(VPack *vp, u8 *seen) {
    DataPack *dp = vp->pack;
    u32 i, n, table, obj, gd, hud;

    if (vp->type == 4) {
        gd = To(vp, 0x9C);
        if (gd != ~0u) {
            Mark(vp, seen, gd + 0xCC, R_PAUSE);
        }
    } else if (vp->type == 0 || vp->type == 1) {
        n = Rd(vp, 0);
        table = To(vp, 4);
        for (i = 0; table != ~0u && i < n && i < 64 && InBlob(vp, table + i * 8, 8); i++) {
            if (vp->type == 1) {
                Mark(vp, seen, table + i * 8, R_PACK);
            } else if ((obj = To(vp, table + i * 8)) != ~0u && (hud = To(vp, obj + 0x84)) != ~0u) {
                Mark(vp, seen, hud + 0x10, R_HUD);
                Mark(vp, seen, hud + 0x18, R_HUD);
                Mark(vp, seen, hud + 0x1C, R_HUD);
            }
        }
    }
    for (i = 0; GETP(dp->triggers) != NULL && i < dp->numTriggers && i < 0x4000; i++) {
        MarkTrigger(vp, seen, GETP(dp->triggers) + i * 0x2C, 0);
    }
}

/* Input events that start another header: type 0x1F creates one with no owner;
 * the others whose target is a header go to it (InterfaceManager_Create with
 * the interface's owner when it ends). */
static void WriteEvents(JsonWriter *w, VPack *vp, u8 *seen, u32 countOff, s32 from) {
    u32 n = Rd(vp, countOff), list = To(vp, countOff + 4), i, def, target;

    for (i = 0; list != ~0u && i < n && i < 256 && InBlob(vp, list + i * 0xC, 0xC); i++) {
        if ((def = To(vp, list + i * 0xC + 8)) == ~0u || !InBlob(vp, def, 8)) {
            continue;
        }
        target = To(vp, def);
        if (target != ~0u && !(target & 3) && (seen[target / 4] & R_HEADER)) {
            Jw_BeginObject(w, NULL, 1);
            Jw_Int(w, "from", from);
            Jw_Int(w, "type", Base(vp)[def + 5]);
            Jw_Int(w, "target", target);
            Jw_EndObject(w);
        }
    }
}

void Viewer_WriteInterfaces(JsonWriter *w, VPack *vp) {
    static const char *const rootNames[] = { "tac", "pause", "hud", "pack", "end" };
    u32 words = vp->size / 4, i, j, cand;
    u8 *seen;

    {   /* the pack's pointer slots, as the scan wants them */
        PortIfaceScan scan;
        u32 *ptrs = SDL_malloc((words + 1) * sizeof(u32)), np = 0, count, *texRelocs;
        u8 *used;

        for (i = 0; ptrs != NULL && vp->slotKind != NULL && i < words; i++) {
            if (vp->slotKind[i] == VS_POINTER || vp->slotKind[i] == VS_REF) {
                ptrs[np++] = i * 4;
            }
        }
        seen = NULL;
        if (ptrs != NULL && np != 0 && Port_IfaceScanInit(&scan, Base(vp), vp->size, (u32)HND(Base(vp)), ptrs, np, NULL, 0)) {
            seen = Port_IfaceHeaders(&scan, &count);
            /* the pack's own textures its interfaces show (what a mixed build keeps) */
            used = SDL_calloc(vp->pack->numTextures + 1, 1);
            texRelocs = (u32 *)GETP(vp->pack->relocs);
            Jw_BeginArray(w, "interfaceTextures", 1);
            if (used != NULL && texRelocs != NULL) {
                Port_IfaceTextures(&scan, texRelocs, vp->pack->numRelocs, used, vp->pack->numTextures);
                for (i = 0; i < vp->pack->numTextures; i++) {
                    if (used[i]) {
                        Jw_Int(w, NULL, i);
                    }
                }
            }
            Jw_EndArray(w);
            SDL_free(used);
            Port_IfaceScanFree(&scan);
        }
        SDL_free(ptrs);
    }
    Jw_BeginArray(w, "interfaces", 0);
    if (seen != NULL) {
        MarkRoots(vp, seen);
    }
    for (cand = 0; seen != NULL && cand < words; cand++) {
        InterfaceHeader *hdr;
        u32 objs;

        if (!seen[cand]) {
            continue;
        }
        hdr = (InterfaceHeader *)(Base(vp) + cand * 4);
        objs = To(vp, cand * 4 + 0x10);
        Jw_BeginObject(w, NULL, 0);
        Jw_Int(w, "off", cand * 4);
        Jw_Int(w, "id", hdr->id);
        Jw_Int(w, "layer", hdr->layer);
        Jw_Int(w, "mode", hdr->mode);
        Jw_BeginArray(w, "roots", 1);
        for (i = 0; i < SDL_arraysize(rootNames); i++) {
            if (seen[cand] & (2 << i)) {
                Jw_String(w, NULL, rootNames[i]);
            }
        }
        Jw_EndArray(w);
        /* the slots that point at it, with the graph node they lie in (first 6) */
        Jw_BeginArray(w, "refs", 1);
        for (i = 0, j = 0; i < words && j < 6; i++) {
            if ((vp->slotKind[i] == VS_POINTER || vp->slotKind[i] == VS_REF) && To(vp, i * 4) == cand * 4) {
                s32 node = vp->owner != NULL ? vp->owner[i] : -1;

                Jw_BeginObject(w, NULL, 1);
                Jw_Int(w, "slot", i * 4);
                if (node >= 0 && (u32)node < vp->numNodes) {
                    Jw_String(w, "node", vp->nodes[node].id);
                }
                Jw_EndObject(w);
                j++;
            }
        }
        Jw_EndArray(w);
        Jw_BeginArray(w, "events", 0);
        WriteEvents(w, vp, seen, cand * 4 + 0x14, -1);
        for (j = 0; j < hdr->numObjects; j++) {
            u32 rec = To(vp, objs + j * DEF_SIZE + 0x44);

            if (rec != ~0u && InBlob(vp, rec, 0x18)) {
                WriteEvents(w, vp, seen, rec + 0x10, (s32)j);
            }
        }
        Jw_EndArray(w);
        Jw_BeginArray(w, "objects", 0);
        for (j = 0; j < hdr->numObjects; j++) {
            u32 d = objs + j * DEF_SIZE, flags = 0;
            IfaceObjectDef *def = (IfaceObjectDef *)(Base(vp) + d);
            f32 xy[2], sc[2], kr[2];

            LayoutXY(vp, objs, hdr->numObjects, j, 0, xy, &flags);
            Jw_BeginObject(w, NULL, 1);
            Jw_Int(w, "type", def->type);
            Jw_Int(w, "kind", KindOf(vp, d));
            Jw_Int(w, "role", def->role == 1 ? 0 : def->role);
            Jw_Int(w, "anchor", def->anchor);
            Jw_Float(w, "x", xy[0]);
            Jw_Float(w, "y", xy[1]);
            if (flags) {
                Jw_Int(w, "flags", flags);
            }
            if (KeyRange(vp, d, &kr[0], &kr[1]) && (kr[0] != kr[1] || kr[0] != xy[0])) {
                Jw_Float(w, "kxmin", kr[0]);
                Jw_Float(w, "kxmax", kr[1]);
            }
            if (FirstKey(vp, d, 0x10, sc)) {
                Jw_Float(w, "w", sc[0]);
                Jw_Float(w, "h", sc[1]);
            }
            if (def->type == 1 || def->type == 12) {
                WriteText(w, vp, d);
            }
            if (def->type == 6 || def->type == 7) {
                u32 slot = To(vp, d + 0x3C), child = slot != ~0u ? To(vp, slot) : ~0u;

                /* -1: the slot holds another pack's export (or nothing); -2: a
                 * target here that doesn't read as a header */
                Jw_Int(w, "child", child == ~0u ? -1 : (seen[child / 4] & R_HEADER) ? (long long)child : -2);
            }
            Jw_EndObject(w);
        }
        Jw_EndArray(w);
        Jw_EndObject(w);
    }
    Jw_EndArray(w);
    free(seen);   /* Port_IfaceHeaders: calloc */
}

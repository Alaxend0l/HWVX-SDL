/* A pack's interfaces, found in its blob (src/game/Port_IfaceScan.c). Port-only.
 *
 * A pack says nothing about which of its objects are interface headers, so
 * every target of a pointer slot is tried as one and kept when its fields fit
 * (objects, definitions, animations all where pointer slots say they are,
 * definition types the loader knows). Used by the viewer's "interfaces"
 * command (tools/hud_scan.py, the widescreen HUD check) and, for the
 * textures interfaces show, by mixed builds (Port_Mix.c). */
#ifndef PORT_IFACESCAN_H
#define PORT_IFACESCAN_H

#include "port.h"

typedef struct PortIfaceScan {
    const u8 *base;     /* the pack's blob */
    u32 size;
    u32 bias;           /* what a pointer slot holds for blob offset 0: HND(base) once the
                         * loader has relocated the slots (UpdatePointers), 0 before */
    u8 *isPtr;          /* per blob word: a pointer slot into this pack */
    const u32 *refs;    /* the { slot, id } reference relocations given to Init */
    u32 numRefs;
} PortIfaceScan;

/* pointers: the pack's pointer list (blob offsets of its pointer slots);
 * refs: its { slot, id } reference relocations (the loader's texRelocs), of
 * which those with id >> 16 == 0 are pointer slots too (may be NULL). 0: out
 * of memory. */
int Port_IfaceScanInit(PortIfaceScan *s, const u8 *base, u32 size, u32 bias, const u32 *pointers, u32 numPointers,
                       const u32 *refs, u32 numRefs);
void Port_IfaceScanFree(PortIfaceScan *s);

/* The blob offset a pointer slot at `off` points to, or ~0 (null, elsewhere). */
u32 Port_IfaceTo(const PortIfaceScan *s, u32 off);

/* Every interface header of the pack: header[off / 4] = 1 (a calloc'd array of
 * size / 4 + 1 bytes, the caller frees it), or NULL. *count: how many. */
u8 *Port_IfaceHeaders(const PortIfaceScan *s, u32 *count);

/* The pack's own textures its interfaces show: used[i] = PORT_IFACE_ART for
 * texture i (of numTex) that an interface object's image, texture key
 * animation or briefing portrait names, PORT_IFACE_FONT for a font's page
 * (a text's, a box's, a briefing character's) and nothing else, through the pack's texture relocations
 * (DataPack.relocs: { slot, id } pairs, id >> 16 != 0 another pack's;
 * PORT_TEXRELOC_U16 masked). Child interfaces are headers of their own and
 * counted as such. Geometry drawn in an interface (image type 1) is not
 * followed. Returns how many textures were marked. */
/* PORT_IFACE_GEOMTEX: a texture of geometry an interface draws (art, kept
 * apart: the yellow set leaves them to the "best" rule, as before). */
enum { PORT_IFACE_ART = 1, PORT_IFACE_FONT = 2, PORT_IFACE_GEOM = 3, PORT_IFACE_GEOMTEX = 4 };
u32 Port_IfaceTextures(const PortIfaceScan *s, const u32 *texRelocs, u32 numTexRelocs, u8 *used, u32 numTex);
/* Geometry an interface draws (an image of type 1: the HUD's 3D target
 * arrow) counts with its groups' textures as art. When it is another pack's
 * (a reference relocation), `other` gets what = PORT_IFACE_GEOM and the word
 * index of that pack's slot holding it: Port_IfaceGeometryTextures marks
 * them as that pack loads.
 * The same, and `other` for each texture of another pack (the pack it refers
 * to by id: DataPack.refs[k].id, `index` in its table) an interface shows:
 * the car packs' HUDs draw HW_Cars share_2's gauges, the worlds' HW_share's. */
u32 Port_IfaceTexturesAll(const PortIfaceScan *s, const u32 *texRelocs, u32 numTexRelocs, u8 *used, u32 numTex,
                          void (*other)(void *user, u32 packId, u32 index, u8 what), void *user);

/* The textures of the geometry the pointer slot at `slot` holds (its levels'
 * groups), as PORT_IFACE_GEOMTEX in used[] (where not art or a font already). Returns how many were marked. */
u32 Port_IfaceGeometryTextures(const PortIfaceScan *s, u32 slot, const u32 *texRelocs, u32 numTexRelocs, u8 *used,
                               u32 numTex);

#endif

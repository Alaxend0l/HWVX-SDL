/* Mixed builds: one game from several releases' data, built on the player's
 * machine (docs/xdl-format/README.md §9 and ultimate.md, HWVX-X-FILE-EDITOR).
 * Port-only.
 *
 * A recipe (JSONC; recipes/ultimate.jsonc is the built-in one) picks a base
 * release per archive, whose packs the game loads as they are, and per asset
 * id the release that supplies its texture pixels or sound samples. The
 * substitution runs as a pack loads from its source (Port_Mix.c), so the
 * pack cache (xdl.c) records the mixed pack and later loads read it back
 * without the other releases.
 *
 *   src/port/mix.c          the recipe, the releases' folders, files
 *   src/game/Port_Mix.c     packs: textures and sounds from another release,
 *                           the builder (every pack of every archive)
 *   src/game/Port_MixPairs.c  generated: which asset pairs with which
 *
 * hwvx --mix NAME|FILE         play the mixed build (packs convert as they load)
 * hwvx --mix-build NAME|FILE   convert every pack into the cache, then quit
 * hwvx --mix-scan NAME|FILE    only the builder's first pass: the share packs'
 *                              textures other packs' interfaces show, into
 *                              the cache's uirefs.txt (what --mix runs first
 *                              when it has no list)
 * (HWVX_MIX, HWVX_MIX_BUILD: the same from the environment.) */
#ifndef PORT_MIX_H
#define PORT_MIX_H

#include "port.h"

/* ---- mix.c ------------------------------------------------------------------ */

/* The releases assets come from: PORT_DATA_GC, _PC, _PS2, and the PS2
 * prototype (Port_Proto.c), whose assets only its import mode "replace"
 * takes: the pack's own release is the base, every asset the pairing table
 * pairs with the prototype comes from it (Port_MixProtoReplace). */
#define PORT_MIX_PROTO PORT_DATA_COUNT
#define PORT_MIX_RELS (PORT_DATA_COUNT + 1)

/* A recipe is loaded (Port_MixLoad). */
int Port_MixActive(void);
/* No recipe, and the prototype's import mode is "replace": packs load as
 * the release has them, with the prototype's paired textures and sounds. */
int Port_MixProtoReplace(void);
/* Loads the recipe `nameOrPath` ("ultimate", or a .jsonc file) and the
 * releases the player has (sources.cfg in the save folder; HWVX_MIX_GC,
 * HWVX_MIX_PC, HWVX_MIX_PS2 override). 0 with a message in err on failure. */
int Port_MixLoad(const char *nameOrPath, char *err, u32 errSize);
/* The recipe's name, and whether --mix-build asked for the whole cache. */
const char *Port_MixName(void);
int Port_MixBuilding(void);
/* The colours the build draws in: 1 the GameCube's, 0 the PC's (recipe "look"). */
int Port_MixLookGC(void);
/* The interface art's set (recipe "interface", for the built-in recipe the
 * launcher's choice): PORT_DATA_GC, the GameCube's yellow menus and HUD, or
 * PORT_DATA_PS2, the PS2's silver ones. The PC's art is yellow too but never
 * taken; its font pages are (the same glyphs at a higher resolution). */
int Port_MixInterface(void);
/* The title's press-start line (recipe "press_start", e.g. "Press START/ENTER": a mixed
 * game is played with a pad or the keyboard), or NULL: the release's own. Port_Mix.c puts it
 * in every press-start text (role 9) of the packs as they mix. */
const char *Port_MixPressStart(void);
/* The folder of release `rel` (PORT_DATA_*, PORT_MIX_PROTO), or NULL: not
 * imported, or not in the recipe. */
const char *Port_MixRoot(int rel);
/* The k-th release to look for the file `name` in (the game's GameCube name),
 * by the recipe's "base" rules; -1 past the last. */
int Port_MixCandidate(const char *name, int k);
/* The release of asset `id` ("base://HW_Cars/car_5/texture/3"): the
 * releases to try in order[], their count returned; or one of these
 * (FROM_IFACE: textures, the interface set's copy, Port_MixInterface). */
enum { PORT_MIX_FROM_BASE = 0, PORT_MIX_FROM_BEST = -1, PORT_MIX_FROM_IFACE = -2 };
int Port_MixAssetRule(const char *id, int order[PORT_MIX_RELS]);
/* Bytes of the pack file `name` as release `rel` has it; the count read. */
u32 Port_MixRead(int rel, const char *name, u64 offset, void *dst, u32 size);
/* What identifies the build's cache: its folder name under the cache
 * folder, and a stamp of the recipe and the releases (xdl.c). */
const char *Port_MixCacheName(void);
u32 Port_MixStamp(void);
/* One line of the build's manifest (<cache>/manifest.jsonl, written by
 * --mix-build): asset `id` came from release `rel`, index `index` there,
 * `hash` of the bytes taken. */
void Port_MixManifest(const char *id, int rel, u32 index, u64 hash);
/* The same for an interface texture (`ui` "art" or "font", a "ui" field):
 * written whichever release it comes from, the base's own included. */
void Port_MixManifestUI(const char *id, int rel, u32 index, u64 hash, const char *ui);
/* Textures of share packs that other packs' interfaces show (the car packs'
 * HUDs draw HW_Cars share_2's gauges): the builder's first pass finds them
 * (Port_Mix.c). A share pack is mixed before the packs that refer to it
 * load, so playing a mixed game needs the list before its first pack:
 * <cache>/uirefs.txt keeps it, stamped as the build (Port_MixStamp and the
 * cache version). */
typedef struct PortMixUiRef {
    char archive[0x10];     /* the share pack's file stem */
    u32 id;
    u16 index;              /* a texture's, or for PORT_IFACE_GEOM the word index of the slot holding a geometry */
    u8 what;                /* PORT_IFACE_ART, _FONT or _GEOM */
} PortMixUiRef;
/* Writes the list into the cache folder; 1 on success. */
int Port_MixUiRefsSave(const PortMixUiRef *refs, u32 count);
/* Playing a mixed game (not --mix-build): the list read from the cache,
 * else made by `program --mix-scan recipe` (a process of its own: the pass
 * loads every pack, which the game must not keep) and read then; given to
 * PortMix_AddUiRef. Cached share archives made without a list (an older
 * unbuilt game's) are removed, to be mixed again. 0 when there is no list:
 * those textures follow the "best" rule. */
int Port_MixUiRefsLoad(const char *program, const char *recipe);
/* The archives of every release in the recipe, as the game names them
 * (no extension, '\\' separators), each once: `fn` per name. */
void Port_MixArchives(void (*fn)(const char *name, void *user), void *user);

/* ---- gx_tex.c --------------------------------------------------------------- */

/* One mip level of a GX (or port, GX_TF_PORT | n) texture format as RGBA8. */
void GX_PortDecodeLevel(int gxFormat, const u8 *src, int w, int h, u8 *rgba);
u32 GX_PortLevelSize(int gxFormat, int w, int h);

/* ---- Port_Mix.c -------------------------------------------------------------- */

struct DataPack;
/* DataPack_LoadFile: around a pack's load (packs load inside packs). */
void PortMix_BeginPack(const char *name, u32 type, u32 id);
void PortMix_EndPack(void);
void PortMix_LogStats(void);
/* DataPack_LoadDataPack, once the pack is relocated and converted: its
 * textures (headers at `texOff` of `base`) from the releases the recipe
 * names. The block the new pixels are in is kept with the pack. The pack's
 * pointer list and its { slot, id } reference relocations find what its
 * interfaces show (Port_IfaceScan.h): those are the interface set's. */
void PortMix_Textures(struct DataPack *pack, s32 fmt, u8 *base, u32 dataSize, u32 texOff, u32 numTex,
                      const u32 *pointers, u32 numPointers, const u32 *refs, u32 numRefs);
/* The pack's sound effects (descriptors converted, relocated): samples from
 * another release, in a block PortMix_SFXDone frees once CreateSFX has
 * copied them. PortMix_SFXPrepare runs before the base's own conversion,
 * which asks PortMix_SFXLength/Fill for a PS2 pack's (Port_PS2.c). */
void PortMix_SFXPrepare(s32 fmt, const u8 *desc, u32 count);
/* A mod's sound over sound `index` (Port_Mods.c): pcm is PCM16 in host order
 * (malloc'd, taken), length in bytes, loop in bytes or 0xFFFFFFFF. */
void PortMix_SFXSet(u32 index, u32 count, u8 *pcm, u32 length, u32 loop, u32 rate);
u32 PortMix_SFXLength(u32 index);
void PortMix_SFXFill(u32 index, void *desc, u8 *out);
void PortMix_SFX(s32 fmt, u8 *desc, u32 count);
void PortMix_SFXDone(void);
/* --mix-build: load every pack of every archive, so the cache holds them;
 * returns the packs converted, *failed those that did not load. Before each
 * pack it prints "@convert DONE TOTAL ARCHIVE" (Port_Convert.h's progress;
 * a first pass over the TOCs counts TOTAL). */
u32 PortMix_BuildAll(u32 *failed);
/* --mix-scan: the first pass alone, its list saved (Port_MixUiRefsSave); 1 on success. */
int PortMix_ScanUi(void);
/* One entry of the list (Port_MixUiRefsLoad). */
void PortMix_AddUiRef(const char *archive, u32 id, u32 index, u8 what);

typedef struct PortMixPairPack {
    const char *archive;    /* file stem, matched without case */
    u8 type;
    u32 id;
    u32 first;              /* in g_portMixPairs: numTextures, then numSounds */
    u16 numTextures, numSounds;
} PortMixPairPack;
extern const u16 g_portMixPairs[][PORT_MIX_RELS];   /* index in the GameCube's, PC's, PS2's, prototype's
                                                       * table; 0xFFFF: none */
extern const PortMixPairPack g_portMixPairPacks[];
extern const u32 g_portMixNumPairPacks;

#endif

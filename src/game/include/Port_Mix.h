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
 * (HWVX_MIX, HWVX_MIX_BUILD: the same from the environment.) */
#ifndef PORT_MIX_H
#define PORT_MIX_H

#include "port.h"

/* ---- mix.c ------------------------------------------------------------------ */

/* A recipe is loaded (Port_MixLoad). */
int Port_MixActive(void);
/* Loads the recipe `nameOrPath` ("ultimate", or a .jsonc file) and the
 * releases the player has (sources.cfg in the save folder; HWVX_MIX_GC,
 * HWVX_MIX_PC, HWVX_MIX_PS2 override). 0 with a message in err on failure. */
int Port_MixLoad(const char *nameOrPath, char *err, u32 errSize);
/* The recipe's name, and whether --mix-build asked for the whole cache. */
const char *Port_MixName(void);
int Port_MixBuilding(void);
/* The colours the build draws in: 1 the GameCube's, 0 the PC's (recipe "look"). */
int Port_MixLookGC(void);
/* The folder of release `rel` (PORT_DATA_*), or NULL: not imported, or not
 * in the recipe. */
const char *Port_MixRoot(int rel);
/* The k-th release to look for the file `name` in (the game's GameCube name),
 * by the recipe's "base" rules; -1 past the last. */
int Port_MixCandidate(const char *name, int k);
/* The release of asset `id` ("base://HW_Cars/car_5/texture/3"): the
 * releases to try in order[], their count returned; or one of these. */
enum { PORT_MIX_FROM_BASE = 0, PORT_MIX_FROM_BEST = -1 };
int Port_MixAssetRule(const char *id, int order[PORT_DATA_COUNT]);
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
 * names. The block the new pixels are in is kept with the pack. */
void PortMix_Textures(struct DataPack *pack, s32 fmt, u8 *base, u32 dataSize, u32 texOff, u32 numTex);
/* The pack's sound effects (descriptors converted, relocated): samples from
 * another release, in a block PortMix_SFXDone frees once CreateSFX has
 * copied them. PortMix_SFXPrepare runs before the base's own conversion,
 * which asks PortMix_SFXLength/Fill for a PS2 pack's (Port_PS2.c). */
void PortMix_SFXPrepare(s32 fmt, const u8 *desc, u32 count);
u32 PortMix_SFXLength(u32 index);
void PortMix_SFXFill(u32 index, void *desc, u8 *out);
void PortMix_SFX(s32 fmt, u8 *desc, u32 count);
void PortMix_SFXDone(void);
/* --mix-build: load every pack of every archive, so the cache holds them;
 * returns the packs converted. */
u32 PortMix_BuildAll(void);

typedef struct PortMixPairPack {
    const char *archive;    /* file stem, matched without case */
    u8 type;
    u32 id;
    u32 first;              /* in g_portMixPairs: numTextures, then numSounds */
    u16 numTextures, numSounds;
} PortMixPairPack;
extern const u16 g_portMixPairs[][3];   /* index in the GameCube's, PC's, PS2's table; 0xFFFF: none */
extern const PortMixPairPack g_portMixPairPacks[];
extern const u32 g_portMixNumPairPacks;

#endif

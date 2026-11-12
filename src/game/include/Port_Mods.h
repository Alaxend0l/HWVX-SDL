/* Mods (docs/xdl-format/README.md §8, §10.3): folders of changes over the
 * player's own game data, never holding any of it. Port-only.
 *
 * A mod is a folder in the mods folder (HWVX_MODS_DIR, else mods/ in the
 * preferences folder) with a mod.jsonc (schemas/Mod.schema.json): new cars
 * and items and tracks that extend base ones, field patches on base objects, colour
 * recipes on base textures. tools/xdl_mod.py packs one into a .hwmod after
 * checking that nothing in it is game data; Port_ModsInstall unpacks one.
 *
 * Load order: mods.jsonc in the mods folder ("order": ids, "disabled":
 * ids), then mods it doesn't list by id; a mod's "after" moves it behind
 * the mods it names. Two enabled mods setting the same field, recolouring
 * the same texture or bringing the same entry id are reported as a conflict
 * (the report, and the log); the later one in the order is the one applied.
 * HWVX_MODS=0 turns every mod off.
 *
 * What the launcher needs is the first group: scan, list, enable, reorder,
 * save, install, remove, and the report. Changes take effect when the game
 * next starts (the game reads the list once, in Port_ContentInit). */
#ifndef PORT_MODS_H
#define PORT_MODS_H

#include <stddef.h>

typedef struct PortModInfo {
    const char *id;             /* uid://... */
    const char *name;
    const char *version;
    const char *author;         /* "" when the mod gives none */
    const char *description;
    const char *folder;         /* its folder's name in the mods folder */
    int enabled;
    int checked;                /* its folder has the packer's check.json (installed from a
                                 * .hwmod); 0: a folder put there by hand, unchecked */
    int problems;               /* report lines about it */
} PortModInfo;

/* ---- for the launcher ---------------------------------------------------- */

/* The mods folder, with its trailing slash; SDL_free it. NULL without one. */
char *Port_ModsDir(void);
/* (Re)reads the mods folder and mods.jsonc: how many mods there are. */
int Port_ModsScan(void);
int Port_ModsCount(void);
/* Mod i in load order (0 loads first), or NULL. Valid until the next scan. */
const PortModInfo *Port_ModsInfo(int i);
/* Enable or disable mod i; move it to position `to` in the order. Both
 * rebuild the report (conflicts change); neither writes anything. */
void Port_ModsSetEnabled(int i, int enabled);
void Port_ModsMove(int i, int to);
/* Writes the order and the disabled mods to mods.jsonc: 1 if written. */
int Port_ModsSave(void);
/* Unpacks a .hwmod into the mods folder (replacing the mod of the same id)
 * and rescans. Only packages the packer checked (a check.json in them), with
 * stored entries, are taken. 1, or 0 with why in err. */
int Port_ModsInstall(const char *package, char *err, size_t errSize);
/* Deletes mod i's folder and rescans: 1 if it's gone. */
int Port_ModsRemove(int i);
/* What's wrong or worth knowing: conflicts between mods, parts that can't
 * be applied, unchecked mods. Lines for a list; valid until the next scan,
 * enable or move. More are added as the game loads packs (a patch whose
 * object a release lacks). */
int Port_ModsReportCount(void);
const char *Port_ModsReportLine(int i);
/* 1 if any enabled mod changes the game (for places that need the retail
 * game: online play). */
int Port_ModsActive(void);

/* ---- the game's side ------------------------------------------------------- */

/* Port_ContentInit: scans, and gets the enabled mods' patches and recipes
 * ready. */
void Port_ModsInit(void);
/* Before Port_ContentLoadLock: the enabled mods' cars and items into the
 * ids.lock at `path` (added without a number: the registry gives one and
 * keeps it; "mod": the mod's id), and entries of mods that are gone or off
 * marked "active": false, so their numbers and saves wait for them. The file
 * is left alone when no mod has an entry in it or for it. */
void Port_ModsSyncLock(const char *path);
/* A pack's pointers are fixed up (FileStreaming_InitializeDataPack): the
 * patches and recipes for it are applied, in load order. */
void Port_ModsPackLoaded(void *dataPack, const char *name, unsigned type, unsigned id);
/* DataPack_ReleaseDataPack: its textures' recipes go. */
void Port_ModsPackReleased(void *dataPack);
/* A registered track's level file (its mod's "tracks" entry's "level", a
 * path in the mod's folder; Port_Level.h), or NULL; the caller frees it. */
char *Port_ModsTrackLevel(const char *trackId);
/* A mod's own file opened by name, "@mod\\<id>\\<path>" (a track's "worldPack", built on this
 * machine): its host path (freed by the caller) and *format (PORT_DATA_*), or NULL. */
char *Port_ModsResolveFile(const char *name, int *format);

#endif

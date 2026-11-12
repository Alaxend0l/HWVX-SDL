/* Levels of one's own over a base world (docs-pending/mission-editor.md):
 * a registered track's records (triggers, volumes, timers, variables,
 * strings) and field patches, compiled by tools/xdl_level.py from a source
 * project into a level file (JSON, the project's own data only: links name
 * the base world's records by path, never by bytes), applied to the world
 * pack as the level loads. Port-only file. */
#ifndef PORT_LEVEL_H
#define PORT_LEVEL_H

#include <stddef.h>

/* LoadWorldProcess, before the world pack's pointers are refreshed (its
 * triggers activated) of the MCP's world path `world`: the level of
 * registered track `track` (-1: a base level) applies its file, if its mod gives one (Port_ModsTrackLevel), or
 * HWVX_LEVEL=<file> for any level (tests, the editor's test drive). */
void Port_LevelWorldLoaded(void *worldPack, const char *world, int track);
/* Applies a level file to a loaded world pack whose triggers aren't active
 * yet (the viewer: a world on show). 1 on success; else 0 and why in err.
 * What an earlier file put in goes first. */
int Port_LevelApply(void *worldPack, const char *path, char *err, size_t errSize);
/* Takes out what a level put into `worldPack` (its pack is going), restoring
 * the base world's tables and patched fields. Any pack: does nothing for
 * the others. */
void Port_LevelPackReleased(void *dataPack);
/* The level applied now: its id and file (NULL: none). */
const char *Port_LevelCurrent(const char **path);

#endif

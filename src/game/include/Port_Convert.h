/* Converting a release to the .xdl cache ahead of time (Port_Convert.c).
 * Port-only. */
#ifndef PORT_CONVERT_H
#define PORT_CONVERT_H

/* The program was started to convert (hwvx --convert-xdl): Main_Init stops
 * before the first game state, and no state ever runs. */
void Port_ConvertBegin(void);
int Port_Converting(void);
/* Loads every pack of every pack file under `dataDir` as the game would,
 * with the cache (xdl.c) recording into `outDir`, then writes the manifest
 * there. `source`: the place as the player gave it, for the manifest.
 * Progress goes to stdout, a line a pack: "@convert DONE TOTAL NAME".
 * Returns 0, or 1 after an "@error TEXT" line. */
int Port_ConvertRun(const char *dataDir, const char *source, const char *outDir);

/* The manifest a finished conversion leaves in its folder: what was
 * converted and from which files, so the launcher can tell a cache that is
 * out of date without opening every file of it. */
#define PORT_CONVERT_MANIFEST "converted.txt"

#endif

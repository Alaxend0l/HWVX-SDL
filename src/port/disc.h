/* A GameCube disc image as the game data: a plain image (.iso/.gcm) instead
 * of the extracted files. The disc's file system table maps paths to byte
 * ranges of the image; each file opens as an SDL_IOStream over its range, so
 * dvd.c and thp.c read it like a file on disk. One image is open at a time;
 * reads are serialised, so streams can be used from any thread. Compressed
 * images (.gcz, .rvz, ...) aren't read: Dolphin converts them to .iso. */
#ifndef DISC_H
#define DISC_H

#include <SDL3/SDL.h>

/* Opens the image at path and makes it the one Disc_OpenFile reads, closing
 * any other. 0 (and an SDL error) when it isn't a GameCube disc image this
 * reads, or a damaged one. */
int Disc_Open(const char *path);
void Disc_Close(void);
int Disc_IsOpen(void);

/* Whether path names a disc image by its extension: .iso, .gcm, or .gcz,
 * which Disc_Open refuses with a message saying how to convert it. */
int Disc_IsImageName(const char *path);

/* A file on the open disc, by its path from the root ("audio_/HWB_M01.xgc",
 * a leading '/' is fine), ignoring case like the GameCube. NULL if there is
 * no such file. */
SDL_IOStream *Disc_OpenFile(const char *name);

/* The six-character game ID (GHWE78 for the US release), or "". */
const char *Disc_GameID(void);

#endif

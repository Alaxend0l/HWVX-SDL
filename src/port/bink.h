/* Bink movies through FFmpeg loaded at run time (bink.c). */
#ifndef BINK_H
#define BINK_H

#include <SDL3/SDL.h>
#include "port.h"

typedef struct BinkMovie BinkMovie;
/* interleaved float samples, `frames` of them per channel */
typedef void (*BinkAudioFn)(void *user, const float *pcm, int frames);

/* Loads FFmpeg on first use (logs once either way); 1 if it can be used. */
int Bink_Load(void);
/* The FFmpeg release loaded (6-9, from libavformat's major), 0 if none is
 * (or Bink_Load hasn't run): for the launcher's Credits page. */
int Bink_Version(void);
/* Opens a movie read from `io`, which it takes over (closed on failure too);
 * `name` is for messages. NULL if FFmpeg is missing or can't play it. */
BinkMovie *Bink_Open(SDL_IOStream *io, const char *name);
/* frames: 0 if the file doesn't say; channels/rate: 0 without audio */
void Bink_Info(const BinkMovie *m, int *width, int *height, double *fps, int *frames, int *channels, int *rate);
/* The first video frame's time in the file, in seconds (to place a
 * separately read audio track: pss.c). */
double Bink_StartTime(const BinkMovie *m);
/* Where decoded audio goes, as packets are read (a few frames ahead of the picture). */
void Bink_SetAudio(BinkMovie *m, BinkAudioFn fn, void *user);
/* Decodes the next video frame into rgba (width*height*4, top row first;
 * NULL: decode without converting, for frames skipped). 0 at the end. */
int Bink_NextFrame(BinkMovie *m, u8 *rgba);
void Bink_Close(BinkMovie *m);

#endif

/* The PS2 movies' (.PSS) own audio track: Sony's SShd/SSbd PCM (pss.c). */
#ifndef PSS_H
#define PSS_H

#include <SDL3/SDL.h>
#include "port.h"

typedef struct PssAudio PssAudio;

/* Reads the audio headers from `io`, which it takes over (closed on
 * failure too). NULL if the file has no PCM track it can play. */
PssAudio *Pss_OpenAudio(SDL_IOStream *io);
/* start: the first audio PTS in seconds (-1 if none); frames: in the track */
void Pss_AudioInfo(const PssAudio *a, int *channels, int *rate, double *start, u32 *frames);
/* Up to maxFrames frames of interleaved s16, whole blocks at a time
 * (Pss_AudioBlockFrames each); 0 at the end. */
int Pss_ReadAudio(PssAudio *a, s16 *out, int maxFrames);
int Pss_AudioBlockFrames(const PssAudio *a);
void Pss_CloseAudio(PssAudio *a);

#endif

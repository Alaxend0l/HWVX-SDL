/* Compressed audio for mods: WAV, FLAC, MP3 and Ogg Vorbis to PCM16.
 *
 * The decoders are vendored single files (third_party/): miniaudio's dr_wav,
 * dr_flac and dr_mp3 (public domain / MIT-0) and stb_vorbis (public domain /
 * MIT). Used by the port for a mod's sounds (Port_Mods.c) and by the helper
 * hwvx-audiodec (main.c) that tools/xdl_media.py and tools/xdl_mod.py run.
 *
 * Loop points come from the file: a WAV's `smpl` chunk, Vorbis comments
 * (Ogg, FLAC: LOOPSTART with LOOPLENGTH or LOOPEND) or an MP3's ID3v2 TXXX
 * frames of the same names, all in the file's own samples. */
#ifndef HWVX_AUDIODEC_H
#define HWVX_AUDIODEC_H

#include <stddef.h>
#include <stdint.h>

#define AUDIODEC_MAX_FRAMES (48000u * 60u * 10u)   /* ten minutes at 48 kHz */

typedef struct AudioDecoded {
    int16_t *pcm;           /* interleaved, `channels` per frame (malloc) */
    uint32_t frames;
    uint32_t channels;
    uint32_t rate;
    int64_t loopStart;      /* in frames; -1: no loop */
    int64_t loopEnd;        /* one past the last looped frame; -1: the end */
    const char *format;     /* "wav", "flac", "mp3", "ogg" */
} AudioDecoded;

/* 1 on success; 0 with a message in err. */
int AudioDec_Memory(const void *data, size_t size, AudioDecoded *out, char *err, size_t errSize);
int AudioDec_File(const char *path, AudioDecoded *out, char *err, size_t errSize);
void AudioDec_Free(AudioDecoded *a);

/* The channels' mean, as a new buffer of `frames` samples (malloc). */
int16_t *AudioDec_Mono(const AudioDecoded *a);

/* Windowed-sinc resampling of mono PCM16 (malloc'd result, *outN samples).
 * The same filter as tools/xdl_media.py's resample(). */
int16_t *AudioDec_Resample(const int16_t *in, uint32_t n, uint32_t srcRate, uint32_t dstRate, uint32_t *outN);

/* A position in source frames at another rate. */
int64_t AudioDec_ScalePos(int64_t pos, uint32_t srcRate, uint32_t dstRate);

#endif

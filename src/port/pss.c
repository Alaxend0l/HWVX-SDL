/* The audio of the PS2's movies (.PSS), which FFmpeg doesn't see.
 *
 * A .PSS is an MPEG-2 program stream: packs of about 4 KB, video in PES
 * stream 0xE0 (MPEG-2, decoded by FFmpeg: bink.c), audio in private stream 1
 * (0xBD). Each 0xBD payload starts with one more 0xFF than its PES header
 * counts (which is why FFmpeg's demuxer, reading that as the sub-stream id,
 * passes the track over), then the sub-stream id 0xA0 and two zero bytes.
 * The first payload then holds Sony's headers:
 *
 *   "SShd" u32 size (0x18), then s32 type (1: 16-bit PCM, 0: SPU ADPCM),
 *          s32 rate, s32 channels, s32 interleave, 8 bytes of 0xFF
 *   "SSbd" u32 size: the bytes of audio data that follow, over all packets
 *
 * (all little-endian). The data is one block of `interleave` bytes per
 * channel in turn: 0x200 bytes of left, 0x200 of right, and so on. Each 0xBD
 * packet carries a PTS; the first one, against the video's first, places
 * the audio in time. Only PCM is played (every retail and prototype file
 * checked is PCM). The file is read through its own SDL_IOStream, alongside
 * FFmpeg's, a packet at a time as the movie needs it. */
#include "port_internal.h"
#include "pss.h"

#define MAX_INTERLEAVE 0x4000

struct PssAudio {
    SDL_IOStream *io;
    int channels, rate, interleave;
    u32 dataLeft;       /* bytes of audio data still to come (SSbd's size) */
    double start;       /* the first audio PTS, in seconds (-1: none) */
    u8 *pend;           /* data bytes not yet de-interleaved */
    size_t pendLen, pendCap;
    u8 pkt[65536];
    int eof;
};

static u32 Le32(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }

/* The payload of the next 0xBD PES packet, after its PES header and the 4
 * sub-stream bytes: its length (0 if empty), or -1 at the end of the file.
 * *pts: in seconds, or -1 without one. */
static int NextAudioPayload(PssAudio *a, const u8 **data, double *pts) {
    u8 h[16];

    *pts = -1.0;
    for (;;) {
        u32 len, hl;
        u8 id;

        if (SDL_ReadIO(a->io, h, 4) != 4) {
            return -1;
        }
        while (h[0] != 0 || h[1] != 0 || h[2] != 1) {   /* lost: find the next start code */
            h[0] = h[1];
            h[1] = h[2];
            h[2] = h[3];
            if (SDL_ReadIO(a->io, h + 3, 1) != 1) {
                return -1;
            }
        }
        id = h[3];
        if (id == 0xB9) {   /* end of the program stream */
            return -1;
        }
        if (id == 0xBA) {   /* pack header: MPEG-2 (10 more bytes, then stuffing) or MPEG-1 (8) */
            if (SDL_ReadIO(a->io, h, 8) != 8) {
                return -1;
            }
            if ((h[0] & 0xC0) == 0x40 &&
                (SDL_ReadIO(a->io, h + 8, 2) != 2 || SDL_SeekIO(a->io, h[9] & 7, SDL_IO_SEEK_CUR) < 0)) {
                return -1;
            }
            continue;
        }
        if (id < 0xBB) {
            continue;   /* not a packet start: look again */
        }
        if (SDL_ReadIO(a->io, h, 2) != 2) {
            return -1;
        }
        len = (u32)h[0] << 8 | h[1];
        if (id != 0xBD) {
            if (SDL_SeekIO(a->io, len, SDL_IO_SEEK_CUR) < 0) {
                return -1;
            }
            continue;
        }
        if (SDL_ReadIO(a->io, a->pkt, len) != len) {
            return -1;
        }
        /* MPEG-2 PES header: '10' flags, PTS/DTS flags, header length */
        if (len < 3 || (a->pkt[0] & 0xC0) != 0x80 || 3 + (hl = a->pkt[2]) > len) {
            continue;
        }
        if ((a->pkt[1] & 0x80) && hl >= 5) {
            const u8 *p = a->pkt + 3;
            u64 v = (u64)((p[0] >> 1) & 7) << 30 | (u64)(p[1] << 7 | p[2] >> 1) << 15 | (u64)(p[3] << 7 | p[4] >> 1);
            *pts = (double)v / 90000.0;
        }
        hl += 3;
        while (hl < len && a->pkt[hl] == 0xFF) {   /* the stuffing byte past the header */
            hl++;
        }
        if (hl + 3 > len || (a->pkt[hl] & 0xF0) != 0xA0) {
            continue;   /* some other private sub-stream */
        }
        *data = a->pkt + hl + 3;
        return (int)(len - hl - 3);
    }
}


static void Keep(PssAudio *a, const u8 *d, size_t n) {
    if (n > a->dataLeft) {
        n = a->dataLeft;
    }
    if (a->pendLen + n > a->pendCap) {
        a->pendCap = (a->pendLen + n) * 2;
        a->pend = SDL_realloc(a->pend, a->pendCap);
    }
    memcpy(a->pend + a->pendLen, d, n);
    a->pendLen += n;
    a->dataLeft -= (u32)n;
}

PssAudio *Pss_OpenAudio(SDL_IOStream *io) {
    PssAudio *a = SDL_calloc(1, sizeof(*a));
    const u8 *d;
    double pts;
    int n, type;
    u32 hs;

    a->io = io;
    /* the headers open the first audio packet, within the first few packs */
    n = NextAudioPayload(a, &d, &pts);
    a->start = pts;
    if (n < 16 || memcmp(d, "SShd", 4) != 0 || (hs = Le32(d + 4)) < 16 || 8 + hs + 8 > (u32)n ||
        memcmp(d + 8 + hs, "SSbd", 4) != 0) {
        Pss_CloseAudio(a);
        return NULL;
    }
    type = (int)Le32(d + 8);
    a->rate = (int)Le32(d + 12);
    a->channels = (int)Le32(d + 16);
    a->interleave = (int)Le32(d + 20);
    a->dataLeft = Le32(d + 8 + hs + 4);
    if (type != 1 || a->rate < 8000 || a->rate > 96000 || a->channels < 1 || a->channels > 2 ||
        a->interleave < 2 || a->interleave > MAX_INTERLEAVE || (a->interleave & 1)) {
        Port_Log("pss: audio type %d, %d Hz, %d ch, interleave 0x%x not played", type, a->rate, a->channels,
                 a->interleave);
        Pss_CloseAudio(a);
        return NULL;
    }
    Keep(a, d + 8 + hs + 8, (size_t)n - (8 + hs + 8));
    return a;
}

void Pss_AudioInfo(const PssAudio *a, int *channels, int *rate, double *start, u32 *frames) {
    *channels = a->channels;
    *rate = a->rate;
    *start = a->start;
    *frames = (u32)(((u64)a->dataLeft + a->pendLen) / 2 / a->channels);
}

int Pss_ReadAudio(PssAudio *a, s16 *out, int maxFrames) {
    int block = a->interleave * a->channels, per = a->interleave / 2, done = 0;
    size_t used = 0;

    while (done + per <= maxFrames) {
        const u8 *b;
        int i, c;

        if (a->pendLen - used < (size_t)block) {
            const u8 *d;
            double pts;
            int n;

            memmove(a->pend, a->pend + used, a->pendLen - used);
            a->pendLen -= used;
            used = 0;
            if (a->dataLeft == 0 || a->eof || (n = NextAudioPayload(a, &d, &pts)) < 0) {
                a->eof = 1;
                break;
            }
            Keep(a, d, (size_t)n);
            continue;
        }
        b = a->pend + used;
        for (i = 0; i < per; i++) {
            for (c = 0; c < a->channels; c++) {
                const u8 *s = b + (size_t)c * a->interleave + i * 2;
                out[(size_t)(done + i) * a->channels + c] = (s16)(s[0] | s[1] << 8);
            }
        }
        used += block;
        done += per;
    }
    if (used) {
        memmove(a->pend, a->pend + used, a->pendLen - used);
        a->pendLen -= used;
    }
    return done;
}

int Pss_AudioBlockFrames(const PssAudio *a) { return a->interleave / 2; }

void Pss_CloseAudio(PssAudio *a) {
    if (a == NULL) {
        return;
    }
    if (a->io != NULL) {
        SDL_CloseIO(a->io);
    }
    SDL_free(a->pend);
    SDL_free(a);
}

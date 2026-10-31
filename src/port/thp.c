/* THP movie playback behind the game's Mpeg_* interface (State_Movie.c).
 *
 * THP files (fmv_/...): a 0x30-byte header (frame rate, frame count, first
 * frame size, offsets), component info (video WxH, audio channels, rate),
 * then a chain of frames. Each frame: next frame size, previous frame size,
 * one size per component, then the component data: a JPEG (thp_jpeg.c) and
 * a block of GameCube DSP ADPCM audio.
 *
 * Video follows the wall clock at the file's frame rate; Mpeg_Render draws
 * the current frame into the EFB and presents it, since nothing else renders
 * while the movie state runs. Audio goes to its own SDL audio stream, a few
 * frames ahead of the picture. */
#include "port_internal.h"

SDL_IOStream *Port_OpenDiscFile(const char *name);
int THPJpeg_Decode(const u8 *data, u32 size, u8 *rgba, int width, int height);
void GX_PortDrawImage(const u8 *rgba, int width, int height);

#define AUDIO_AHEAD 4   /* frames of audio queued ahead of the picture */

typedef struct Movie {
    SDL_IOStream *io;
    SDL_AudioStream *audio;
    f32 fps;
    u32 numFrames;
    u32 numComps;
    u8 compTypes[16];
    int width, height;
    int channels;
    u32 rate;
    u8 *frame;        /* the frame being read */
    u32 frameCap;
    u64 nextOffset;   /* file offset of the next frame to read */
    u32 nextSize;
    u32 framesRead;
    u32 shown;        /* index of the frame on screen + 1 (0: none yet) */
    u8 *video[AUDIO_AHEAD + 1];   /* JPEG data of frames read ahead */
    u32 videoSize[AUDIO_AHEAD + 1];
    u8 *rgba;
    u64 startNS;
    s16 *pcm;
    u32 pcmCap;
} Movie;

static Movie *s_movie;

static u32 Be32(const u8 *p) { return Port_ReadBE32(p); }

static void Movie_Free(Movie *m) {
    u32 i;

    if (m == NULL) {
        return;
    }
    if (m->audio != NULL) {
        SDL_DestroyAudioStream(m->audio);
    }
    if (m->io != NULL) {
        SDL_CloseIO(m->io);
    }
    for (i = 0; i <= AUDIO_AHEAD; i++) {
        SDL_free(m->video[i]);
    }
    SDL_free(m->frame);
    SDL_free(m->rgba);
    SDL_free(m->pcm);
    SDL_free(m);
}

/* DSP ADPCM: 8-byte frames of a header byte (predictor << 4 | scale shift)
 * and 14 4-bit samples. coefs: 8 pairs; hist: the two previous samples. */
static void DecodeADPCM(const u8 *src, u32 numSamples, const s16 *coefs, s16 *hist, s16 *out, int stride) {
    u32 i = 0;

    while (i < numSamples) {
        u8 header = *src++;
        int scale = 1 << (header & 0xF), pred = (header >> 4) & 7, k;
        s32 c1 = coefs[pred * 2], c2 = coefs[pred * 2 + 1];

        for (k = 0; k < 14 && i < numSamples; k++, i++) {
            int nib = (k & 1) ? (src[k >> 1] & 0xF) : (src[k >> 1] >> 4);
            s32 s;
            if (nib >= 8) {
                nib -= 16;
            }
            s = ((nib * scale) << 11) + 1024 + c1 * hist[0] + c2 * hist[1];
            s >>= 11;
            s = s < -32768 ? -32768 : s > 32767 ? 32767 : s;
            hist[1] = hist[0];
            hist[0] = (s16)s;
            out[(size_t)i * stride] = (s16)s;
        }
        src += 7;
    }
}

/* Audio component: u32 channel size, u32 samples, s16 coefs[16] per channel,
 * s16 history[2] per channel, then each channel's ADPCM data. */
static void QueueAudio(Movie *m, const u8 *a, u32 size) {
    u32 chSize, n;
    s16 coefs[2][16], hist[2][2];
    int ch, k;
    const u8 *p;

    if (m->audio == NULL || size < 8) {
        return;
    }
    chSize = Be32(a);
    n = Be32(a + 4);
    p = a + 8;
    for (ch = 0; ch < m->channels; ch++) {
        for (k = 0; k < 16; k++, p += 2) {
            coefs[ch][k] = (s16)Port_ReadBE16(p);
        }
    }
    for (ch = 0; ch < m->channels; ch++) {
        hist[ch][0] = (s16)Port_ReadBE16(p);
        hist[ch][1] = (s16)Port_ReadBE16(p + 2);
        p += 4;
    }
    if ((size_t)(p - a) + (size_t)chSize * m->channels > size) {
        return;
    }
    if (n * m->channels > m->pcmCap) {
        m->pcmCap = n * m->channels;
        m->pcm = SDL_realloc(m->pcm, m->pcmCap * sizeof(s16));
    }
    for (ch = 0; ch < m->channels; ch++) {
        DecodeADPCM(p + (size_t)ch * chSize, n, coefs[ch], hist[ch], m->pcm + ch, m->channels);
    }
    SDL_PutAudioStreamData(m->audio, m->pcm, (int)(n * m->channels * sizeof(s16)));
}

/* Reads the next frame: queues its audio, keeps its JPEG for display. */
static int ReadFrame(Movie *m) {
    u32 size = m->nextSize, i, off;
    u8 *f;

    if (m->framesRead >= m->numFrames || size < 8 + 4 * m->numComps) {
        return 0;
    }
    if (size > m->frameCap) {
        m->frameCap = size;
        m->frame = SDL_realloc(m->frame, size);
    }
    f = m->frame;
    if (SDL_SeekIO(m->io, (Sint64)m->nextOffset, SDL_IO_SEEK_SET) < 0 || SDL_ReadIO(m->io, f, size) != size) {
        return 0;
    }
    m->nextOffset += size;
    m->nextSize = Be32(f);
    off = 8 + 4 * m->numComps;
    for (i = 0; i < m->numComps; i++) {
        u32 cs = Be32(f + 8 + i * 4);
        if (off + cs > size) {
            break;
        }
        if (m->compTypes[i] == 0) {
            u32 slot = m->framesRead % (AUDIO_AHEAD + 1);
            m->video[slot] = SDL_realloc(m->video[slot], cs);
            memcpy(m->video[slot], f + off, cs);
            m->videoSize[slot] = cs;
        } else if (m->compTypes[i] == 1) {
            QueueAudio(m, f + off, cs);
        }
        off += cs;
    }
    m->framesRead++;
    return 1;
}

s32 Mpeg_Create(void) { return 1; }

s32 Mpeg_Destroy(void) {
    Movie_Free(s_movie);
    s_movie = NULL;
    return 1;
}

s32 Mpeg_StopMovie(void) {
    return Mpeg_Destroy();
}

/* name is a disc path like "FMV_\bglogo"; volume 0-100 (music volume). */
s32 Mpeg_StartMovie(const char *name, s32 volume, double volumeScale) {
    u8 hdr[0x30], comp[0x80];
    char path[256];
    Movie *m;
    u32 compOff, movieOff, i, o;
    size_t k;

    (void)volume;
    Mpeg_Destroy();
    if (name == NULL) {
        return 0;
    }
    SDL_strlcpy(path, name, sizeof(path));
    for (k = 0; path[k]; k++) {
        if (path[k] == '\\') {
            path[k] = '/';
        }
    }
    m = SDL_calloc(1, sizeof(*m));
    m->io = Port_OpenDiscFile(path);
    if (m->io == NULL || SDL_ReadIO(m->io, hdr, sizeof(hdr)) != sizeof(hdr) || memcmp(hdr, "THP\0", 4) != 0) {
        Port_Log("movie %s: not found or not a THP file", name);
        Movie_Free(m);
        return 0;
    }
    {
        union { u32 u; f32 f; } c;
        c.u = Be32(hdr + 0x10);
        m->fps = c.f > 1.0f && c.f < 120.0f ? c.f : 29.97f;
    }
    m->numFrames = Be32(hdr + 0x14);
    m->nextSize = Be32(hdr + 0x18);
    compOff = Be32(hdr + 0x20);
    movieOff = Be32(hdr + 0x28);
    if (SDL_SeekIO(m->io, compOff, SDL_IO_SEEK_SET) < 0 || SDL_ReadIO(m->io, comp, sizeof(comp)) != sizeof(comp)) {
        Movie_Free(m);
        return 0;
    }
    m->numComps = Be32(comp);
    if (m->numComps > 16) {
        m->numComps = 16;
    }
    memcpy(m->compTypes, comp + 4, 16);
    o = 0x14;
    for (i = 0; i < m->numComps && o + 12 <= sizeof(comp); i++) {
        if (m->compTypes[i] == 0) {
            m->width = (int)Be32(comp + o);
            m->height = (int)Be32(comp + o + 4);
            o += 8;
        } else if (m->compTypes[i] == 1) {
            m->channels = (int)Be32(comp + o);
            m->rate = Be32(comp + o + 4);
            o += 12;
        }
    }
    if (m->width <= 0 || m->height <= 0 || m->width > 1024 || m->height > 1024) {
        Port_Log("movie %s: unsupported video %dx%d", name, m->width, m->height);
        Movie_Free(m);
        return 0;
    }
    m->rgba = SDL_calloc((size_t)m->width * m->height, 4);
    m->nextOffset = movieOff;
    if (m->channels == 1 || m->channels == 2) {
        SDL_AudioSpec spec = { SDL_AUDIO_S16, m->channels, (int)m->rate };
        m->audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
        if (m->audio != NULL) {
            /* as the original (THPPlay.c): THPPlayerAudioOutput gets volume
             * (u32)(46.0f * volumeScale), clamped to 127, and scales samples by
             * VolumeTable[volume] / 32768, a square law table: floor(32768 i^2 / 127^2).
             * Full music volume (1.0) -> 46 -> 4298 / 32768, about -17.6 dB. */
            f32 scaled = 46.0f * (f32)volumeScale;
            u32 vol = scaled <= 0.0f ? 0 : scaled >= 127.0f ? 127 : (u32)scaled;
            u32 table = (u32)(32768ull * vol * vol / (127 * 127));
            SDL_SetAudioStreamGain(m->audio, table / 32768.0f);
            SDL_ResumeAudioStreamDevice(m->audio);
        }
    }
    for (i = 0; i <= AUDIO_AHEAD && ReadFrame(m); i++) {
    }
    m->startNS = SDL_GetTicksNS();
    s_movie = m;
    Port_Log("movie %s: %ux%u, %u frames at %.2f fps, audio %d ch %u Hz", name, m->width, m->height,
             m->numFrames, m->fps, m->channels, m->rate);
    return 1;
}

/* Returns 0 once the movie has ended. */
s32 Mpeg_Heartbeat(void) {
    Movie *m = s_movie;
    u32 due;

    if (m == NULL) {
        return 0;
    }
    due = (u32)((double)(SDL_GetTicksNS() - m->startNS) * m->fps / 1e9);
    if (due >= m->numFrames) {
        return 0;
    }
    while (m->framesRead <= due + AUDIO_AHEAD && ReadFrame(m)) {
    }
    if (due >= m->framesRead) {
        return 0;   /* ran out of data early */
    }
    if (m->shown != due + 1) {
        u32 slot = due % (AUDIO_AHEAD + 1);
        if (m->video[slot] != NULL && THPJpeg_Decode(m->video[slot], m->videoSize[slot], m->rgba, m->width, m->height)) {
            m->shown = due + 1;
        }
    }
    return 1;
}

s32 Mpeg_Render(void) {
    if (s_movie != NULL && s_movie->shown != 0) {
        GX_PortDrawImage(s_movie->rgba, s_movie->width, s_movie->height);
    }
    VIWaitForRetrace();
    return 1;
}

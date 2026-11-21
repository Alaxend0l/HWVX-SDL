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
 * frames ahead of the picture.
 *
 * The PC release's movies are Bink files (FMV_/name.bik), played through the
 * player's own FFmpeg when there is one (bink.c), on the same clock, audio
 * stream and drawing; without FFmpeg they are skipped, as a missing THP is.
 * So are the PS2's (name.pss, MPEG-2), whose audio track pss.c reads. */
#include "port_internal.h"
#include "bink.h"
#include "pss.h"

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
    u32 beats;          /* heartbeats so far: the clock under a fixed step */
    s16 *pcm;
    u32 pcmCap;
    BinkMovie *bink;    /* a movie FFmpeg plays (bink.c: Bink, PSS) instead of THP */
    u32 decoded;        /* FFmpeg: video frames decoded so far */
    PssAudio *pss;      /* a PSS's own audio track (pss.c) */
    u32 pssFed;         /* its frames queued so far */
    double pssLead;     /* its start after the picture's, in seconds */
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
    Bink_Close(m->bink);
    Pss_CloseAudio(m->pss);
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
    s16 *pcm;

    /* the header and every channel's coefficients and history in the block */
    if (m->audio == NULL || m->channels < 1 || m->channels > 2 || size < 8 + 36u * (u32)m->channels) {
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
    /* no more samples than the channel's 8-byte frames hold, 14 each */
    if (n > chSize / 8 * 14) {
        n = chSize / 8 * 14;
    }
    if (n == 0) {
        return;
    }
    if (n * m->channels > m->pcmCap) {
        pcm = SDL_realloc(m->pcm, (size_t)n * m->channels * sizeof(s16));
        if (pcm == NULL) {
            return;
        }
        m->pcm = pcm;
        m->pcmCap = n * m->channels;
    }
    for (ch = 0; ch < m->channels; ch++) {
        DecodeADPCM(p + (size_t)ch * chSize, n, coefs[ch], hist[ch], m->pcm + ch, m->channels);
    }
    SDL_PutAudioStreamData(m->audio, m->pcm, (int)(n * m->channels * sizeof(s16)));
}

/* A frame bigger than this is a corrupt size, not a THP frame (they are tens
 * of KB). */
#define FRAME_MAX (64u << 20)

/* Reads the next frame: queues its audio, keeps its JPEG for display. */
static int ReadFrame(Movie *m) {
    u32 size = m->nextSize, i, off;
    u8 *f;

    if (m->framesRead >= m->numFrames || size < 8 + 4 * m->numComps || size > FRAME_MAX) {
        return 0;
    }
    if (size > m->frameCap) {
        f = SDL_realloc(m->frame, size);
        if (f == NULL) {
            return 0;
        }
        m->frame = f;
        m->frameCap = size;
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
        if (cs > size - off) {   /* not off + cs > size: that wraps */
            break;
        }
        if (m->compTypes[i] == 0) {
            u32 slot = m->framesRead % (AUDIO_AHEAD + 1);
            u8 *v = SDL_realloc(m->video[slot], cs > 0 ? cs : 1);
            if (v == NULL) {
                m->videoSize[slot] = 0;
            } else {
                m->video[slot] = v;
                memcpy(v, f + off, cs);
                m->videoSize[slot] = cs;
            }
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

/* Movies off (graphics.cfg "movies=off", as the PC release's Disable Movies
 * option; HWVX_NO_MOVIE=1 or 0 overrides): every movie is left unstarted, so
 * Mpeg_Heartbeat reports "ended" and State_Movie_Heartbeat moves on through
 * its normal skip path. The intro FMVs are what stands between boot and the
 * attract demo. */
static int MoviesOff(void) {
    static int off = -1;
    const char *e;

    if (off < 0) {
        e = SDL_getenv("HWVX_NO_MOVIE");
        off = e != NULL ? *e == '1' : g_port.moviesOff;
        if (off) {
            Port_Log("movies disabled (%s)", e != NULL ? "HWVX_NO_MOVIE=1" : "graphics.cfg movies=off");
        }
    }
    return off;
}

/* The movie's own audio stream, playing. */
static void OpenAudio(Movie *m, SDL_AudioFormat format, double volumeScale) {
    SDL_AudioSpec spec = { format, m->channels, (int)m->rate };

    m->audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (m->audio != NULL) {
        /* The original (THPPlay.c) truncated (u32)(46.0f * volumeScale) to an
         * index into VolumeTable, a square law: floor(32768 i^2 / 127^2), so
         * 1.0 -> 46 -> 4298 / 32768, about -17.6 dB. The truncation and the
         * square law together made the movie inaudible below about 15 while
         * the game's own volume (SetMusicGVol) is linear, so keep the full
         * volume level and scale it linearly with the volume instead. The PC's
         * Bink audio is as loud as the GameCube's THP (hw_intro: mean -11.0 dB
         * against -10.9 dB), so it takes the same gain. */
        f32 gain = 4298.0f / 32768.0f * (f32)volumeScale;
        SDL_SetAudioStreamGain(m->audio, gain < 0.0f ? 0.0f : gain > 1.0f ? 1.0f : gain);
        SDL_ResumeAudioStreamDevice(m->audio);
    }
}

static void BinkAudio(void *user, const float *pcm, int frames) {
    Movie *m = user;

    if (m->audio != NULL) {
        SDL_PutAudioStreamData(m->audio, pcm, frames * m->channels * (int)sizeof(float));
    }
}

/* Queues the PS2 track's audio up to `ahead` video frames past `due`, from
 * where it starts against the picture (silence before, if it starts later). */
static void FeedPss(Movie *m, u32 due) {
    double until = (due + 1 + AUDIO_AHEAD) / (double)m->fps;
    s16 buf[2048];
    int per = (int)(SDL_arraysize(buf) / m->channels), n;

    if (m->pssLead != 0.0) {
        u32 left = (u32)(SDL_fabs(m->pssLead) * m->rate);

        SDL_memset(buf, 0, sizeof(buf));
        while (left > 0) {
            if (m->pssLead > 0.0) {   /* starts after the picture: silence first */
                n = (int)(left < (u32)per ? left : (u32)per);
                if (m->audio != NULL) {
                    SDL_PutAudioStreamData(m->audio, buf, n * m->channels * (int)sizeof(s16));
                }
                m->pssFed += (u32)n;
            } else if ((n = Pss_ReadAudio(m->pss, buf, (int)(left < (u32)per ? left : (u32)per))) <= 0) {
                break;   /* starts before it: what's before is dropped */
            }
            left -= (u32)n < left ? (u32)n : left;
        }
        m->pssLead = 0.0;
    }
    /* pssFed: frames queued, the silence included, so it is the picture's clock */
    while ((double)m->pssFed / m->rate < until) {
        n = Pss_ReadAudio(m->pss, buf, per);
        if (n <= 0) {
            break;
        }
        if (m->audio != NULL) {
            SDL_PutAudioStreamData(m->audio, buf, n * m->channels * (int)sizeof(s16));
        }
        m->pssFed += (u32)n;
    }
}

/* A movie FFmpeg plays (bink.c): the PC's FMV_/name.bik, or the PS2's
 * name.pss with its own audio track (pss.c). 1 if it plays, 0 if it can't
 * (logged), -1 if there is no such file. */
static int StartFF(Movie *m, const char *name, const char *path, double volumeScale) {
    char file[288];
    size_t n = SDL_strlen(path);
    const char *kind = "Bink", *base = path, *p;
    SDL_IOStream *io = NULL;
    double fps;
    int frames, rate, pss = 0;

    for (p = path; *p; p++) {
        if (*p == '/') {
            base = p + 1;
        }
    }
    if (n > 4 && SDL_strcasecmp(path + n - 4, ".bik") == 0) {
        SDL_strlcpy(file, path, sizeof(file));
    } else {
        SDL_snprintf(file, sizeof(file), "%s.bik", path);
    }
    io = Port_OpenDiscFile(file);
    /* the PS2's: under XPS/ENGLISH/ unless the data folder is that folder;
     * the prototype keeps them there without the FMV_ folder */
    if (io == NULL) {
        static const char *const fmts[] = { "%s.pss", "XPS/ENGLISH/%s.pss", "XPS/ENGLISH/%s.pss" };
        int k;

        for (k = 0; io == NULL && k < (int)SDL_arraysize(fmts); k++) {
            SDL_snprintf(file, sizeof(file), fmts[k], k == 2 ? base : path);
            io = Port_OpenDiscFile(file);
        }
        pss = io != NULL;
        kind = "PSS";
    }
    if (io == NULL) {
        return -1;
    }
    if (!Bink_Load()) {
        Port_Log("movie %s: %s, skipped (no FFmpeg)", name, kind);
        SDL_CloseIO(io);
        return 0;
    }
    m->bink = Bink_Open(io, file);
    if (m->bink == NULL) {
        Port_Log("movie %s: %s, skipped (FFmpeg can't play it)", name, kind);
        return 0;
    }
    Bink_Info(m->bink, &m->width, &m->height, &fps, &frames, &m->channels, &rate);
    m->fps = (f32)fps;
    m->numFrames = (u32)frames;
    m->rate = (u32)rate;
    if (m->width <= 0 || m->height <= 0 || m->width > 1024 || m->height > 1024) {
        Port_Log("movie %s: unsupported video %dx%d", name, m->width, m->height);
        return 0;
    }
    m->rgba = SDL_calloc((size_t)m->width * m->height, 4);
    if (pss && m->channels == 0 && (m->pss = Pss_OpenAudio(Port_OpenDiscFile(file))) != NULL) {
        double start;
        u32 total;

        Pss_AudioInfo(m->pss, &m->channels, &rate, &start, &total);
        m->rate = (u32)rate;
        m->pssLead = start >= 0.0 ? start - Bink_StartTime(m->bink) : 0.0;
        OpenAudio(m, SDL_AUDIO_S16, volumeScale);
        FeedPss(m, 0);
    } else if (m->channels >= 1 && m->channels <= 8) {
        OpenAudio(m, SDL_AUDIO_F32, volumeScale);
        Bink_SetAudio(m->bink, BinkAudio, m);
    }
    Port_Log("movie %s: %s %dx%d, %u frames at %.2f fps, audio %d ch %u Hz%s", name, kind, m->width, m->height,
             m->numFrames, m->fps, m->channels, m->rate, m->pss != NULL ? " (SShd PCM)" : "");
    return 1;
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
    if (MoviesOff()) {
        return 1;
    }
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
        int ff;

        if (m->io != NULL) {
            SDL_CloseIO(m->io);
            m->io = NULL;
        }
        ff = StartFF(m, name, path, volumeScale);
        if (ff <= 0) {
            if (ff < 0) {
                Port_Log("movie %s: not found or not a THP, Bink or PSS file", name);
            }
            Movie_Free(m);
            return 0;
        }
        m->startNS = SDL_GetTicksNS();
        s_movie = m;
        return 1;
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
        OpenAudio(m, SDL_AUDIO_S16, volumeScale);
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
    /* under a fixed step (HWVX_DETERMINISTIC), game frames are the clock, so
     * a movie lasts the same number of frames on every run */
    if (Port_FixedStepUs() != 0) {
        due = (u32)((double)m->beats++ * Port_FixedStepUs() * m->fps / 1e6);
    } else {
        due = (u32)((double)(SDL_GetTicksNS() - m->startNS) * m->fps / 1e9);
    }
    if (m->bink != NULL) {
        /* decode up to the frame due, converting only that one; the movie
         * ends with its last frame (a PSS's frame count is an estimate) */
        while (m->decoded <= due) {
            if (!Bink_NextFrame(m->bink, m->decoded == due ? m->rgba : NULL)) {
                return 0;
            }
            m->shown = ++m->decoded;
        }
        if (m->pss != NULL) {
            FeedPss(m, due);
        }
        return 1;
    }
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

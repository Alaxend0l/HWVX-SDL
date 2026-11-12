/* Streamed music: a mod's recorded songs (OGG, MP3, FLAC, WAV) in place of a
 * sequenced one. Port-only; docs-pending/streamed-music.md.
 *
 * When the game creates a song (CreateMusic, as a pack with one loads), the
 * pack's id is looked up in the enabled mods' "music" (Port_ModsMusic). With
 * an entry, the game's music hooks (DestroyMusic, SetMusicState,
 * SetMusicVolume...) point here instead of at the MIDI/IT player, so the game
 * starts, pauses, stops and sets the volume of the stream as it would its
 * song. Without one nothing here runs and the mixer's output is unchanged.
 *
 * Decoding: miniaudio's decoders (dr_wav, dr_flac, dr_mp3) and stb_vorbis,
 * vendored in src/third_party, at the file's own rate, read a chunk at a time;
 * a cubic resampler brings it to the mixer's 48 kHz into a ring of
 * RING_FRAMES. The AX thread tops the rings up between AX frames
 * (PortStream_Refill, outside the giant lock) and mixes them in RunAXFrame
 * (PortStream_Mix, under it): both on one thread, so the ring needs no lock.
 * The game's calls (under the giant lock) and Refill share s_lock.
 *
 * Loops: a track loops from loopStart to loopEnd (sample frames of the file):
 * the mod's entry, else the file's LOOPSTART / LOOPEND / LOOPLENGTH tags
 * (Vorbis comments, ID3 TXXX), else a sidecar <file>.loop.txt with the same
 * lines, else the whole file. A playlist (several tracks) plays each to its
 * end in turn and wraps. A song the game destroys fades out over FADE_MS
 * while the next one starts (a crossfade). */
#include "port_internal.h"
#include "Port_Mods.h"

#define MA_NO_DEVICE_IO
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_GENERATION
#define MA_NO_ENCODING
#define MA_NO_THREADING
#define MA_API static
#define MINIAUDIO_IMPLEMENTATION
#define STB_VORBIS_HEADER_ONLY
#include "../third_party/stb_vorbis.c"
#include "../third_party/miniaudio.h"
#undef STB_VORBIS_HEADER_ONLY
#include "../third_party/stb_vorbis.c"

#define OUT_RATE 48000          /* the mixer's (ax.c AX_RATE) */
#define RING_FRAMES 16384       /* 341 ms of 48 kHz stereo: 128 KB */
#define CHUNK_FRAMES 2048       /* decoded per read, at the file's rate */
#define FADE_MS 500
#define MAX_STREAMS 3           /* the playing one and those fading out */

typedef struct Stream {
    PortModMusic *tracks;
    int numTracks, cur;
    ma_decoder dec;
    int decOpen;
    u32 srcRate;
    s64 srcPos;                 /* frames read from the track */
    s64 sinceLoop;              /* frames read since the last loop or track change */
    int emptyRuns;              /* track ends in a row with nothing read: all broken */
    s64 loopStart, loopEnd;     /* this track's, resolved; loopEnd -1: the end */
    float chunk[CHUNK_FRAMES * 2];
    int chunkLen, chunkPos;
    float hist[4][2];           /* the resampler's last four source frames */
    double phase, step;
    float ring[RING_FRAMES * 2];
    u64 wr, rd;                 /* frames written / read */
    int ended;                  /* nothing more to decode */
    int state;                  /* the game's: 0 stopped, 1 playing, 2 paused */
    float gain, target;         /* now and the music volume's */
    float fade;                 /* 1, falling to 0 once destroyed */
    int dying, dead;
    char name[64];
} Stream;

static SDL_Mutex *s_lock;
static Stream *s_streams[MAX_STREAMS];
static Stream *s_cur;           /* the one the game's hooks drive */
static int s_log = -1;
static char s_packName[64];     /* the pack whose song is being created */
static unsigned s_packType, s_packId;
static int s_packSet;
extern u32 g_musicVol;          /* Music.c: the game's music volume */

static void Log(const char *fmt, ...) PORT_PRINTF(1, 2);
static void Log(const char *fmt, ...) {
    va_list ap;
    char line[512];

    if (s_log < 0) {
        s_log = SDL_getenv("HWVX_MUSICLOG") != NULL;
    }
    if (!s_log) {
        return;
    }
    va_start(ap, fmt);
    SDL_vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    Port_Log("music: %s", line);
}

/* ---- loop points from tags -------------------------------------------- */

/* "KEY=123" or "KEY\0123" (ID3 TXXX) anywhere in buf, case-insensitive. */
static s64 TagValue(const u8 *buf, size_t n, const char *key) {
    size_t kl = SDL_strlen(key), i;

    for (i = 0; i + kl + 1 < n; i++) {
        if (SDL_strncasecmp((const char *)buf + i, key, kl) == 0 && (buf[i + kl] == '=' || buf[i + kl] == 0)) {
            size_t j = i + kl + 1;
            s64 v = 0;
            int digits = 0;

            while (j < n && buf[j] == 0) {   /* ID3v2.4 text encodings may pad */
                j++;
            }
            while (j < n && buf[j] >= '0' && buf[j] <= '9' && digits < 18) {
                v = v * 10 + (buf[j++] - '0');
                digits++;
            }
            if (digits > 0) {
                return v;
            }
        }
    }
    return -1;
}

static void TagsFrom(const char *path, size_t max, s64 *start, s64 *end) {
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    u8 *buf;
    size_t n;
    s64 len;

    if (io == NULL) {
        return;
    }
    buf = SDL_malloc(max);
    n = buf != NULL ? SDL_ReadIO(io, buf, max) : 0;
    SDL_CloseIO(io);
    if (n == 0) {
        SDL_free(buf);
        return;
    }
    if (*start < 0) {
        *start = TagValue(buf, n, "LOOPSTART");
    }
    if (*end < 0) {
        *end = TagValue(buf, n, "LOOPEND");
        if (*end < 0 && *start >= 0 && (len = TagValue(buf, n, "LOOPLENGTH")) > 0) {
            *end = *start + len;
        }
    }
    SDL_free(buf);
}

/* ---- decoding ------------------------------------------------------------ */

static void CloseTrack(Stream *s) {
    if (s->decOpen) {
        ma_decoder_uninit(&s->dec);
        s->decOpen = 0;
    }
}

/* Opens track i at its start; 0 if it can't be read. */
static int OpenTrack(Stream *s, int i) {
    const PortModMusic *t = &s->tracks[i];
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, 0);
    ma_uint32 rate = 0;
    char *side = NULL;

    CloseTrack(s);
    if (ma_decoder_init_file(t->path, &cfg, &s->dec) != MA_SUCCESS) {
        Port_Log("music: %s: can't be decoded (OGG, MP3, FLAC or WAV)", t->path);
        return 0;
    }
    s->decOpen = 1;
    ma_decoder_get_data_format(&s->dec, NULL, NULL, &rate, NULL, 0);
    if (rate < 1000 || rate > 384000) {
        Port_Log("music: %s: sample rate %u", t->path, (unsigned)rate);
        CloseTrack(s);
        return 0;
    }
    s->srcRate = rate;
    s->step = (double)rate / OUT_RATE;
    s->cur = i;
    s->srcPos = 0;
    s->sinceLoop = 0;
    s->chunkLen = s->chunkPos = 0;
    s->loopStart = t->loopStart;
    s->loopEnd = t->loopEnd;
    if (s->loopStart < 0 || s->loopEnd < 0) {
        TagsFrom(t->path, 256 * 1024, &s->loopStart, &s->loopEnd);
    }
    if ((s->loopStart < 0 || s->loopEnd < 0) && SDL_asprintf(&side, "%s.loop.txt", t->path) > 0) {
        TagsFrom(side, 4096, &s->loopStart, &s->loopEnd);
    }
    SDL_free(side);
    if (s->loopStart < 0) {
        s->loopStart = 0;
    }
    if (s->loopEnd >= 0 && s->loopEnd <= s->loopStart) {
        s->loopEnd = -1;
    }
    Log("%s: %u Hz, loop %lld..%lld", t->path, (unsigned)rate, (long long)s->loopStart, (long long)s->loopEnd);
    return 1;
}

/* The next source frame into f; 0 at the very end (a track played once). */
static int NextFrame(Stream *s, float f[2]) {
    while (s->chunkPos >= s->chunkLen) {
        ma_uint64 want = CHUNK_FRAMES, got = 0;
        const PortModMusic *t = &s->tracks[s->cur];

        if (!s->decOpen) {
            return 0;
        }
        if (s->loopEnd >= 0 && t->loop && s->numTracks == 1) {
            if (s->srcPos >= s->loopEnd) {
                want = 0;
            } else if ((s64)want > s->loopEnd - s->srcPos) {
                want = (ma_uint64)(s->loopEnd - s->srcPos);
            }
        }
        if (want > 0) {
            ma_decoder_read_pcm_frames(&s->dec, s->chunk, want, &got);
        }
        s->chunkLen = (int)got;
        s->chunkPos = 0;
        s->srcPos += (s64)got;
        s->sinceLoop += (s64)got;
        if (got > 0) {
            s->emptyRuns = 0;
            break;
        }
        /* the end of the track or its loop (or a read error: a damaged file
         * plays up to the damage). Nothing read since the last one, once per
         * track: stop rather than spin. */
        if (s->sinceLoop == 0 && ++s->emptyRuns >= s->numTracks) {
            Port_Log("music: %s: nothing more can be decoded; the stream stops", s->name);
            CloseTrack(s);
            return 0;
        }
        s->sinceLoop = 0;
        if (s->numTracks > 1) {
            int next = (s->cur + 1) % s->numTracks, tries;

            for (tries = 0; tries < s->numTracks && !OpenTrack(s, next); tries++) {
                next = (next + 1) % s->numTracks;
            }
            if (!s->decOpen) {
                return 0;
            }
        } else if (t->loop) {
            if (ma_decoder_seek_to_pcm_frame(&s->dec, (ma_uint64)s->loopStart) != MA_SUCCESS) {
                Port_Log("music: %s: can't seek to its loop start %lld; the stream stops", s->name,
                         (long long)s->loopStart);
                CloseTrack(s);
                return 0;
            }
            Log("%s: loop at %lld to %lld", s->name, (long long)s->srcPos, (long long)s->loopStart);
            s->srcPos = s->loopStart;
        } else {
            CloseTrack(s);
            return 0;
        }
    }
    f[0] = s->chunk[s->chunkPos * 2];
    f[1] = s->chunk[s->chunkPos * 2 + 1];
    s->chunkPos++;
    /* a damaged file can decode to anything: no NaN or huge values into the mix */
    f[0] = f[0] >= -2.0f && f[0] <= 2.0f ? f[0] : f[0] > 2.0f ? 2.0f : f[0] < -2.0f ? -2.0f : 0.0f;
    f[1] = f[1] >= -2.0f && f[1] <= 2.0f ? f[1] : f[1] > 2.0f ? 2.0f : f[1] < -2.0f ? -2.0f : 0.0f;
    return 1;
}

/* Fills the ring: cubic (Catmull-Rom) resampling from the file's rate. */
static void Fill(Stream *s) {
    while (!s->ended && s->wr - s->rd < RING_FRAMES) {
        float *o = &s->ring[(s->wr % RING_FRAMES) * 2];
        float t, vol;
        int c;

        while (s->phase >= 1.0) {
            float f[2] = { 0.0f, 0.0f };

            if (!NextFrame(s, f)) {
                s->ended = 1;
            }
            SDL_memmove(s->hist[0], s->hist[1], sizeof(s->hist[0]) * 3);
            s->hist[3][0] = f[0];
            s->hist[3][1] = f[1];
            s->phase -= 1.0;
        }
        t = (float)s->phase;
        vol = s->tracks[s->cur].volume;
        for (c = 0; c < 2; c++) {
            float y0 = s->hist[0][c], y1 = s->hist[1][c], y2 = s->hist[2][c], y3 = s->hist[3][c];

            o[c] = vol * (y1 + 0.5f * t * (y2 - y0 + t * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 +
                                                           t * (3.0f * (y1 - y2) + y3 - y0))));
        }
        s->wr++;
        s->phase += s->step;
    }
}

/* Back to the first track that decodes, the ring emptied and filled: 0 if
 * none gives a single frame. */
static int Rewind(Stream *s) {
    int i;

    SDL_zeroa(s->hist);
    s->phase = 3.0;     /* reads three frames: the first output is the first frame */
    s->wr = s->rd = 0;
    s->ended = 0;
    s->emptyRuns = 0;
    for (i = 0; i < s->numTracks && !OpenTrack(s, i); i++) {
    }
    if (i == s->numTracks) {
        s->ended = 1;
        return 0;
    }
    Fill(s);
    return s->wr > 0 && !(s->ended && s->wr <= 3);
}

static void FreeStream(Stream *s) {
    CloseTrack(s);
    Port_ModsMusicFree(s->tracks, s->numTracks);
    SDL_free(s);
}

/* ---- the game's music hooks (Music.c CreateMusic) ------------------------ */

static void Lock(void) {
    if (s_lock == NULL) {
        s_lock = SDL_CreateMutex();
    }
    SDL_LockMutex(s_lock);
}

/* The pack the next song comes from (DataPack_LoadFile); NULL after it. */
void PortStream_SetPack(const char *name, unsigned type, unsigned id) {
    s_packSet = name != NULL;
    if (name != NULL) {
        const char *p;

        for (p = name; *p; p++) {   /* "worlds_\\sp_\\chal1_\\ch1_l2": base://ch1_l2/... */
            if (*p == '\\' || *p == '/') {
                name = p + 1;
            }
        }
        SDL_strlcpy(s_packName, name, sizeof(s_packName));
        s_packType = type;
        s_packId = id;
    }
}

/* The current stream fades out (it's destroyed, or another song starts). */
static void Retire(void) {
    if (s_cur != NULL) {
        Log("%s: fades out", s_cur->name);
        s_cur->dying = 1;
        s_cur = NULL;
    }
}

int PortStream_Create(void) {
    PortModMusic *tracks;
    int n, i;
    Stream *s;

    if (s_cur == NULL && !(s_packSet && Port_ModsActive())) {
        return 0;   /* the retail game: nothing here runs */
    }
    Lock();
    Retire();
    n = s_packSet ? Port_ModsMusic(s_packName, s_packType, s_packId, &tracks) : 0;
    if (n == 0) {
        SDL_UnlockMutex(s_lock);
        return 0;
    }
    for (i = 0; i < MAX_STREAMS && s_streams[i] != NULL; i++) {
    }
    if (i == MAX_STREAMS) {     /* too many fading out: drop the oldest */
        FreeStream(s_streams[0]);
        SDL_memmove(s_streams, s_streams + 1, sizeof(s_streams[0]) * (MAX_STREAMS - 1));
        i = MAX_STREAMS - 1;
    }
    s = SDL_calloc(1, sizeof(*s));
    if (s == NULL) {
        Port_ModsMusicFree(tracks, n);
        SDL_UnlockMutex(s_lock);
        return 0;
    }
    s->tracks = tracks;
    s->numTracks = n;
    s->fade = 1.0f;
    SDL_snprintf(s->name, sizeof(s->name), "%s/%u", s_packName, s_packId);
    s->target = (float)g_musicVol * (1.0f / 32767.0f);
    if (!Rewind(s)) {
        /* the game's own song plays instead */
        Port_Log("music: %s: no track of the mod's decodes (%s); the game's own song plays", s->name,
                 tracks[0].path);
        FreeStream(s);
        SDL_UnlockMutex(s_lock);
        return 0;
    }
    s_streams[i] = s;
    s_cur = s;
    Log("%s: streams %s (%d track%s) for its song", s->name, tracks[0].path, n, n == 1 ? "" : "s");
    SDL_UnlockMutex(s_lock);
    return 1;
}

s32 PortStream_Destroy(void) {
    Lock();
    Retire();
    SDL_UnlockMutex(s_lock);
    return 0;
}

/* As SetMIDIState: 0 asks, 1 plays (from the start when stopped), 2 pauses,
 * 3 stops, 4 restarts. The old state. */
s32 PortStream_SetState(s32 state) {
    s32 old;

    if (s_cur == NULL) {
        return -1;
    }
    old = s_cur->state;
    if (state == 0) {
        return old;
    }
    Lock();
    if (state == 3 || state == 4 || (state == 1 && old == 0)) {
        if (old != 0) {
            Rewind(s_cur);
        }
        s_cur->state = 0;
    }
    if (state == 1 || state == 4) {
        if (s_cur->state != 1 && s_cur->state != 2) {
            s_cur->gain = s_cur->target;    /* starts at its volume, as a song does */
        }
        s_cur->state = 1;
    } else if (state == 2) {
        s_cur->state = 2;
    }
    Log("%s: state %d -> %d", s_cur->name, old, s_cur->state);
    SDL_UnlockMutex(s_lock);
    return old;
}

/* g_musicVol: the game's music volume (its option times the world's fades). */
s32 PortStream_SetVol(void) {
    if (s_cur != NULL) {
        s_cur->target = (float)g_musicVol * (1.0f / 32767.0f);   /* each track's "volume" in Fill */
        Log("%s: volume %.3f", s_cur->name, s_cur->target);
    }
    return 0;
}

int PortStream_Current(void) { return s_cur != NULL; }

/* ---- the AX thread --------------------------------------------------------- */

/* Between AX frames, outside the giant lock: the rings topped up, the
 * streams that faded out freed. */
void PortStream_Refill(void) {
    int i;

    if (s_lock == NULL) {
        return;     /* no stream was ever made */
    }
    SDL_LockMutex(s_lock);
    for (i = 0; i < MAX_STREAMS; i++) {
        Stream *s = s_streams[i];

        if (s == NULL) {
            continue;
        }
        if (s->dead) {
            Log("%s: freed", s->name);
            FreeStream(s);
            s_streams[i] = NULL;
            continue;
        }
        if (s->wr - s->rd < RING_FRAMES / 2) {
            Fill(s);
        }
    }
    SDL_UnlockMutex(s_lock);
}

/* In RunAXFrame, under the giant lock: the streams added into out (n stereo
 * frames). */
void PortStream_Mix(float *out, int n) {
    int i, k;
    static int warned;

    if (s_lock == NULL) {
        return;
    }
    for (i = 0; i < MAX_STREAMS; i++) {
        Stream *s = s_streams[i];
        float dfade = 1000.0f / (FADE_MS * (float)OUT_RATE);
        float dgain = 1.0f / (0.02f * OUT_RATE);     /* volume changes over 20 ms at most */

        if (s == NULL || s->dead || (s->state != 1 && !s->dying)) {
            continue;
        }
        if (s->dying && s->state != 1) {
            s->dead = 1;    /* paused or stopped: nothing to fade */
            continue;
        }
        for (k = 0; k < n; k++) {
            const float *f;

            if (s->rd == s->wr) {
                if (!s->ended && !warned) {
                    warned = 1;
                    Port_Log("music: %s: the decoder fell behind", s->name);
                }
                if (s->ended && s->dying) {
                    s->dead = 1;
                }
                break;
            }
            f = &s->ring[(s->rd % RING_FRAMES) * 2];
            s->rd++;
            if (s->gain < s->target) {
                s->gain = SDL_min(s->gain + dgain, s->target);
            } else if (s->gain > s->target) {
                s->gain = SDL_max(s->gain - dgain, s->target);
            }
            out[k * 2] += f[0] * s->gain * s->fade;
            out[k * 2 + 1] += f[1] * s->gain * s->fade;
            if (s->dying && (s->fade -= dfade) <= 0.0f) {
                s->fade = 0.0f;
                s->dead = 1;
                break;
            }
        }
    }
}

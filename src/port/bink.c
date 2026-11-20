/* Bink movies (the PC release's .bik files in FMV_) through FFmpeg, loaded at run time.
 *
 * The PC files are Bink 1 ("BIKi"): 640x480 at 29.97 fps, 4:2:0 video and
 * Bink Audio (DCT) at 48000 Hz stereo. No permissively licensed Bink decoder
 * exists, so the port uses the player's own FFmpeg (LGPL) when there is one:
 * libavformat, libavcodec and libavutil are opened with SDL_LoadObject, never
 * linked, and the build needs neither them nor their headers. Without them a
 * Bink movie is skipped, as before (thp.c).
 *
 * The same code plays the PS2's MPEG-2 movies (.PSS), whose video FFmpeg's
 * program stream demuxer reads; their audio track FFmpeg doesn't see, so
 * pss.c reads it.
 *
 * Which FFmpeg: releases 6.0 to 9.x (libavformat/libavcodec majors 60-63,
 * libavutil 58-61; the Flatpak runtime's 7.1 is 61). libswscale and
 * libswresample are not used: YUV to RGBA is done here, and SDL's audio stream
 * converts the rate and format. Everything goes through functions, except five
 * structs' leading fields, which are the same in all of these releases (checked against
 * their headers): AVFormatContext up to `streams`, AVStream up to `avg_frame_rate`
 * (6.0 moved av_class/codecpar to the front; 5.x and older differ), AVPacket up
 * to `stream_index`, AVFrame up to `format`, AVIOContext up to `buffer`. The
 * majors are checked (avformat_version(), avcodec_version(), avutil_version())
 * before anything else is called, and a library outside the range is not used.
 * Decoder settings are read through AVOptions (av_opt_get_*), which don't
 * depend on AVCodecContext's layout.
 *
 * Where: HWVX_FFMPEG_DIR, if set, is the only folder looked in. Otherwise the
 * game's own folder, the packages' (an .app's ../Frameworks, an AppImage's
 * ../lib), then the system's library search (and Homebrew's folders on
 * macOS), by each release's file names: libavformat.so.63 (Linux, BSD),
 * libavformat.63.dylib (macOS), avformat-63.dll (Windows), libavformat.so
 * (Android, and a fallback elsewhere). */
#include "port_internal.h"
#include "bink.h"
#include <errno.h>
#include <stdio.h>

#define FF_MIN_MAJOR 60     /* FFmpeg 6.0 */
#define FF_MAX_MAJOR 63     /* FFmpeg 9.x */
#define VIDEO_AHEAD 4       /* video packets read ahead (their audio is queued) */

#define FF_EAGAIN (-(EAGAIN))
#define FF_EOF (-(int)(('E') | ('O' << 8) | ('F' << 16) | ((unsigned)' ' << 24)))
#define FF_SEEK_SIZE 0x10000
#define FF_SEEK_FORCE 0x20000
#define FF_MEDIA_VIDEO 0
#define FF_MEDIA_AUDIO 1
#define FF_LOG_ERROR 16

/* The leading fields of FFmpeg's structs, as they are in 6.0-9.x. */
typedef struct FFFormatHead {
    const void *av_class, *iformat, *oformat;
    void *priv_data;
    void *pb;
    int ctx_flags;
    unsigned nb_streams;
    void **streams;
} FFFormatHead;

typedef struct FFStreamHead {
    const void *av_class;
    int index, id;
    void *codecpar;
    void *priv_data;
    int tbNum, tbDen;
    int64_t start_time, duration, nb_frames;
    int disposition, discard;
    int sarNum, sarDen;
    void *metadata;
    int avgNum, avgDen;
} FFStreamHead;

typedef struct FFPacketHead {
    void *buf;
    int64_t pts, dts;
    uint8_t *data;
    int size, stream_index;
} FFPacketHead;

typedef struct FFFrameHead {
    uint8_t *data[8];
    int linesize[8];
    uint8_t **extended_data;
    int width, height, nb_samples, format;
} FFFrameHead;

typedef struct FFIOHead {
    const void *av_class;
    unsigned char *buffer;
} FFIOHead;

typedef struct FFChannelLayout {   /* AVChannelLayout (libavutil 57.24+) */
    int order, nb_channels;
    uint64_t mask;
    void *opaque;
} FFChannelLayout;

typedef int (*FFRead)(void *, uint8_t *, int);
typedef int64_t (*FFSeek)(void *, int64_t, int);

/* The functions used, by library. */
static struct {
    unsigned (*avutil_version)(void);
    void *(*av_frame_alloc)(void);
    void (*av_frame_free)(void **);
    void (*av_frame_unref)(void *);
    void *(*av_malloc)(size_t);
    void (*av_free)(void *);
    int (*av_opt_get_int)(void *, const char *, int, int64_t *);
    int (*av_opt_get_image_size)(void *, const char *, int, int *, int *);
    int (*av_opt_get_chlayout)(void *, const char *, int, FFChannelLayout *);
    void (*av_channel_layout_uninit)(FFChannelLayout *);
    int (*av_get_pix_fmt)(const char *);
    int (*av_get_sample_fmt)(const char *);
    void (*av_log_set_level)(int);

    unsigned (*avcodec_version)(void);
    void *(*avcodec_alloc_context3)(const void *);
    void (*avcodec_free_context)(void **);
    int (*avcodec_parameters_to_context)(void *, const void *);
    int (*avcodec_open2)(void *, const void *, void **);
    int (*avcodec_send_packet)(void *, const void *);
    int (*avcodec_receive_frame)(void *, void *);
    void *(*av_packet_alloc)(void);
    void (*av_packet_free)(void **);
    void (*av_packet_unref)(void *);
    void (*av_packet_move_ref)(void *, void *);

    unsigned (*avformat_version)(void);
    void *(*avformat_alloc_context)(void);
    int (*avformat_open_input)(void **, const char *, const void *, void **);
    int (*avformat_find_stream_info)(void *, void **);
    void (*avformat_close_input)(void **);
    int (*av_find_best_stream)(void *, int, int, int, const void **, int);
    int (*av_read_frame)(void *, void *);
    void *(*avio_alloc_context)(unsigned char *, int, int, void *, FFRead, void *, FFSeek);
    void (*avio_context_free)(void **);
} ff;

static SDL_SharedObject *s_lib[3];   /* avutil, avcodec, avformat */
static int s_state;                  /* 0: not tried, 1: loaded, -1: not available */
static char s_why[256];
static int s_pixYUV420, s_pixYUVJ420, s_pixYUVA420;
static int s_smpFLT, s_smpFLTP, s_smpS16, s_smpS16P;

struct BinkMovie {
    SDL_IOStream *io;
    void *fmt;          /* AVFormatContext */
    void *pb;           /* AVIOContext */
    void *vdec, *adec;  /* AVCodecContext */
    void *pkt, *frame;
    void *queue[VIDEO_AHEAD + 1];   /* video packets read ahead, in order */
    int qHead, qCount;
    int vIndex, aIndex;
    int width, height, frames, channels, rate;
    double fps;
    double start;       /* the video's first PTS, in seconds */
    int eof, flushed;
    BinkAudioFn audioFn;
    void *audioUser;
    float *pcm;
    size_t pcmCap;
    int badAudio, badVideo;
};

static void Unload(void) {
    int i;

    for (i = 2; i >= 0; i--) {
        if (s_lib[i] != NULL) {
            SDL_UnloadObject(s_lib[i]);
            s_lib[i] = NULL;
        }
    }
}

#define GET(lib, name)                                                          \
    do {                                                                        \
        *(SDL_FunctionPointer *)&ff.name = SDL_LoadFunction(s_lib[lib], #name); \
        if (ff.name == NULL) {                                                  \
            SDL_snprintf(s_why, sizeof(s_why), "%s has no %s", file, #name);    \
            return 0;                                                           \
        }                                                                       \
    } while (0)

/* The file name of FFmpeg library `lib` (0 avutil, 1 avcodec, 2 avformat) of
 * release `major` (libavformat's; 0: unversioned) in `dir` ("" for the
 * system's search). */
static void LibName(char *out, size_t cap, const char *dir, int lib, int major) {
    static const char *const names[3] = { "avutil", "avcodec", "avformat" };
    int v = lib == 0 ? major - 2 : major;
    const char *sep = *dir && dir[SDL_strlen(dir) - 1] != '/' && dir[SDL_strlen(dir) - 1] != '\\' ? "/" : "";

#if defined(SDL_PLATFORM_WINDOWS)
    if (major) {
        SDL_snprintf(out, cap, "%s%s%s-%d.dll", dir, sep, names[lib], v);
    } else {
        SDL_snprintf(out, cap, "%s%s%s.dll", dir, sep, names[lib]);
    }
#elif defined(SDL_PLATFORM_APPLE)
    if (major) {
        SDL_snprintf(out, cap, "%s%slib%s.%d.dylib", dir, sep, names[lib], v);
    } else {
        SDL_snprintf(out, cap, "%s%slib%s.dylib", dir, sep, names[lib]);
    }
#else
    if (major) {
        SDL_snprintf(out, cap, "%s%slib%s.so.%d", dir, sep, names[lib], v);
    } else {
        SDL_snprintf(out, cap, "%s%slib%s.so", dir, sep, names[lib]);
    }
#endif
}

/* Loads release `major` from `dir`; 1 if all three libraries are there, of
 * matching majors in the supported range, with every function. */
static int TryLoad(const char *dir, int major) {
    char file[1024];
    unsigned vu, vc, vf;
    int lib;

    for (lib = 0; lib < 3; lib++) {
        LibName(file, sizeof(file), dir, lib, major);
        if ((s_lib[lib] = SDL_LoadObject(file)) == NULL) {
            Unload();
            return 0;
        }
    }
    LibName(file, sizeof(file), dir, 0, major);
    GET(0, avutil_version);
    LibName(file, sizeof(file), dir, 1, major);
    *(SDL_FunctionPointer *)&ff.avcodec_version = SDL_LoadFunction(s_lib[1], "avcodec_version");
    LibName(file, sizeof(file), dir, 2, major);
    *(SDL_FunctionPointer *)&ff.avformat_version = SDL_LoadFunction(s_lib[2], "avformat_version");
    if (ff.avcodec_version == NULL || ff.avformat_version == NULL) {
        SDL_snprintf(s_why, sizeof(s_why), "%s is not FFmpeg", file);
        Unload();
        return 0;
    }
    vu = ff.avutil_version() >> 16;
    vc = ff.avcodec_version() >> 16;
    vf = ff.avformat_version() >> 16;
    if (vf < FF_MIN_MAJOR || vf > FF_MAX_MAJOR || vc != vf || vu != vf - 2) {
        SDL_snprintf(s_why, sizeof(s_why), "%s: libavformat %u, libavcodec %u, libavutil %u (need FFmpeg 6-9: %d-%d, %d-%d)",
                     file, vf, vc, vu, FF_MIN_MAJOR, FF_MAX_MAJOR, FF_MIN_MAJOR - 2, FF_MAX_MAJOR - 2);
        Unload();
        return 0;
    }
    return 1;
}

static int LoadFunctions(void) {
    char file[64];

    SDL_strlcpy(file, "libavutil", sizeof(file));
    GET(0, av_frame_alloc);
    GET(0, av_frame_free);
    GET(0, av_frame_unref);
    GET(0, av_malloc);
    GET(0, av_free);
    GET(0, av_opt_get_int);
    GET(0, av_opt_get_image_size);
    GET(0, av_opt_get_chlayout);
    GET(0, av_channel_layout_uninit);
    GET(0, av_get_pix_fmt);
    GET(0, av_get_sample_fmt);
    GET(0, av_log_set_level);
    SDL_strlcpy(file, "libavcodec", sizeof(file));
    GET(1, avcodec_alloc_context3);
    GET(1, avcodec_free_context);
    GET(1, avcodec_parameters_to_context);
    GET(1, avcodec_open2);
    GET(1, avcodec_send_packet);
    GET(1, avcodec_receive_frame);
    GET(1, av_packet_alloc);
    GET(1, av_packet_free);
    GET(1, av_packet_unref);
    GET(1, av_packet_move_ref);
    SDL_strlcpy(file, "libavformat", sizeof(file));
    GET(2, avformat_alloc_context);
    GET(2, avformat_open_input);
    GET(2, avformat_find_stream_info);
    GET(2, avformat_close_input);
    GET(2, av_find_best_stream);
    GET(2, av_read_frame);
    GET(2, avio_alloc_context);
    GET(2, avio_context_free);
    return 1;
}

int Bink_Load(void) {
    const char *env;
    const char *dirs[8];
    char *base = NULL, *sibling = NULL;
    int nd = 0, d, major, found = 0;
    unsigned v;

    if (s_state != 0) {
        return s_state > 0;
    }
    s_state = -1;
    env = SDL_getenv("HWVX_FFMPEG_DIR");
    if (env != NULL && *env) {
        dirs[nd++] = env;
    } else {
        const char *b = SDL_GetBasePath();
        if (b != NULL && *b) {
            base = SDL_strdup(b);
            dirs[nd++] = base;
#if defined(SDL_PLATFORM_APPLE) || (defined(SDL_PLATFORM_LINUX) && !defined(SDL_PLATFORM_ANDROID))
            /* the packages' own copy: an .app's Contents/Frameworks (the base
             * path is Contents/Resources), an AppImage's usr/lib (beside usr/bin) */
#if defined(SDL_PLATFORM_APPLE)
            SDL_asprintf(&sibling, "%s../Frameworks", base);
#else
            SDL_asprintf(&sibling, "%s../lib", base);
#endif
            if (sibling != NULL) {
                dirs[nd++] = sibling;
            }
#endif
        }
        dirs[nd++] = "";
#if defined(SDL_PLATFORM_MACOS)
        dirs[nd++] = "/opt/homebrew/lib";
        dirs[nd++] = "/usr/local/lib";
#endif
    }
    SDL_strlcpy(s_why, "", sizeof(s_why));
    for (d = 0; d < nd && !found; d++) {
        for (major = FF_MAX_MAJOR; major >= FF_MIN_MAJOR && !found; major--) {
            found = TryLoad(dirs[d], major);
        }
        if (!found) {
            found = TryLoad(dirs[d], 0);
        }
    }
    if (!found) {
        char want[64];
        LibName(want, sizeof(want), "", 2, FF_MAX_MAJOR);
        Port_Log("ffmpeg: no FFmpeg 6-9 found (looked for %s and older%s%s); the PC (Bink) and PS2 (PSS) "
                 "movies are skipped", want,
                 env != NULL && *env ? " in HWVX_FFMPEG_DIR=" : "", env != NULL && *env ? env : "");
        if (*s_why) {
            Port_Log("ffmpeg: %s", s_why);
        }
        SDL_free(base);
        SDL_free(sibling);
        return 0;
    }
    if (!LoadFunctions()) {
        Port_Log("ffmpeg: %s; the PC (Bink) and PS2 (PSS) movies are skipped", s_why);
        Unload();
        SDL_free(base);
        SDL_free(sibling);
        return 0;
    }
    ff.av_log_set_level(FF_LOG_ERROR);
    s_pixYUV420 = ff.av_get_pix_fmt("yuv420p");
    s_pixYUVJ420 = ff.av_get_pix_fmt("yuvj420p");
    s_pixYUVA420 = ff.av_get_pix_fmt("yuva420p");
    s_smpFLT = ff.av_get_sample_fmt("flt");
    s_smpFLTP = ff.av_get_sample_fmt("fltp");
    s_smpS16 = ff.av_get_sample_fmt("s16");
    s_smpS16P = ff.av_get_sample_fmt("s16p");
    v = ff.avformat_version();
    Port_Log("ffmpeg: FFmpeg %d (libavformat %u.%u.%u, libavcodec %u.%u.%u, libavutil %u.%u.%u)%s%s",
             (int)(v >> 16) - 54, v >> 16, (v >> 8) & 0xFF, v & 0xFF, ff.avcodec_version() >> 16,
             (ff.avcodec_version() >> 8) & 0xFF, ff.avcodec_version() & 0xFF, ff.avutil_version() >> 16,
             (ff.avutil_version() >> 8) & 0xFF, ff.avutil_version() & 0xFF, env != NULL && *env ? " from " : "",
             env != NULL && *env ? env : "");
    SDL_free(base);
    SDL_free(sibling);
    s_state = 1;
    return 1;
}

int Bink_Version(void) {
    return s_state > 0 ? (int)(ff.avformat_version() >> 16) - 54 : 0;
}

/* ---- reading through the game's SDL_IOStream ---------------------------- */

static int IORead(void *opaque, uint8_t *buf, int size) {
    size_t n = SDL_ReadIO(((BinkMovie *)opaque)->io, buf, (size_t)size);
    return n > 0 ? (int)n : FF_EOF;
}

static int64_t IOSeek(void *opaque, int64_t offset, int whence) {
    SDL_IOStream *io = ((BinkMovie *)opaque)->io;

    whence &= ~FF_SEEK_FORCE;
    if (whence == FF_SEEK_SIZE) {
        return SDL_GetIOSize(io);
    }
    return SDL_SeekIO(io, offset, whence == SEEK_CUR ? SDL_IO_SEEK_CUR : whence == SEEK_END ? SDL_IO_SEEK_END : SDL_IO_SEEK_SET);
}

/* ---- the movie ------------------------------------------------------------ */

void Bink_Close(BinkMovie *m) {
    int i;

    if (m == NULL) {
        return;
    }
    for (i = 0; i <= VIDEO_AHEAD; i++) {
        if (m->queue[i] != NULL) {
            ff.av_packet_free(&m->queue[i]);
        }
    }
    if (m->pkt != NULL) {
        ff.av_packet_free(&m->pkt);
    }
    if (m->frame != NULL) {
        ff.av_frame_free(&m->frame);
    }
    if (m->vdec != NULL) {
        ff.avcodec_free_context(&m->vdec);
    }
    if (m->adec != NULL) {
        ff.avcodec_free_context(&m->adec);
    }
    if (m->fmt != NULL) {
        ff.avformat_close_input(&m->fmt);   /* leaves a custom pb alone */
    }
    if (m->pb != NULL) {
        ff.av_free(((FFIOHead *)m->pb)->buffer);
        ff.avio_context_free(&m->pb);
    }
    if (m->io != NULL) {
        SDL_CloseIO(m->io);
    }
    SDL_free(m->pcm);
    SDL_free(m);
}

static void *OpenDecoder(BinkMovie *m, int index, const void *codec) {
    FFStreamHead *st = (FFStreamHead *)((FFFormatHead *)m->fmt)->streams[index];
    void *dec = ff.avcodec_alloc_context3(codec);

    if (dec == NULL || ff.avcodec_parameters_to_context(dec, st->codecpar) < 0 || ff.avcodec_open2(dec, codec, NULL) < 0) {
        if (dec != NULL) {
            ff.avcodec_free_context(&dec);
        }
        return NULL;
    }
    return dec;
}

BinkMovie *Bink_Open(SDL_IOStream *io, const char *name) {
    BinkMovie *m;
    const void *vcodec = NULL, *acodec = NULL;
    unsigned char *buf;
    FFStreamHead *vst;
    int i;

    if (!Bink_Load()) {
        SDL_CloseIO(io);
        return NULL;
    }
    m = SDL_calloc(1, sizeof(*m));
    m->io = io;
    m->aIndex = -1;
    buf = ff.av_malloc(32768);
    if (buf == NULL || (m->pb = ff.avio_alloc_context(buf, 32768, 0, m, IORead, NULL, IOSeek)) == NULL) {
        ff.av_free(buf);
        Bink_Close(m);
        return NULL;
    }
    m->fmt = ff.avformat_alloc_context();
    if (m->fmt == NULL) {
        Bink_Close(m);
        return NULL;
    }
    ((FFFormatHead *)m->fmt)->pb = m->pb;
    if (ff.avformat_open_input(&m->fmt, name, NULL, NULL) < 0) {   /* frees fmt on failure */
        Port_Log("ffmpeg: %s: not a movie FFmpeg can read", name);
        m->fmt = NULL;
        Bink_Close(m);
        return NULL;
    }
    /* fills in what a header doesn't say (an MPEG program stream's picture
     * size and frame rate) by reading the first packets */
    ff.avformat_find_stream_info(m->fmt, NULL);
    m->vIndex = ff.av_find_best_stream(m->fmt, FF_MEDIA_VIDEO, -1, -1, &vcodec, 0);
    if (m->vIndex < 0 || vcodec == NULL || (m->vdec = OpenDecoder(m, m->vIndex, vcodec)) == NULL) {
        Port_Log("ffmpeg: %s: no video FFmpeg can decode", name);
        Bink_Close(m);
        return NULL;
    }
    vst = (FFStreamHead *)((FFFormatHead *)m->fmt)->streams[m->vIndex];
    ff.av_opt_get_image_size(m->vdec, "video_size", 0, &m->width, &m->height);
    /* Bink's time base is its frame clock; MPEG's is 90 kHz, with the rate
     * in avg_frame_rate */
    m->fps = vst->tbNum > 0 && vst->tbDen > 0 ? (double)vst->tbDen / vst->tbNum : 0.0;
    if ((m->fps < 1.0 || m->fps > 120.0) && vst->avgNum > 0 && vst->avgDen > 0) {
        m->fps = (double)vst->avgNum / vst->avgDen;
    }
    if (m->fps < 1.0 || m->fps > 120.0) {
        m->fps = 29.97;   /* the GameCube movies' */
    }
    if (vst->tbNum > 0 && vst->tbDen > 0) {
        double tb = (double)vst->tbNum / vst->tbDen;
        m->start = vst->start_time != (int64_t)0x8000000000000000ULL ? vst->start_time * tb : 0.0;
        if (vst->nb_frames > 0) {
            m->frames = (int)vst->nb_frames;
        } else if (vst->duration > 0) {
            m->frames = (int)(vst->duration * tb * m->fps + 0.5);
        }
    }
    i = ff.av_find_best_stream(m->fmt, FF_MEDIA_AUDIO, -1, m->vIndex, &acodec, 0);
    if (i >= 0 && acodec != NULL && (m->adec = OpenDecoder(m, i, acodec)) != NULL) {
        FFChannelLayout cl;
        int64_t rate = 0;

        SDL_zero(cl);
        ff.av_opt_get_int(m->adec, "ar", 0, &rate);
        if (ff.av_opt_get_chlayout(m->adec, "ch_layout", 0, &cl) >= 0) {
            m->channels = cl.nb_channels;
            ff.av_channel_layout_uninit(&cl);
        }
        m->rate = (int)rate;
        if (m->channels < 1 || m->channels > 8 || m->rate < 4000 || m->rate > 192000) {
            ff.avcodec_free_context(&m->adec);
            m->channels = m->rate = 0;
        } else {
            m->aIndex = i;
        }
    }
    m->pkt = ff.av_packet_alloc();
    m->frame = ff.av_frame_alloc();
    for (i = 0; i <= VIDEO_AHEAD; i++) {
        m->queue[i] = ff.av_packet_alloc();
    }
    if (m->pkt == NULL || m->frame == NULL || m->queue[VIDEO_AHEAD] == NULL || m->width <= 0 || m->height <= 0) {
        Bink_Close(m);
        return NULL;
    }
    return m;
}

void Bink_Info(const BinkMovie *m, int *width, int *height, double *fps, int *frames, int *channels, int *rate) {
    *width = m->width;
    *height = m->height;
    *fps = m->fps;
    *frames = m->frames;
    *channels = m->channels;
    *rate = m->rate;
}

double Bink_StartTime(const BinkMovie *m) { return m->start; }

void Bink_SetAudio(BinkMovie *m, BinkAudioFn fn, void *user) {
    m->audioFn = fn;
    m->audioUser = user;
}

/* A decoded audio frame, interleaved as float, to the audio function. */
static void EmitAudio(BinkMovie *m) {
    FFFrameHead *f = (FFFrameHead *)m->frame;
    int n = f->nb_samples, ch = m->channels, c, i;
    size_t need = (size_t)n * ch;

    if (n <= 0 || m->audioFn == NULL) {
        return;
    }
    if (need > m->pcmCap) {
        m->pcmCap = need;
        m->pcm = SDL_realloc(m->pcm, need * sizeof(float));
    }
    if (f->format == s_smpFLTP) {
        for (c = 0; c < ch; c++) {
            const float *s = (const float *)f->extended_data[c];
            for (i = 0; i < n; i++) {
                m->pcm[(size_t)i * ch + c] = s[i];
            }
        }
    } else if (f->format == s_smpFLT) {
        SDL_memcpy(m->pcm, f->extended_data[0], need * sizeof(float));
    } else if (f->format == s_smpS16P) {
        for (c = 0; c < ch; c++) {
            const int16_t *s = (const int16_t *)f->extended_data[c];
            for (i = 0; i < n; i++) {
                m->pcm[(size_t)i * ch + c] = s[i] / 32768.0f;
            }
        }
    } else if (f->format == s_smpS16) {
        const int16_t *s = (const int16_t *)f->extended_data[0];
        for (i = 0; i < (int)need; i++) {
            m->pcm[i] = s[i] / 32768.0f;
        }
    } else {
        if (!m->badAudio) {
            Port_Log("ffmpeg: audio sample format %d not handled; the movie plays silent", f->format);
            m->badAudio = 1;
        }
        return;
    }
    m->audioFn(m->audioUser, m->pcm, n);
}

static void DecodeAudio(BinkMovie *m, void *pkt) {
    if (ff.avcodec_send_packet(m->adec, pkt) < 0) {
        return;
    }
    while (ff.avcodec_receive_frame(m->adec, m->frame) == 0) {
        EmitAudio(m);
        ff.av_frame_unref(m->frame);
    }
}

/* Reads one packet: video into the queue, audio decoded at once. 0 at the end. */
static int ReadPacket(BinkMovie *m) {
    int idx;

    if (m->eof) {
        return 0;
    }
    if (ff.av_read_frame(m->fmt, m->pkt) < 0) {
        m->eof = 1;
        if (m->adec != NULL) {
            DecodeAudio(m, NULL);
        }
        return 0;
    }
    idx = ((FFPacketHead *)m->pkt)->stream_index;
    if (idx == m->vIndex && m->qCount <= VIDEO_AHEAD) {
        ff.av_packet_move_ref(m->queue[(m->qHead + m->qCount) % (VIDEO_AHEAD + 1)], m->pkt);
        m->qCount++;
    } else if (idx == m->aIndex) {
        DecodeAudio(m, m->pkt);
    }
    ff.av_packet_unref(m->pkt);
    return 1;
}

static u8 Clamp8(int v) { return (u8)(v < 0 ? 0 : v > 255 ? 255 : v); }

/* 4:2:0 planes to RGBA, top row first: BT.601, video range (Bink's), or full
 * range for yuvj420p. Chroma is taken from the 2x2 block a pixel lies in. */
static void ToRGBA(const FFFrameHead *f, int full, u8 *rgba, int width, int height) {
    int x, y, w = f->width < width ? f->width : width, h = f->height < height ? f->height : height;
    int ky = full ? 256 : 298, kr = full ? 359 : 409, kgu = full ? 88 : 100, kgv = full ? 183 : 208,
        kb = full ? 454 : 516, y0 = full ? 0 : 16;

    for (y = 0; y < h; y++) {
        const u8 *py = f->data[0] + (size_t)y * f->linesize[0];
        const u8 *pu = f->data[1] + (size_t)(y >> 1) * f->linesize[1];
        const u8 *pv = f->data[2] + (size_t)(y >> 1) * f->linesize[2];
        u8 *o = rgba + (size_t)y * width * 4;

        for (x = 0; x < w; x++, o += 4) {
            int c = ky * (py[x] - y0) + 128, d = pu[x >> 1] - 128, e = pv[x >> 1] - 128;
            o[0] = Clamp8((c + kr * e) >> 8);
            o[1] = Clamp8((c - kgu * d - kgv * e) >> 8);
            o[2] = Clamp8((c + kb * d) >> 8);
            o[3] = 255;
        }
    }
}

int Bink_NextFrame(BinkMovie *m, u8 *rgba) {
    for (;;) {
        int r = ff.avcodec_receive_frame(m->vdec, m->frame);

        if (r == 0) {
            FFFrameHead *f = (FFFrameHead *)m->frame;
            if (rgba != NULL) {
                if (f->format == s_pixYUV420 || f->format == s_pixYUVA420 || f->format == s_pixYUVJ420) {
                    ToRGBA(f, f->format == s_pixYUVJ420, rgba, m->width, m->height);
                } else if (!m->badVideo) {
                    Port_Log("ffmpeg: pixel format %d not handled", f->format);
                    m->badVideo = 1;
                }
            }
            ff.av_frame_unref(m->frame);
            return 1;
        }
        if (r != FF_EAGAIN) {
            return 0;   /* the end, or an error */
        }
        while (m->qCount < VIDEO_AHEAD && ReadPacket(m)) {
        }
        if (m->qCount == 0) {
            if (m->flushed) {
                return 0;
            }
            ff.avcodec_send_packet(m->vdec, NULL);
            m->flushed = 1;
            continue;
        }
        r = ff.avcodec_send_packet(m->vdec, m->queue[m->qHead]);
        if (r == FF_EAGAIN) {
            continue;   /* keep the packet until a frame has been taken */
        }
        ff.av_packet_unref(m->queue[m->qHead]);
        m->qHead = (m->qHead + 1) % (VIDEO_AHEAD + 1);
        m->qCount--;
        /* a packet the decoder refused is dropped, the next one tried */
    }
}

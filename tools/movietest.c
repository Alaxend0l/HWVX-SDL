/* movietest: the movies decoded by FFmpeg loaded at run time (bink.c) and
 * the PS2 movies' own audio track (pss.c), checked against the ffmpeg
 * command line's decode.
 *
 *   movietest [FILE [FRAMES [AUDIO_REF]]]
 *
 * Decodes the first FRAMES (default 60) video frames of FILE through
 * Bink_Open/Bink_NextFrame, as the game does, and compares each picture with
 * `ffmpeg -i FILE -frames:v FRAMES -f rawvideo -pix_fmt rgb24` (PSNR over
 * R, G, B; every frame must be above 40 dB). Then the audio:
 *
 * - Bink (FFmpeg decodes it): what came out with the frames against
 *   `ffmpeg -i FILE -vn -f f32le`, sample for sample.
 * - PSS (FILE ending .pss): the whole SShd/SSbd track through Pss_ReadAudio,
 *   its length against SSbd's, and, given AUDIO_REF (the same movie from
 *   another release, e.g. the GameCube's THP, decoded by ffmpeg), the
 *   correlation of the two after lining them up: each channel against the
 *   same channel must be above 0.8, and not below the crossed channels.
 *
 * FILE defaults to $HWVX_MOVIETEST_FILE, else orig/pc/FMV_/thqLogo.bik next
 * to the source tree. Exits 77 (skipped) without the file, the FFmpeg
 * libraries or the ffmpeg program: game data is never in the repository or CI. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_internal.h"
#include "bink.h"
#include "pss.h"

#define SKIP 77

void Port_Log(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
}

static float *s_audio;
static size_t s_audioLen, s_audioCap;
static int s_channels;

static void Append(const float *pcm, size_t n) {
    if (s_audioLen + n > s_audioCap) {
        s_audioCap = (s_audioLen + n) * 2;
        s_audio = realloc(s_audio, s_audioCap * sizeof(float));
    }
    memcpy(s_audio + s_audioLen, pcm, n * sizeof(float));
    s_audioLen += n;
}

static void OnAudio(void *user, const float *pcm, int frames) {
    (void)user;
    Append(pcm, (size_t)frames * s_channels);
}

/* stdout of ffmpeg run with these arguments; NULL if it can't run. */
static void *RunFFmpeg(size_t *size, const char *const *args) {
    const char *argv[32];
    SDL_Process *p;
    void *out;
    int n = 0, code = -1;

    argv[n++] = "ffmpeg";
    argv[n++] = "-nostdin";
    argv[n++] = "-v";
    argv[n++] = "error";
    /* the plain C decoders, as the game's FFmpeg (packaging/ffmpeg: --disable-x86asm) has them:
     * the assembly ones round Bink audio differently (hw_gal.bik: 1.2e-6 apart) */
    argv[n++] = "-cpuflags";
    argv[n++] = "0";
    while (*args && n < 30) {
        argv[n++] = *args++;
    }
    argv[n] = NULL;
    p = SDL_CreateProcess(argv, true);
    if (p == NULL) {
        return NULL;
    }
    out = SDL_ReadProcess(p, size, &code);
    SDL_DestroyProcess(p);
    if (code != 0) {
        SDL_free(out);
        return NULL;
    }
    return out;
}

/* Normalised correlation of channel ca of a with channel cb of b, b shifted
 * by `lag` frames (a[i] against b[i + lag]), over n frames. */
static double Corr(const float *a, const float *b, size_t na, size_t nb, int ca, int cb, long lag, size_t n) {
    double sab = 0, saa = 0, sbb = 0;
    size_t i;

    for (i = 0; i < n; i++) {
        long j = (long)i + lag;
        double x, y;
        if (i >= na || j < 0 || (size_t)j >= nb) {
            continue;
        }
        x = a[i * 2 + ca];
        y = b[(size_t)j * 2 + cb];
        sab += x * y;
        saa += x * x;
        sbb += y * y;
    }
    return saa > 0 && sbb > 0 ? sab / sqrt(saa * sbb) : 0.0;
}

/* The PSS audio track against a reference decode of the same movie. */
static int CheckPssAudio(const char *file, const char *refFile, double videoStart) {
    SDL_IOStream *io = SDL_IOFromFile(file, "rb");
    PssAudio *a = io != NULL ? Pss_OpenAudio(io) : NULL;
    int ch, rate, n, fails = 0;
    double start;
    u32 total;
    s16 buf[4096];
    float fb[4096];

    if (a == NULL) {
        printf("movietest: %s: no PSS audio track\n", file);
        return 1;
    }
    Pss_AudioInfo(a, &ch, &rate, &start, &total);
    s_channels = ch;
    while ((n = Pss_ReadAudio(a, buf, 4096 / ch)) > 0) {
        int i;
        for (i = 0; i < n * ch; i++) {
            fb[i] = buf[i] / 32768.0f;
        }
        Append(fb, (size_t)n * ch);
    }
    Pss_CloseAudio(a);
    if (SDL_getenv("HWVX_MOVIETEST_WAV") != NULL) {   /* the track as read, to listen to */
        SDL_AudioSpec spec = { SDL_AUDIO_F32, ch, rate };
        SDL_IOStream *w = SDL_IOFromFile(SDL_getenv("HWVX_MOVIETEST_WAV"), "wb");
        u8 hdr[44] = "RIFF....WAVEfmt ";
        u32 bytes = (u32)(s_audioLen * sizeof(float));
        (void)spec;
        if (w != NULL) {
            SDL_WriteU32LE(w, 0);
            SDL_WriteIO(w, hdr, 0);
            SDL_SeekIO(w, 0, SDL_IO_SEEK_SET);
            SDL_WriteIO(w, "RIFF", 4);
            SDL_WriteU32LE(w, 36 + bytes);
            SDL_WriteIO(w, "WAVEfmt ", 8);
            SDL_WriteU32LE(w, 16);
            SDL_WriteU16LE(w, 3);   /* IEEE float */
            SDL_WriteU16LE(w, (Uint16)ch);
            SDL_WriteU32LE(w, (Uint32)rate);
            SDL_WriteU32LE(w, (Uint32)rate * ch * 4);
            SDL_WriteU16LE(w, (Uint16)(ch * 4));
            SDL_WriteU16LE(w, 32);
            SDL_WriteIO(w, "data", 4);
            SDL_WriteU32LE(w, bytes);
            SDL_WriteIO(w, s_audio, bytes);
            SDL_CloseIO(w);
        }
    }
    printf("movietest: PSS audio: %d ch %d Hz, starts at %.4f s (video %.4f s), %zu frames read of SSbd's %u (%.2f s)\n",
           ch, rate, start, videoStart, s_audioLen / ch, total, (double)total / rate);
    if (s_audioLen / ch != total || ch != 2) {
        fails++;
    }
    if (refFile != NULL && ch == 2) {
        const char *args[] = { "-i", refFile, "-vn", "-ac", "2", "-ar", "48000", "-f", "f32le", "-", NULL };
        size_t refSize = 0, na = s_audioLen / 2, nb, win;
        float *ref = RunFFmpeg(&refSize, args);
        double best = -2, ll, rr, lr, rl;
        long lag, bestLag = 0, step;

        if (ref == NULL) {
            printf("movietest: ffmpeg can't decode %s\n", refFile);
            return fails + 1;
        }
        nb = refSize / sizeof(float) / 2;
        win = na < nb ? na : nb;
        if (win > (size_t)rate * 6) {
            win = (size_t)rate * 6;
        }
        /* coarse then fine search for the offset, within 8 seconds (a cut may differ in its start) */
        for (step = 128, lag = -8L * rate; lag <= 8L * rate; lag += step) {
            double c = Corr(s_audio, ref, na, nb, 0, 0, lag, win);
            if (c > best) {
                best = c;
                bestLag = lag;
            }
        }
        for (lag = bestLag - 128; lag <= bestLag + 128; lag++) {
            double c = Corr(s_audio, ref, na, nb, 0, 0, lag, win);
            if (c > best) {
                best = c;
                bestLag = lag;
            }
        }
        win = na < nb ? na : nb;
        ll = Corr(s_audio, ref, na, nb, 0, 0, bestLag, win);
        rr = Corr(s_audio, ref, na, nb, 1, 1, bestLag, win);
        lr = Corr(s_audio, ref, na, nb, 0, 1, bestLag, win);
        rl = Corr(s_audio, ref, na, nb, 1, 0, bestLag, win);
        printf("movietest: PSS audio against %s: offset %ld frames (%.1f ms), over %.2f s: L-L %.4f, R-R %.4f, "
               "L-R %.4f, R-L %.4f\n", refFile, bestLag, bestLag * 1000.0 / rate, (double)win / rate, ll, rr, lr, rl);
        if (ll < 0.8 || rr < 0.8 || lr > ll + 1e-3 || rl > rr + 1e-3) {
            fails++;
        }
        SDL_free(ref);
    }
    return fails;
}

int main(int argc, char **argv) {
    char deflt[1024], framesArg[16];
    const char *file = argc > 1 ? argv[1] : SDL_getenv("HWVX_MOVIETEST_FILE");
    const char *audioRef = argc > 3 ? argv[3] : NULL;
    int frames = argc > 2 ? atoi(argv[2]) : 60;
    int w, h, nframes, rate, i, fails = 0, pss;
    double fps, minPsnr = 1e9, sumPsnr = 0;
    size_t refSize = 0, aRefSize = 0, frameSize, k, same = 0, cmpLen, len;
    u8 *ref, *rgba;
    float *aref;
    double maxDiff = 0;
    SDL_IOStream *io;
    BinkMovie *m;

    if (file == NULL) {
        SDL_snprintf(deflt, sizeof(deflt), "%s/../orig/pc/FMV_/thqLogo.bik", HWVX_SOURCE_DIR);
        file = deflt;
    }
    len = strlen(file);
    pss = len > 4 && SDL_strcasecmp(file + len - 4, ".pss") == 0;
    if (frames <= 0) {
        frames = 60;
    }
    io = SDL_IOFromFile(file, "rb");
    if (io == NULL) {
        printf("movietest: %s not found; skipped\n", file);
        return SKIP;
    }
    if (!Bink_Load()) {
        SDL_CloseIO(io);
        printf("movietest: no FFmpeg libraries; skipped\n");
        return SKIP;
    }
    m = Bink_Open(io, file);
    if (m == NULL) {
        printf("movietest: %s: can't open\n", file);
        return 1;
    }
    Bink_Info(m, &w, &h, &fps, &nframes, &s_channels, &rate);
    printf("movietest: %s: %dx%d, %d frames at %.3f fps, audio %d ch %d Hz\n", file, w, h, nframes, fps, s_channels, rate);
    Bink_SetAudio(m, OnAudio, NULL);

    SDL_snprintf(framesArg, sizeof(framesArg), "%d", frames);
    {
        const char *args[] = { "-i", file, "-frames:v", framesArg, "-fps_mode", "passthrough", "-f", "rawvideo",
                               "-pix_fmt", "rgb24", "-", NULL };
        ref = RunFFmpeg(&refSize, args);
    }
    if (ref == NULL) {
        printf("movietest: the ffmpeg program didn't run; skipped\n");
        Bink_Close(m);
        return SKIP;
    }
    frameSize = (size_t)w * h * 3;
    if (refSize < frameSize * frames) {
        frames = (int)(refSize / frameSize);
    }
    rgba = malloc((size_t)w * h * 4);
    for (i = 0; i < frames; i++) {
        const u8 *r = ref + frameSize * i;
        double se = 0, psnr;
        size_t p;

        if (!Bink_NextFrame(m, rgba)) {
            printf("movietest: frame %d: decoding ended early\n", i);
            fails++;
            break;
        }
        for (p = 0; p < (size_t)w * h; p++) {
            int c;
            for (c = 0; c < 3; c++) {
                double d = (double)rgba[p * 4 + c] - r[p * 3 + c];
                se += d * d;
            }
        }
        se /= (double)w * h * 3;
        psnr = se > 0 ? 10.0 * log10(255.0 * 255.0 / se) : 99.0;
        sumPsnr += psnr;
        if (psnr < minPsnr) {
            minPsnr = psnr;
        }
        if (psnr <= 40.0) {
            printf("movietest: frame %d: PSNR %.2f dB\n", i, psnr);
            fails++;
        }
    }
    printf("movietest: video: %d frames, PSNR min %.2f dB, mean %.2f dB (ffmpeg rgb24)\n", frames, minPsnr,
           frames ? sumPsnr / frames : 0.0);

    if (pss) {
        free(s_audio);
        s_audio = NULL;
        s_audioLen = s_audioCap = 0;
        fails += CheckPssAudio(file, audioRef, Bink_StartTime(m));
    } else if (s_channels > 0) {
        const char *args[] = { "-i", file, "-vn", "-f", "f32le", "-", NULL };
        aref = RunFFmpeg(&aRefSize, args);
        if (aref == NULL) {
            printf("movietest: ffmpeg gave no audio\n");
            fails++;
        } else {
            size_t refN = aRefSize / sizeof(float);
            cmpLen = s_audioLen < refN ? s_audioLen : refN;
            for (k = 0; k < cmpLen; k++) {
                double d = fabs((double)s_audio[k] - aref[k]);
                same += d == 0.0;
                if (d > maxDiff) {
                    maxDiff = d;
                }
            }
            printf("movietest: audio: %zu samples (%.2f s) decoded with the frames, %zu identical to ffmpeg's, max "
                   "difference %g\n", cmpLen, s_channels ? (double)cmpLen / s_channels / rate : 0.0, same, maxDiff);
            if (cmpLen == 0 || maxDiff > 1e-6) {
                fails++;
            }
            SDL_free(aref);
        }
    }
    Bink_Close(m);
    SDL_free(ref);
    free(rgba);
    free(s_audio);
    printf("movietest: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}

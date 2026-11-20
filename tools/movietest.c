/* movietest: the movie decoders that use FFmpeg loaded at run time (bink.c),
 * checked against the ffmpeg command line's own decode of the same file.
 *
 *   movietest [FILE [FRAMES]]
 *
 * Decodes the first FRAMES (default 60) video frames of FILE through
 * Bink_Open/Bink_NextFrame, as the game does, and compares each picture with
 * `ffmpeg -i FILE -frames:v FRAMES -f rawvideo -pix_fmt rgb24` (PSNR over
 * R, G, B; every frame must be above 40 dB), then the audio that came out
 * with it against `ffmpeg -i FILE -vn -f f32le` (sample for sample). FILE
 * defaults to $HWVX_MOVIETEST_FILE, else orig/pc/FMV_/thqLogo.bik next to
 * the source tree. Exits 77 (skipped) without the file, the FFmpeg libraries
 * or the ffmpeg program: game data is never in the repository or CI. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_internal.h"
#include "bink.h"

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

static void OnAudio(void *user, const float *pcm, int frames) {
    size_t n = (size_t)frames * s_channels;

    (void)user;
    if (s_audioLen + n > s_audioCap) {
        s_audioCap = (s_audioLen + n) * 2;
        s_audio = realloc(s_audio, s_audioCap * sizeof(float));
    }
    memcpy(s_audio + s_audioLen, pcm, n * sizeof(float));
    s_audioLen += n;
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

int main(int argc, char **argv) {
    char deflt[1024], framesArg[16];
    const char *file = argc > 1 ? argv[1] : SDL_getenv("HWVX_MOVIETEST_FILE");
    int frames = argc > 2 ? atoi(argv[2]) : 60;
    int w, h, nframes, rate, i, fails = 0;
    double fps, minPsnr = 1e9, sumPsnr = 0;
    size_t refSize = 0, aRefSize = 0, frameSize, k, same = 0, cmpLen;
    u8 *ref, *rgba;
    float *aref;
    double maxDiff = 0;
    SDL_IOStream *io;
    BinkMovie *m;

    if (file == NULL) {
        SDL_snprintf(deflt, sizeof(deflt), "%s/../orig/pc/FMV_/thqLogo.bik", HWVX_SOURCE_DIR);
        file = deflt;
    }
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
        const char *args[] = { "-i", file, "-frames:v", framesArg, "-f", "rawvideo", "-pix_fmt", "rgb24", "-", NULL };
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

    if (s_channels > 0) {
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

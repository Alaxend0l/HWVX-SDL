/* tools/streamfuzz.c: the stream player (src/port/musicstream.c) on damaged
 * files, under AddressSanitizer and UBSan (tools/stream_fuzz.sh builds and
 * runs it). Not a ctest: it needs files made with ffmpeg.
 *
 * streamfuzz FILE...: for each file, as a mod's only track for a song,
 * PortStream_Create; when it is taken, 20 s of 48 kHz output are mixed
 * (refilled as the AX thread does, with a stop / restart halfway), then the
 * stream is destroyed and faded out. Prints one line per file: "fallback"
 * (the game's own song would play), or "played" with the frames that weren't
 * silent and the peak. Any memory error aborts under the sanitizers. */
#include "../src/port/musicstream.c"

u32 g_musicVol = 0x7FFF;
static const char *s_file;

void Port_Log(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    fputs("  log: ", stdout);
    vprintf(fmt, ap);
    fputc('\n', stdout);
    va_end(ap);
}

int Port_ModsActive(void) { return 1; }

int Port_ModsMusic(const char *name, unsigned type, unsigned id, PortModMusic **out) {
    PortModMusic *t = SDL_calloc(1, sizeof(*t));

    (void)name;
    (void)type;
    (void)id;
    t->path = SDL_strdup(s_file);
    t->loopStart = t->loopEnd = -1;
    t->loop = 1;
    t->volume = 1.0f;
    *out = t;
    return 1;
}

void Port_ModsMusicFree(PortModMusic *tracks, int count) {
    int i;

    for (i = 0; tracks != NULL && i < count; i++) {
        SDL_free(tracks[i].path);
    }
    SDL_free(tracks);
}

int main(int argc, char **argv) {
    static float out[160 * 2];
    int a, f, k;

    setvbuf(stdout, NULL, _IOLBF, 0);

    for (a = 1; a < argc; a++) {
        long loud = 0;
        float peak = 0.0f;

        s_file = argv[a];
        PortStream_SetPack("test", 4, 0);
        if (!PortStream_Create()) {
            printf("%s: fallback\n", argv[a]);
            continue;
        }
        PortStream_SetVol();
        PortStream_SetState(1);
        for (f = 0; f < 20 * 300; f++) {      /* 300 AX frames a second */
            if (f == 10 * 300) {
                PortStream_SetState(3);
                PortStream_SetState(1);
            }
            SDL_memset(out, 0, sizeof(out));
            PortStream_Mix(out, 160);
            if (f % 16 == 0) {
                PortStream_Refill();
            }
            for (k = 0; k < 320; k++) {
                if (SDL_fabsf(out[k]) > 1e-4f) {
                    loud++;
                }
                peak = SDL_max(peak, SDL_fabsf(out[k]));
            }
        }
        PortStream_Destroy();
        for (f = 0; f < 400; f++) {
            PortStream_Mix(out, 160);
            PortStream_Refill();
        }
        printf("%s: played, %ld loud samples, peak %.3f\n", argv[a], loud, peak);
    }
    return 0;
}

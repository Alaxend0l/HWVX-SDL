/* hwvx-audiodec: a sound file (WAV, FLAC, MP3, Ogg Vorbis) to a PCM16 WAV.
 *
 *     hwvx-audiodec IN OUT.wav [--mono] [--rate HZ]
 *
 * OUT ("-": stdout, the JSON then on stderr) holds every channel at the file's rate unless --mono (the channels'
 * mean) or --rate (windowed-sinc, mono only) say otherwise; a loop from the
 * file's tags becomes OUT's `smpl` chunk, scaled to the rate. One line of
 * JSON on stdout describes it. tools/xdl_media.py runs this for anything but
 * a plain WAV (HWVX_AUDIODEC, or found next to the game). */
#include "audiodec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

static void Put32(FILE *f, uint32_t v) {
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };

    fwrite(b, 1, 4, f);
}

static void Put16(FILE *f, uint32_t v) {
    uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };

    fwrite(b, 1, 2, f);
}

static int WriteWav(const char *path, const int16_t *pcm, uint32_t frames, uint32_t ch, uint32_t rate,
                    int64_t loopStart, int64_t loopEnd) {
    FILE *f = strcmp(path, "-") == 0 ? stdout : fopen(path, "wb");
    uint32_t data = frames * ch * 2, smpl = loopStart >= 0 ? 36 + 24 : 0, i;

    if (f == NULL) {
        return 0;
    }
    fwrite("RIFF", 1, 4, f);
    Put32(f, 4 + 8 + 16 + 8 + data + (smpl ? 8 + smpl : 0));
    fwrite("WAVEfmt ", 1, 8, f);
    Put32(f, 16);
    Put16(f, 1);
    Put16(f, ch);
    Put32(f, rate);
    Put32(f, rate * ch * 2);
    Put16(f, ch * 2);
    Put16(f, 16);
    fwrite("data", 1, 4, f);
    Put32(f, data);
    for (i = 0; i < frames * ch; i++) {
        Put16(f, (uint16_t)pcm[i]);
    }
    if (smpl) {
        fwrite("smpl", 1, 4, f);
        Put32(f, smpl);
        for (i = 0; i < 7; i++) {
            Put32(f, i == 2 ? 1000000000u / rate : 0);  /* manufacturer, product, period, note, pitch, smpte x2 */
        }
        Put32(f, 1);        /* loops */
        Put32(f, 0);        /* sampler data */
        Put32(f, 0);        /* loop id, type (forward), start, end (inclusive), fraction, play count */
        Put32(f, 0);
        Put32(f, (uint32_t)loopStart);
        Put32(f, (uint32_t)((loopEnd >= 0 ? loopEnd : frames) - 1));
        Put32(f, 0);
        Put32(f, 0);
    }
    return f == stdout ? fflush(f) == 0 : fclose(f) == 0;
}

int main(int argc, char **argv) {
    AudioDecoded a;
    char err[256];
    int mono = 0, i;
    uint32_t rate = 0, n, ch, outRate;
    int16_t *pcm, *m = NULL, *r = NULL;
    int64_t ls, le;

    if (argc < 3) {
        fprintf(stderr, "usage: %s IN OUT.wav [--mono] [--rate HZ]\n", argv[0]);
        return 2;
    }
    for (i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--mono") == 0) {
            mono = 1;
        } else if (strcmp(argv[i], "--rate") == 0 && i + 1 < argc) {
            rate = (uint32_t)strtoul(argv[++i], NULL, 10);
            mono = 1;
        } else {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            return 2;
        }
    }
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (!AudioDec_File(argv[1], &a, err, sizeof(err))) {
        fprintf(stderr, "%s: %s\n", argv[1], err);
        return 1;
    }
    pcm = a.pcm;
    n = a.frames;
    ch = a.channels;
    outRate = a.rate;
    ls = a.loopStart;
    le = a.loopEnd;
    if (mono) {
        m = AudioDec_Mono(&a);
        pcm = m;
        ch = 1;
    }
    if (pcm != NULL && rate && rate != a.rate) {
        if (rate < 1000 || rate > 384000) {
            fprintf(stderr, "--rate %u is out of range\n", rate);
            return 2;
        }
        r = AudioDec_Resample(pcm, n, a.rate, rate, &n);
        pcm = r;
        outRate = rate;
        ls = AudioDec_ScalePos(ls, a.rate, rate);
        le = AudioDec_ScalePos(le, a.rate, rate);
        if (ls >= (int64_t)n) {
            ls = le = -1;
        }
        if (le > (int64_t)n) {
            le = -1;
        }
    }
    if (pcm == NULL || !WriteWav(argv[2], pcm, n, ch, outRate, ls, le)) {
        fprintf(stderr, "%s: can't be written\n", argv[2]);
        return 1;
    }
    fprintf(strcmp(argv[2], "-") == 0 ? stderr : stdout, "{\"format\": \"%s\", \"rate\": %u, \"channels\": %u, \"frames\": %u, \"source_rate\": %u, "
           "\"source_channels\": %u, \"loop_start\": %lld, \"loop_end\": %lld}\n",
           a.format, outRate, ch, n, a.rate, a.channels, (long long)ls, (long long)le);
    free(m);
    free(r);
    AudioDec_Free(&a);
    return 0;
}

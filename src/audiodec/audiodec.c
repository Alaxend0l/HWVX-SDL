/* Compressed audio to PCM16 (audiodec.h). */
#include "audiodec.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MA_NO_DEVICE_IO
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_GENERATION
#define MA_NO_THREADING
#define MA_NO_ENCODING
#define MA_NO_VORBIS        /* stb_vorbis below, called directly */
#define MA_API static
#define MINIAUDIO_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "../../third_party/miniaudio/miniaudio.h"
#undef L
#undef C
#undef R
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#include "../../third_party/stb_vorbis/stb_vorbis.c"
#undef L
#undef C
#undef R
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static void Err(char *err, size_t n, const char *msg) {
    if (err != NULL && n > 0) {
        snprintf(err, n, "%s", msg);
    }
}

static uint32_t Le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t Be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

/* ---- loop tags ------------------------------------------------------------ */

typedef struct Tags {
    int64_t start, length, end;     /* -1: absent */
} Tags;

static int64_t Num(const char *s, size_t n) {
    int64_t v = 0;
    size_t i = 0;

    while (i < n && (s[i] == ' ' || s[i] == '\t')) {
        i++;
    }
    if (i == n || s[i] < '0' || s[i] > '9') {
        return -1;
    }
    for (; i < n && s[i] >= '0' && s[i] <= '9'; i++) {
        v = v * 10 + (s[i] - '0');
        if (v > 0x7FFFFFFF) {
            return -1;
        }
    }
    return v;
}

static int KeyIs(const char *s, size_t n, const char *key) {
    size_t k = strlen(key), i;

    if (n < k) {
        return 0;
    }
    for (i = 0; i < k; i++) {
        char c = s[i];

        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 32);
        }
        if (c != key[i]) {
            return 0;
        }
    }
    return 1;
}

/* "KEY=value" (a Vorbis comment), or key and value apart (ID3 TXXX). */
static void Tag(Tags *t, const char *key, size_t kn, const char *val, size_t vn) {
    if (kn == 9 && KeyIs(key, kn, "LOOPSTART")) {
        t->start = Num(val, vn);
    } else if (kn == 10 && KeyIs(key, kn, "LOOPLENGTH")) {
        t->length = Num(val, vn);
    } else if (kn == 7 && KeyIs(key, kn, "LOOPEND")) {
        t->end = Num(val, vn);
    }
}

static void Comment(Tags *t, const char *s, size_t n) {
    const char *eq = memchr(s, '=', n);

    if (eq != NULL) {
        Tag(t, s, (size_t)(eq - s), eq + 1, n - (size_t)(eq - s) - 1);
    }
}

/* A Vorbis comment block (after its packet type): vendor, count, comments. */
static void VorbisComments(Tags *t, const uint8_t *p, size_t n) {
    size_t o = 0;
    uint32_t len, count, i;

    if (n < 8) {
        return;
    }
    len = Le32(p);
    if (len > n - 8) {
        return;
    }
    o = 4 + len;
    count = Le32(p + o);
    o += 4;
    for (i = 0; i < count && o + 4 <= n; i++) {
        len = Le32(p + o);
        o += 4;
        if (len > n - o) {
            return;
        }
        Comment(t, (const char *)p + o, len);
        o += len;
    }
}

static void WavTags(Tags *t, const uint8_t *p, size_t n) {
    size_t o = 12;

    while (o + 8 <= n) {
        uint32_t id = Be32(p + o), len = Le32(p + o + 4);

        if (len > n - o - 8) {
            return;
        }
        if (id == 0x736D706C /* smpl */ && len >= 36 + 24 && Le32(p + o + 8 + 28) > 0) {
            const uint8_t *lp = p + o + 8 + 36;     /* the first loop: id, type, start, end (inclusive) */

            t->start = Le32(lp + 8);
            t->end = (int64_t)Le32(lp + 12) + 1;
            return;
        }
        o += 8 + len + (len & 1);
    }
}

static void FlacTags(Tags *t, const uint8_t *p, size_t n) {
    size_t o = 4;

    while (o + 4 <= n) {
        int last = p[o] & 0x80, type = p[o] & 0x7F;
        uint32_t len = (uint32_t)p[o + 1] << 16 | (uint32_t)p[o + 2] << 8 | p[o + 3];

        o += 4;
        if (len > n - o) {
            return;
        }
        if (type == 4) {
            VorbisComments(t, p + o, len);
        }
        o += len;
        if (last) {
            return;
        }
    }
}

/* The comment header's packet; it starts on the second page and, past a
 * page's 255 segments, would continue on the next (not followed: no tags). */
static void OggTags(Tags *t, const uint8_t *p, size_t n) {
    size_t o = 0, limit = n < (1u << 20) ? n : (1u << 20);

    while (o + 27 <= limit) {
        uint32_t segs, body = 0, i;

        if (memcmp(p + o, "OggS", 4) != 0) {
            return;
        }
        segs = p[o + 26];
        if (o + 27 + segs > limit) {
            return;
        }
        for (i = 0; i < segs; i++) {
            body += p[o + 27 + i];
        }
        if (o + 27 + segs + body > n) {
            return;
        }
        if (body >= 7 && memcmp(p + o + 27 + segs, "\x03vorbis", 7) == 0) {
            VorbisComments(t, p + o + 27 + segs + 7, body - 7);
            return;
        }
        o += 27 + segs + body;
    }
}

static uint32_t Syncsafe(const uint8_t *p) {
    return (uint32_t)(p[0] & 0x7F) << 21 | (uint32_t)(p[1] & 0x7F) << 14 | (uint32_t)(p[2] & 0x7F) << 7 | (p[3] & 0x7F);
}

/* ID3v2.3/2.4 TXXX frames: encoding, description, 0, value. */
static void Id3Tags(Tags *t, const uint8_t *p, size_t n) {
    size_t o = 10, end;
    int ver;

    if (n < 10 || memcmp(p, "ID3", 3) != 0 || (p[3] != 3 && p[3] != 4) || (p[5] & 0x80) /* unsynchronised */) {
        return;
    }
    ver = p[3];
    end = 10 + (size_t)Syncsafe(p + 6);
    if (end > n) {
        end = n;
    }
    if (p[5] & 0x40) {     /* extended header */
        uint32_t ext = ver == 4 ? Syncsafe(p + 10) : Be32(p + 10) + 4;

        o += ext;
    }
    while (o + 10 <= end && p[o] != 0) {
        uint32_t len = ver == 4 ? Syncsafe(p + o + 4) : Be32(p + o + 4);
        const uint8_t *f = p + o + 10;

        if (len > end - o - 10) {
            return;
        }
        if (memcmp(p + o, "TXXX", 4) == 0 && len > 2 && (f[0] == 0 || f[0] == 3)) {
            const char *d = (const char *)f + 1;
            size_t dn = strnlen(d, len - 1);

            if (dn + 2 <= len - 1) {
                const char *v = d + dn + 1;

                Tag(t, d, dn, v, strnlen(v, len - 1 - dn - 1));
            }
        }
        o += 10 + len;
    }
}

/* ---- decoding --------------------------------------------------------------- */

static int DecodeMa(const void *data, size_t size, ma_encoding_format fmt, AudioDecoded *out, char *err,
                    size_t errSize) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_s16, 0, 0);
    ma_decoder dec;
    size_t cap = 0;
    uint64_t got;

    cfg.encodingFormat = fmt;
    if (ma_decoder_init_memory(data, size, &cfg, &dec) != MA_SUCCESS) {
        Err(err, errSize, "can't be decoded");
        return 0;
    }
    out->channels = dec.outputChannels;
    out->rate = dec.outputSampleRate;
    if (out->channels == 0 || out->channels > 8 || out->rate < 1000 || out->rate > 384000) {
        ma_decoder_uninit(&dec);
        Err(err, errSize, "has an unusable channel count or rate");
        return 0;
    }
    for (;;) {
        if (out->frames + 4096 > cap) {
            int16_t *p;

            cap = cap ? cap * 2 : 65536;
            if (cap > AUDIODEC_MAX_FRAMES + 4096) {
                cap = AUDIODEC_MAX_FRAMES + 4096;
            }
            p = realloc(out->pcm, cap * out->channels * sizeof(int16_t));
            if (p == NULL) {
                ma_decoder_uninit(&dec);
                Err(err, errSize, "out of memory");
                return 0;
            }
            out->pcm = p;
        }
        if (ma_decoder_read_pcm_frames(&dec, out->pcm + (size_t)out->frames * out->channels, 4096, &got) !=
                MA_SUCCESS || got == 0) {
            break;
        }
        out->frames += (uint32_t)got;
        if (out->frames > AUDIODEC_MAX_FRAMES) {
            ma_decoder_uninit(&dec);
            Err(err, errSize, "is longer than ten minutes");
            return 0;
        }
    }
    ma_decoder_uninit(&dec);
    if (out->frames == 0) {
        Err(err, errSize, "holds no samples");
        return 0;
    }
    return 1;
}

static int DecodeOgg(const void *data, size_t size, AudioDecoded *out, char *err, size_t errSize) {
    int ch = 0, rate = 0, n;
    short *pcm = NULL;

    if (size > 0x7FFFFFFF) {
        Err(err, errSize, "is too large");
        return 0;
    }
    n = stb_vorbis_decode_memory(data, (int)size, &ch, &rate, &pcm);
    if (n <= 0 || pcm == NULL) {
        free(pcm);
        Err(err, errSize, "can't be decoded (Ogg Vorbis)");
        return 0;
    }
    if (ch <= 0 || ch > 8 || rate < 1000 || rate > 384000 || (uint32_t)n > AUDIODEC_MAX_FRAMES) {
        free(pcm);
        Err(err, errSize, ch <= 0 || ch > 8 || rate < 1000 || rate > 384000 ? "has an unusable channel count or rate"
                                                                             : "is longer than ten minutes");
        return 0;
    }
    out->pcm = pcm;
    out->frames = (uint32_t)n;
    out->channels = (uint32_t)ch;
    out->rate = (uint32_t)rate;
    return 1;
}

int AudioDec_Memory(const void *data, size_t size, AudioDecoded *out, char *err, size_t errSize) {
    const uint8_t *p = data;
    Tags t = { -1, -1, -1 };
    int ok;

    memset(out, 0, sizeof(*out));
    out->loopStart = out->loopEnd = -1;
    if (size >= 12 && memcmp(p, "RIFF", 4) == 0 && memcmp(p + 8, "WAVE", 4) == 0) {
        out->format = "wav";
        WavTags(&t, p, size);
        ok = DecodeMa(data, size, ma_encoding_format_wav, out, err, errSize);
    } else if (size >= 4 && memcmp(p, "fLaC", 4) == 0) {
        out->format = "flac";
        FlacTags(&t, p, size);
        ok = DecodeMa(data, size, ma_encoding_format_flac, out, err, errSize);
    } else if (size >= 4 && memcmp(p, "OggS", 4) == 0) {
        out->format = "ogg";
        OggTags(&t, p, size);
        ok = DecodeOgg(data, size, out, err, errSize);
    } else if (size >= 3 && (memcmp(p, "ID3", 3) == 0 || (p[0] == 0xFF && (p[1] & 0xE0) == 0xE0))) {
        out->format = "mp3";
        Id3Tags(&t, p, size);
        ok = DecodeMa(data, size, ma_encoding_format_mp3, out, err, errSize);
    } else {
        Err(err, errSize, "isn't WAV, FLAC, Ogg Vorbis or MP3");
        return 0;
    }
    if (!ok) {
        AudioDec_Free(out);
        return 0;
    }
    if (t.start >= 0 && t.start < out->frames) {
        int64_t end = t.end >= 0 ? t.end : t.length > 0 ? t.start + t.length : -1;

        out->loopStart = t.start;
        out->loopEnd = end > t.start && end < out->frames ? end : -1;
    }
    return 1;
}

int AudioDec_File(const char *path, AudioDecoded *out, char *err, size_t errSize) {
    FILE *f = fopen(path, "rb");
    long size;
    void *data;
    int ok;

    memset(out, 0, sizeof(*out));
    if (f == NULL) {
        Err(err, errSize, "can't be opened");
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) <= 0 || size > (256L << 20) || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        Err(err, errSize, "is empty or larger than 256 MB");
        return 0;
    }
    data = malloc((size_t)size);
    if (data == NULL || fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        Err(err, errSize, "can't be read");
        return 0;
    }
    fclose(f);
    ok = AudioDec_Memory(data, (size_t)size, out, err, errSize);
    free(data);
    return ok;
}

void AudioDec_Free(AudioDecoded *a) {
    free(a->pcm);
    a->pcm = NULL;
    a->frames = 0;
}

int16_t *AudioDec_Mono(const AudioDecoded *a) {
    int16_t *m = malloc((size_t)a->frames * sizeof(int16_t) + 1);
    uint32_t i, c;

    if (m == NULL) {
        return NULL;
    }
    for (i = 0; i < a->frames; i++) {
        int32_t s = 0;

        for (c = 0; c < a->channels; c++) {
            s += a->pcm[(size_t)i * a->channels + c];
        }
        /* round half away from zero, as numpy's rint differs only on .5 */
        m[i] = (int16_t)(s >= 0 ? (s + (int32_t)a->channels / 2) / (int32_t)a->channels
                                : -((-s + (int32_t)a->channels / 2) / (int32_t)a->channels));
    }
    return m;
}

int64_t AudioDec_ScalePos(int64_t pos, uint32_t srcRate, uint32_t dstRate) {
    return pos < 0 ? pos : (int64_t)((pos * (int64_t)dstRate * 2 + srcRate) / (2 * (int64_t)srcRate));
}

/* y[m] = sum_k x[k] fc sinc(fc (t - k)) w((t - k) / H), t = m src/dst, with
 * fc = 0.95 min(1, dst/src), H = 16 / fc, w a Blackman window. */
#define ZEROS 16
int16_t *AudioDec_Resample(const int16_t *in, uint32_t n, uint32_t srcRate, uint32_t dstRate, uint32_t *outN) {
    double fc = 0.95 * (dstRate < srcRate ? (double)dstRate / srcRate : 1.0), half = ZEROS / fc, step;
    uint64_t m, count = ((uint64_t)n * dstRate + srcRate / 2) / srcRate;
    int16_t *out;

    *outN = 0;
    if (srcRate == dstRate) {
        out = malloc((size_t)n * sizeof(int16_t) + 1);
        if (out != NULL) {
            memcpy(out, in, (size_t)n * sizeof(int16_t));
            *outN = n;
        }
        return out;
    }
    if (count == 0 || count > AUDIODEC_MAX_FRAMES * 8ull || srcRate == 0) {
        return NULL;
    }
    out = malloc((size_t)count * sizeof(int16_t));
    if (out == NULL) {
        return NULL;
    }
    step = (double)srcRate / dstRate;
    for (m = 0; m < count; m++) {
        double t = m * step, acc = 0;
        int64_t k0 = (int64_t)ceil(t - half), k1 = (int64_t)floor(t + half), k;

        if (k0 < 0) {
            k0 = 0;
        }
        if (k1 > (int64_t)n - 1) {
            k1 = (int64_t)n - 1;
        }
        for (k = k0; k <= k1; k++) {
            double d = t - (double)k, x = fc * d, s, u, w;

            if (fabs(d) >= half) {
                continue;
            }
            s = x == 0 ? 1.0 : sin(M_PI * x) / (M_PI * x);
            u = d / half;
            w = 0.42 + 0.5 * cos(M_PI * u) + 0.08 * cos(2 * M_PI * u);
            acc += in[k] * fc * s * w;
        }
        acc = acc < -32768 ? -32768 : acc > 32767 ? 32767 : acc;
        out[m] = (int16_t)lrint(acc);
    }
    *outN = (uint32_t)count;
    return out;
}

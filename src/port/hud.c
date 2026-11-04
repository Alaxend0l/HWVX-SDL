/* On-screen frame rate readout: HWVX_FPSHUD=1 draws a small strip at the top
 * left of the screen with the FPS now, the average, and the 1% low (the
 * average of the worst 1% of frame times), over a ring of the last few
 * thousand retraces.
 *
 * vi.c ticks it once per retrace; gx_draw.c draws it before each EFB copy,
 * where it bakes background and glyphs into one opaque RGBA image and blits
 * that through the renderer's drawImage. Being a single opaque image, both
 * backends already handle it, and it rides the copy's letterbox like any
 * other part of the frame. */
#include "port_internal.h"
#include "gx_render.h"   /* g_gxr: only drawImage is used */

static int HudOn(void) {
    static int on = -1;
    const char *e;

    if (on < 0) {
        e = SDL_getenv("HWVX_FPSHUD");
        on = e != NULL && *e == '1';
    }
    return on;
}

/* ---- frame time ring ----------------------------------------------------- */

#define HUD_RING 4096 /* 68 s at 60 fps; 1% of it is 40 frames */
static u64 s_dt[HUD_RING];       /* retrace to retrace, ns */
static u32 s_count, s_next;
static u64 s_lastNS;

void Port_HudTick(void) {
    u64 now, dt;

    if (!HudOn()) {
        return;
    }
    now = SDL_GetTicksNS();
    if (s_lastNS != 0) {
        dt = now - s_lastNS;
        /* A gap longer than a few retraces is the game not asking for frames
         * (a movie plays on its own clock, a load stalls the loop), not a slow
         * frame; HWVX_FPSLOG's log gaps say the same thing. */
        if (dt > 0 && dt < 250000000ull) {
            s_dt[s_next] = dt;
            s_next = (s_next + 1) % HUD_RING;
            if (s_count < HUD_RING) {
                s_count++;
            }
        }
    }
    s_lastNS = now;
}

/* The last `n` retraces' mean, as frames per second. */
static double MeanRecentFps(u32 n) {
    u64 sum = 0;
    u32 i;

    if (n > s_count) {
        n = s_count;
    }
    if (n == 0) {
        return 0.0;
    }
    for (i = 0; i < n; i++) {
        u32 at = (s_next - 1 - i + HUD_RING) % HUD_RING;
        sum += s_dt[at];
    }
    return (double)n * 1e9 / (double)sum;
}

static double MeanAllFps(void) {
    u64 sum = 0;
    u32 i;

    if (s_count == 0) {
        return 0.0;
    }
    for (i = 0; i < s_count; i++) {
        sum += s_dt[i];
    }
    return (double)s_count * 1e9 / (double)sum;
}

/* Average of the worst 1% of frame times in the ring, as a frame rate. */
static double Low1Fps(void) {
    u64 worst[40]; /* HUD_RING / 100, the most the insertion below holds */
    u32 k, n = 0, i;
    u64 sum = 0;

    if (s_count == 0) {
        return 0.0;
    }
    k = s_count / 100;
    if (k < 1) {
        k = 1;
    }
    if (k > 40) {
        k = 40;
    }
    for (i = 0; i < s_count; i++) {
        u64 dt = s_dt[i];
        u32 j = n < k ? n : k - 1;

        while (j > 0 && dt > worst[j - 1]) {
            if (j < k) {
                worst[j] = worst[j - 1];
            }
            j--;
        }
        if (n < k || dt > worst[k - 1]) {
            worst[j] = dt;
        }
        if (n < k) {
            n++;
        }
    }
    for (i = 0; i < n; i++) {
        sum += worst[i];
    }
    return (double)n * 1e9 / (double)sum;
}

/* ---- font ---------------------------------------------------------------- */

/* 5x7 glyphs, rows top to bottom, bit 4 the leftmost column. Only what the
 * readout needs: the game's own text path (Render_.c's DrawChar) can't be
 * used here because the GameCube's IPL ROM font was never shipped (stubs.c). */
static const u8 s_glyph['Z' + 1 - ' '][7] = {
    /* ' ' */ { 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0 }, /* ! */
    { 0, 0, 0, 0, 0, 0, 0 }, /* " */
    { 0, 0, 0, 0, 0, 0, 0 }, /* # */
    { 0, 0, 0, 0, 0, 0, 0 }, /* $ */
    /* % */ { 0x19, 0x1A, 0x04, 0x0B, 0x13, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0 }, /* & */
    { 0, 0, 0, 0, 0, 0, 0 }, /* ' */
    { 0, 0, 0, 0, 0, 0, 0 }, /* ( */
    { 0, 0, 0, 0, 0, 0, 0 }, /* ) */
    { 0, 0, 0, 0, 0, 0, 0 }, /* * */
    { 0, 0, 0, 0, 0, 0, 0 }, /* + */
    { 0, 0, 0, 0, 0, 0, 0 }, /* , */
    /* - */ { 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00 },
    /* . */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C },
    { 0, 0, 0, 0, 0, 0, 0 }, /* / */
    /* 0 */ { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E },
    /* 1 */ { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },
    /* 2 */ { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F },
    /* 3 */ { 0x1F, 0x02, 0x04, 0x02, 0x11, 0x11, 0x0E },
    /* 4 */ { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 },
    /* 5 */ { 0x1F, 0x10, 0x1C, 0x01, 0x01, 0x11, 0x0E },
    /* 6 */ { 0x04, 0x08, 0x10, 0x1C, 0x11, 0x11, 0x0E },
    /* 7 */ { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
    /* 8 */ { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E },
    /* 9 */ { 0x0E, 0x11, 0x11, 0x1E, 0x01, 0x02, 0x0C },
    { 0, 0, 0, 0, 0, 0, 0 }, /* : */
    { 0, 0, 0, 0, 0, 0, 0 }, /* ; */
    { 0, 0, 0, 0, 0, 0, 0 }, /* < */
    { 0, 0, 0, 0, 0, 0, 0 }, /* = */
    { 0, 0, 0, 0, 0, 0, 0 }, /* > */
    { 0, 0, 0, 0, 0, 0, 0 }, /* ? */
    { 0, 0, 0, 0, 0, 0, 0 }, /* @ */
    /* A */ { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },
    /* B */ { 0x1C, 0x11, 0x11, 0x1C, 0x11, 0x11, 0x1C },
    /* C */ { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E },
    /* D */ { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C },
    /* E */ { 0x1F, 0x10, 0x10, 0x1C, 0x10, 0x10, 0x1F },
    /* F */ { 0x1F, 0x10, 0x10, 0x1C, 0x10, 0x10, 0x10 },
    /* G */ { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x1E },
    /* H */ { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },
    /* I */ { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E },
    /* J */ { 0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E },
    /* K */ { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 },
    /* L */ { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F },
    /* M */ { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 },
    /* N */ { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },
    /* O */ { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },
    /* P */ { 0x1C, 0x11, 0x11, 0x1C, 0x10, 0x10, 0x10 },
    /* Q */ { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D },
    /* R */ { 0x1C, 0x11, 0x11, 0x1C, 0x14, 0x12, 0x11 },
    /* S */ { 0x1E, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1C },
    /* T */ { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
    /* U */ { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },
    /* V */ { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 },
    /* W */ { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 },
    /* X */ { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 },
    /* Y */ { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 },
    /* Z */ { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F },
};

/* ---- the strip ----------------------------------------------------------- */

#define HUD_PAD 4      /* dark border around the text, unscaled pixels */
#define HUD_MAXW 640
#define HUD_MAXH 48
static u8 s_rgba[HUD_MAXW * HUD_MAXH * 4];
static int s_w, s_h;
static char s_text[48];

static void Put(u8 *p, u8 v) {
    p[0] = p[1] = p[2] = v;
    p[3] = 255;
}

/* The opaque image: a dark bar, the text in white over it. */
static void Rasterize(const char *text, int scale) {
    int len = (int)SDL_strlen(text);
    int cw = 6 * scale; /* glyph + one scaled pixel of spacing */
    int x, y, i, row, bit;

    s_w = HUD_PAD * 2 + len * cw - scale;
    s_h = HUD_PAD * 2 + 7 * scale;
    if (s_w > HUD_MAXW) {
        s_w = HUD_MAXW;
    }
    if (s_h > HUD_MAXH) {
        s_h = HUD_MAXH;
    }
    for (y = 0; y < s_h; y++) {
        for (x = 0; x < s_w; x++) {
            Put(&s_rgba[(y * s_w + x) * 4], 0x18);
        }
    }
    for (i = 0; text[i] != '\0'; i++) {
        const u8 *g;
        int cx;

        if (text[i] < ' ' || text[i] > 'Z') {
            continue;
        }
        g = s_glyph[text[i] - ' '];
        cx = HUD_PAD + i * cw;
        for (row = 0; row < 7; row++) {
            for (bit = 0; bit < 5; bit++) {
                if ((g[row] & (1 << (4 - bit))) == 0) {
                    continue;
                }
                for (y = 0; y < scale; y++) {
                    for (x = 0; x < scale; x++) {
                        int px = cx + bit * scale + x;
                        int py = HUD_PAD + row * scale + y;

                        if (px < s_w && py < s_h) {
                            Put(&s_rgba[(py * s_w + px) * 4], 0xFF);
                        }
                    }
                }
            }
        }
    }
}

void Port_HudDraw(int efbW, int efbH) {
    static u32 draws;
    int dst[4];

    if (!HudOn() || s_count == 0) {
        return;
    }
    /* Numbers refreshed about three times a second; a frame's worth of new
     * retraces barely moves the averages anyway. */
    if ((draws++ & 31) == 0 || s_text[0] == '\0') {
        char text[48];

        SDL_snprintf(text, sizeof(text), "FPS %.1f AVG %.1f 1%% LOW %.1f",
                     MeanRecentFps(20), MeanAllFps(), Low1Fps());
        if (SDL_strcmp(text, s_text) != 0) {
            SDL_strlcpy(s_text, text, sizeof(s_text));
            Rasterize(s_text, efbH >= 1000 ? 3 : 2);
        }
    }
    if (s_w <= 0 || s_h <= 0) {
        return;
    }
    dst[0] = 6;
    dst[1] = 6;
    dst[2] = dst[0] + s_w;
    dst[3] = dst[1] + s_h;
    if (dst[2] > efbW) {
        dst[2] = efbW;
    }
    if (dst[3] > efbH) {
        dst[3] = efbH;
    }
    g_gxr->drawImage(s_rgba, s_w, s_h, dst);
}

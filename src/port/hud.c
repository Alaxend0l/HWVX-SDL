/* On-screen readouts. HWVX_FPSHUD=1 draws a small strip at the top
 * left of the screen with the FPS now, the average, and the 1% low (the
 * average of the worst 1% of frame times), over a ring of the last few
 * thousand retraces. --show-position / HWVX_POSHUD=1 draws, under it, a strip per human
 * player with the car's position, heading, speed and collision square (game/CarA.c reports
 * them every frame; F9 logs them, vi.c).
 *
 * vi.c ticks it once per retrace; gx_draw.c draws it before each EFB copy,
 * where it bakes background and glyphs into one opaque RGBA image and blits
 * that through the renderer's drawImage. Being a single opaque image, both
 * backends already handle it, and it rides the copy's letterbox like any
 * other part of the frame. */
#include "port_internal.h"
#include "gx_render.h"   /* g_gxr: only drawImage is used */
#include "viewer.h"      /* Port_ViewerEfbSize */

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

/* ---- the strips ---------------------------------------------------------- */

#define HUD_PAD 4      /* dark border around the text, unscaled pixels */
#define HUD_MAXW 900
#define HUD_MAXH 48
#define HUD_STRIPS 5   /* the frame rate, then a position per player */

typedef struct Strip {
    u8 rgba[HUD_MAXW * HUD_MAXH * 4];
    int w, h, scale;
    char text[96];
} Strip;
static Strip s_strip[HUD_STRIPS];

static void Put(u8 *p, u8 v) {
    p[0] = p[1] = p[2] = v;
    p[3] = 255;
}

/* The opaque image: a dark bar, the text in white over it. */
static void Rasterize(Strip *st, const char *text, int scale) {
    int len = (int)SDL_strlen(text);
    int cw = 6 * scale; /* glyph + one scaled pixel of spacing */
    int x, y, i, row, bit;

    SDL_strlcpy(st->text, text, sizeof(st->text));
    st->scale = scale;
    st->w = HUD_PAD * 2 + len * cw - scale;
    st->h = HUD_PAD * 2 + 7 * scale;
    if (st->w > HUD_MAXW) {
        st->w = HUD_MAXW;
    }
    if (st->h > HUD_MAXH) {
        st->h = HUD_MAXH;
    }
    for (y = 0; y < st->h; y++) {
        for (x = 0; x < st->w; x++) {
            Put(&st->rgba[(y * st->w + x) * 4], 0x18);
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

                        if (px < st->w && py < st->h) {
                            Put(&st->rgba[(py * st->w + px) * 4], 0xFF);
                        }
                    }
                }
            }
        }
    }
}

/* A strip's text, re-rasterized only when it or the scale changed. */
static void SetText(Strip *st, const char *text, int scale) {
    if (SDL_strcmp(text, st->text) != 0 || scale != st->scale) {
        Rasterize(st, text, scale);
    }
}

/* ---- the position readout ------------------------------------------------- */

typedef struct PosLine {
    u64 ticks;                 /* SDL_GetTicks of the last report, 0: never */
    float x, y, z, heading, speed;
    int cx, cy, cz;
} PosLine;
static PosLine s_pos[4];

int Port_PosHudOn(void) {
    static int on = -1;
    const char *e;

    if (on < 0) {
        e = SDL_getenv("HWVX_POSHUD");
        on = g_port.showPosition || (e != NULL && *e == '1');
    }
    return on;
}

void Port_PosHudSet(int player, float x, float y, float z, float heading, float speed, int cx, int cy, int cz) {
    PosLine *p;

    if (player < 0 || player >= 4) {
        return;
    }
    p = &s_pos[player];
    p->ticks = SDL_GetTicks();
    p->x = x;
    p->y = y;
    p->z = z;
    p->heading = heading;
    p->speed = speed;
    p->cx = cx;
    p->cy = cy;
    p->cz = cz;
}

/* Shown while reported in the last quarter second (in a race). */
static int PosLive(int i) {
    return s_pos[i].ticks != 0 && SDL_GetTicks() - s_pos[i].ticks < 250;
}

void Port_PosHudMark(void) {
    int i;

    for (i = 0; i < 4; i++) {
        const PosLine *p = &s_pos[i];

        if (PosLive(i)) {
            Port_Log("pos: player %d x %.1f y %.1f z %.1f heading %.1f speed %.1f cell %d %d %d", i + 1, p->x, p->y, p->z,
                     p->heading, p->speed, p->cx, p->cy, p->cz);
        }
    }
}

static void PosText(int i, char *out, size_t size) {
    const PosLine *p = &s_pos[i];

    if (p->cx < 0) {
        SDL_snprintf(out, size, "P%d X %.1f Y %.1f Z %.1f H %.0f S %.0f", i + 1, p->x, p->y, p->z, p->heading, p->speed);
    } else {
        SDL_snprintf(out, size, "P%d X %.1f Y %.1f Z %.1f H %.0f S %.0f CELL %d %d %d", i + 1, p->x, p->y, p->z,
                     p->heading, p->speed, p->cx, p->cy, p->cz);
    }
}

/* ---- drawing -------------------------------------------------------------- */

void Port_HudDraw(int efbW, int efbH) {
    static u32 draws;
    int winW = 0, winH = 0, scale, vw, vh, i, top;
    f32 r = 1.0f;   /* EFB pixels per window pixel (the resolution scale) */
    int fps = HudOn() && s_count != 0, pos = Port_PosHudOn();

    if (!fps && !pos) {
        return;
    }
    /* Sized in window pixels, so the strips look the same at any resolution
     * scale: rasterized for the window's height, stretched by the EFB's
     * size over the window's (the viewer's frames: the EFB's own size). */
    if (!Port_ViewerEfbSize(&vw, &vh) && SDL_GetWindowSizeInPixels(g_port.window, &winW, &winH) && winH > 0 &&
        winH != efbH) {
        r = (f32)efbH / (f32)winH;
    }
    scale = (r != 1.0f ? winH : efbH) >= 1000 ? 3 : 2;
    draws++;
    /* The frame rate refreshed about three times a second; a frame's worth of
     * new retraces barely moves the averages anyway. */
    if (fps && ((draws & 31) == 1 || s_strip[0].text[0] == '\0' || scale != s_strip[0].scale)) {
        char text[96];

        SDL_snprintf(text, sizeof(text), "FPS %.1f AVG %.1f 1%% LOW %.1f", MeanRecentFps(20), MeanAllFps(), Low1Fps());
        SetText(&s_strip[0], text, scale);
    }
    top = (int)(6.0f * r + 0.5f);
    for (i = 0; i < HUD_STRIPS; i++) {
        Strip *st = &s_strip[i];
        int dst[4];

        if (i == 0 && !fps) {
            continue;
        }
        if (i > 0) {
            char text[96];

            if (!pos || !PosLive(i - 1)) {
                continue;
            }
            PosText(i - 1, text, sizeof(text));
            SetText(st, text, scale);
        }
        if (st->w <= 0 || st->h <= 0) {
            continue;
        }
        dst[0] = (int)(6.0f * r + 0.5f);
        dst[1] = top;
        dst[2] = dst[0] + (int)((f32)st->w * r + 0.5f);
        dst[3] = dst[1] + (int)((f32)st->h * r + 0.5f);
        if (dst[2] > efbW) {
            dst[2] = efbW;
        }
        if (dst[3] > efbH) {
            dst[3] = efbH;
        }
        g_gxr->drawImage(st->rgba, st->w, st->h, dst);
        top = dst[3] + (int)(2.0f * r + 0.5f);
    }
}

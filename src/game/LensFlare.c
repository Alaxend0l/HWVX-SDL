#include "common.h"


typedef struct FlareOverlay {
    GXColor color;
    u8 mode;
    u8 pad5[3];
    f32 scale;
} FlareOverlay;

typedef struct FlareElement {
    GXColor color;
    f32 size;
    f32 pos;
    u32 texture;
    u8 flag;
    u8 pad11[3];
} FlareElement;

typedef struct FlareData {
    u32 count;
    PTR32(FlareElement) elements;
    u8 rayTest;
    u8 overlay;
    u8 padA[2];
    f32 fadeStart;
    f32 fadeEnd;
    PTR32(FlareOverlay) ov;
} FlareData;

typedef struct Flare {
    PTR32(FlareData) flare;
    PTR32(u8) thing;
    u8 unk8[0x10 - 0x8];
    Vector4 pos;
    s8 visible;
    u8 unk21[0x30 - 0x21];
} Flare;

Flare g_pFlare[64];
u32 g_LensFlareRayTest;
u32 g_activeFlares;

/* a flare follows its thing when it has one, else sits at its own position */
static inline Vector4 LensFlare_GetPos(u8 *thing, Vector4 *pos) {
    if (thing != NULL) {
        return *(Vector4 *)(thing + 0x40);
    }
    return *pos;
}

void LensFlare_Init(void) {
    extern Flare g_pFlare[64];
    extern u32 g_activeFlares;
    extern u32 g_LensFlareRayTest;
    s32 i;

    for (i = 0; i < 64; i++) {
        SETP(g_pFlare[i].flare, NULL);
    }
    g_activeFlares = 0;
    g_LensFlareRayTest = 1;
}

s32 LensFlare_Add(FlareData *flare, u8 *thing, Vector4 *pos) {
    extern Flare g_pFlare[64];
    extern u32 g_activeFlares;
    u32 i;

    if (flare != NULL) {
        for (i = 0; i < 64; i++) {
            if (GETP(g_pFlare[i].flare) == NULL) {
                SETP(g_pFlare[i].thing, thing);
                SETP(g_pFlare[i].flare, flare);
                g_pFlare[i].visible = 0;
                if (pos != NULL) {
                    g_pFlare[i].pos = *pos;
                }
                break;
            }
        }
        if (i != 64) {
            if (i >= g_activeFlares) {
                g_activeFlares = i + 1;
            }
            return i;
        }
    }
    return -1;
}

void LensFlare_Delete(s32 arg0) {
    extern Flare g_pFlare[64];
    extern u32 g_activeFlares;
    s32 i;

    if (arg0 != -1) {
        g_activeFlares = 0;
        SETP(g_pFlare[arg0].flare, NULL);
        for (i = 0; i < 64; i++) {
            if (GETP(g_pFlare[i].flare) != NULL) {
                g_activeFlares = i + 1;
            }
        }
    }
}

void LensFlare_Render(void) {
    extern Flare g_pFlare[64];
    extern u32 g_activeFlares;
    extern u32 g_LensFlareRayTest;
    f32 mtx[4][4] __attribute__((aligned(16)));
    Vector4 dir __attribute__((aligned(16)));
    Vector4 screen __attribute__((aligned(16)));
    Vector4 sprite __attribute__((aligned(16)));
    Vector4 camera __attribute__((aligned(16)));
    Vector4 at __attribute__((aligned(16)));
    Vector4 up __attribute__((aligned(16)));
    Vector4 right __attribute__((aligned(16)));
    GXColor color;
    f32 len;
    s32 vx;
    s32 vy;
    s32 w;
    s32 h;
    f32 scale;
    f32 y;
    f32 halfW;
    f32 halfH;
    f32 x;
    Flare *f;
    u32 i;
    u32 j;
    FlareElement *e;
    FlareData *d;
    FlareOverlay *ov;

    Gfx_GetCameraPosition(&camera);
    Gfx_GetCameraVector(&at, &up, &right);
    mtx[0][3] = 0.0f;
    mtx[1][3] = 0.0f;
    mtx[2][3] = 0.0f;
    mtx[3][0] = 0.0f;
    mtx[3][1] = 0.0f;
    mtx[3][2] = 0.0f;
    mtx[3][3] = 1.0f;
    mtx[0][0] = right.x;
    mtx[1][0] = right.y;
    mtx[2][0] = right.z;
    mtx[0][1] = up.x;
    mtx[1][1] = up.y;
    mtx[2][1] = up.z;
    mtx[0][2] = at.x;
    mtx[1][2] = at.y;
    mtx[2][2] = at.z;
    Gfx_GetViewport(&vx, &vy, &w, &h);
    halfW = w >> 1;
    halfH = h >> 1;
    sprite.z = -1.0f;
    if (++g_LensFlareRayTest > g_activeFlares) {
        g_LensFlareRayTest = 0;
    }
    for (i = 0; i < g_activeFlares; i++) {
        f = &g_pFlare[i];
        if (GETP(f->flare) == NULL) {
            continue;
        }
        screen = LensFlare_GetPos(GETP(f->thing), &f->pos);
        PSVECSubtract(&camera, &screen, &dir);
        XformVec(mtx, &dir, &screen);
        screen.z *= -1.0f;
        if (screen.z > 0.0f) {
            x = halfW + (-screen.x * halfW) / screen.z;
            y = halfH + (-screen.y * halfH) / screen.z;
            if (x < 0.0f || x > (f32)w || y < 0.0f || y > (f32)h) {
                continue;
            }
            d = GETP(f->flare);
            scale = 128.0f;
            if (screen.z > d->fadeStart) {
                scale -= scale * ((screen.z - d->fadeStart) / (d->fadeEnd - d->fadeStart));
            }
            if (scale <= 0.0f) {
                continue;
            }
            vx = x - halfW;
            vy = y - halfH;
            y -= Sqrt(vx * vx + vy * vy);
            y *= 1.0f / halfH;
            if (y > 0.0f) {
                if (GETP(f->flare)->rayTest && i == g_LensFlareRayTest) {
                    len = 1.0f;
                    if (RayHitsTerrainAndObjects(&camera, &dir, &len, NULL)) {
                        f->visible = 0;
                    } else {
                        f->visible = 1;
                    }
                }
                if (f->visible) {
                    y *= scale;
                    if (GETP(f->flare)->overlay) {
                        GXColor c;
                        ov = GETP(GETP(f->flare)->ov);
                        color = ov->color;
                        scale = y * ov->scale / 128.0f;
                        if (scale < 0.0f) {
                            scale = 0.0f;
                        }
                        if (ov->mode < 4) {
                            u32 r = color.r * scale;
                            u32 g = color.g * scale;
                            u32 b = color.b * scale;
                            if (r > 255) {
                                color.r = 255;
                            } else {
                                color.r = r;
                            }
                            if (g > 255) {
                                color.g = 255;
                            } else {
                                color.g = g;
                            }
                            if (b > 255) {
                                color.b = 255;
                            } else {
                                color.b = b;
                            }
                        } else {
                            u32 a = color.a * scale;
                            if (a > 255) {
                                color.a = 255;
                            } else {
                                color.a = a;
                            }
                        }
                        c = color;
                        Gfx_ScreenOverlay(&c, GETP(GETP(f->flare)->ov)->mode);
                    }
                    y /= 128.0f;
                    for (j = 0; j < GETP(f->flare)->count; j++) {
                        GXColor c;
                        e = &GETP(GETP(f->flare)->elements)[j];
                        sprite.x = (f32)vx * e->pos + halfW;
                        sprite.y = (f32)vy * e->pos + halfH;
                        color = e->color;
                        if (GETP(GETP(f->flare)->elements)[j].flag < 4) {
                            u32 r = color.r * y;
                            u32 g = color.g * y;
                            u32 b = color.b * y;
                            if (r > 255) {
                                color.r = 255;
                            } else {
                                color.r = r;
                            }
                            if (g > 255) {
                                color.g = 255;
                            } else {
                                color.g = g;
                            }
                            if (b > 255) {
                                color.b = 255;
                            } else {
                                color.b = b;
                            }
                        } else {
                            u32 a = color.a * y;
                            if (a > 255) {
                                color.a = 255;
                            } else {
                                color.a = a;
                            }
                        }
                        c = color;
                        Gfx_Add2DSprite(&sprite, e->texture, &c, e->size, e->flag, 0.0f);
                    }
                }
            }
        }
    }
}


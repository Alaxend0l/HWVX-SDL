#include "common.h"

BulletHole g_bulletData[512];
u32 g_nextBullet;

void BulletHole_Init(void) {
    s32 i;

    for (i = 0; i < 512; i++) {
        g_bulletData[i].life = 0;
    }
    g_nextBullet = 1;
}

void BulletHole_Heartbeat(void) {
    extern s32 g_iDeltaTimeMS;
    BulletHole *bullet;
    u32 alpha;
    s32 i;
    s32 delta;

    delta = g_iDeltaTimeMS;
    for (i = 0; i < 512; i++) {
        bullet = &g_bulletData[i];
        if (bullet->life > 0) {
            bullet->life -= delta;
            if (bullet->life <= 0) {
                bullet->life = 0;
            } else if (bullet->life < 2000) {
                alpha = (bullet->life << 7) / 2000;
                if (alpha < bullet->a) {
                    bullet->a = alpha;
                }
            }
        }
    }
}


void BulletHole_Render(void) {
    BulletHole *bullet;
    u32 i;
    u16 texture;
    u32 count;
    u8 *dst;

    count = 0;
    texture = 0xFFFF;
    dst = NULL;
    for (i = 0; i < 512; i++) {
        bullet = &g_bulletData[i];
        if (bullet->life > 0) {
            if (texture != bullet->texture) {
                if (count != 0) {
                    Gfx_End_GroupTri(count);
                    count = 0;
                }
                dst = Gfx_Begin_GroupQuadFlat(bullet->texture, 7, 0);
                texture = bullet->texture;
            }
            MemCpy32(dst, bullet, 0x50);
            dst += 0x50;
            count++;
        }
    }
    if (count != 0) {
        Gfx_End_GroupTri(count);
    }
}


void BulletHole_AddSegment(Matrix4 *mtx, u16 texture, GXColor color, s32 life, f32 size) {
    s32 i;

    i = 512;
    while (i-- > 0) {
        if (g_nextBullet == 512) {
            g_nextBullet = 0;
        }
        if (g_bulletData[g_nextBullet++].life == 0) {
            i = g_nextBullet - 1;
            break;
        }
    }
    if (i == 0) {
        return;
    }
    g_bulletData[i].life = life;
    g_bulletData[i].texture = texture;
    g_bulletData[i].r = color.r;
    g_bulletData[i].g = color.g;
    g_bulletData[i].b = color.b;
    g_bulletData[i].a = color.a;
    g_bulletData[i].v[3] = 0.0f;
    g_bulletData[i].v[7] = 1.0f;
    g_bulletData[i].v[0] = mtx->v[3].x + mtx->v[0].x * size + mtx->v[1].x * size;
    g_bulletData[i].v[1] = mtx->v[3].y + mtx->v[0].y * size + mtx->v[1].y * size;
    g_bulletData[i].v[2] = mtx->v[3].z + mtx->v[0].z * size + mtx->v[1].z * size;
    g_bulletData[i].v[4] = mtx->v[3].x - mtx->v[0].x * size + mtx->v[1].x * size;
    g_bulletData[i].v[5] = mtx->v[3].y - mtx->v[0].y * size + mtx->v[1].y * size;
    g_bulletData[i].v[6] = mtx->v[3].z - mtx->v[0].z * size + mtx->v[1].z * size;
    g_bulletData[i].v[12] = mtx->v[3].x - mtx->v[0].x * size - mtx->v[1].x * size;
    g_bulletData[i].v[13] = mtx->v[3].y - mtx->v[0].y * size - mtx->v[1].y * size;
    g_bulletData[i].v[14] = mtx->v[3].z - mtx->v[0].z * size - mtx->v[1].z * size;
    g_bulletData[i].v[8] = mtx->v[3].x + mtx->v[0].x * size - mtx->v[1].x * size;
    g_bulletData[i].v[9] = mtx->v[3].y + mtx->v[0].y * size - mtx->v[1].y * size;
    g_bulletData[i].v[10] = mtx->v[3].z + mtx->v[0].z * size - mtx->v[1].z * size;
}


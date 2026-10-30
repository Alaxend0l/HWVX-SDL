#include "common.h"



CollisionTri g_aCollisionTriPairCache[4];
extern s32 g_iDeltaTimeMS;


void CollisionTriCache_Clear(void) {
    u32 i;

    for (i = 0; i < 4; i++) {
        g_aCollisionTriPairCache[i].triId0 = 0;
        g_aCollisionTriPairCache[i].triId1 = 0;
        g_aCollisionTriPairCache[i].time = 99999;
    }
}

void CollisionTriCache_Heartbeat(void)
{
    u32 i;

    for (i = 0; i < 4; i++)
    {
        if (g_aCollisionTriPairCache[i].triId0 != 0)
        {
            g_aCollisionTriPairCache[i].time += g_iDeltaTimeMS;
        }
        if (g_aCollisionTriPairCache[i].time > 1000)
        {
            g_aCollisionTriPairCache[i].triId0 = 0;
            g_aCollisionTriPairCache[i].triId1 = 0;
            g_aCollisionTriPairCache[i].time = 99999;
        }
    }
}

s32 CollisionTriCache_Get(u32 triId0, u32 triId1, Vector4 *pos, Vector4 *normal) {
    u32 i;

    for (i = 0; i < 4; i++) {
        if (triId0 == g_aCollisionTriPairCache[i].triId0 && triId1 == g_aCollisionTriPairCache[i].triId1) {
            *pos = g_aCollisionTriPairCache[i].pos;
            *normal = g_aCollisionTriPairCache[i].normal;
            g_aCollisionTriPairCache[i].time = 0;
            return 1;
        }
    }
    return 0;
}

void CollisionTriCache_Set(u32 triId0, u32 triId1, Vector4 *pos, Vector4 *normal)
{
    u32 i;
    u32 maxTime;
    u32 slot;

    maxTime = 0;
    slot = 0;
    for (i = 0; i < 4; i++)
    {
        if (triId0 == g_aCollisionTriPairCache[i].triId0 && triId1 == g_aCollisionTriPairCache[i].triId1)
        {
            g_aCollisionTriPairCache[i].time = 0;
            g_aCollisionTriPairCache[i].normal = *normal;
            g_aCollisionTriPairCache[i].pos = *pos;
            return;
        }
    }

    for (i = 0; i < 4; i++)
    {
        if (g_aCollisionTriPairCache[i].triId0 == 0)
        {
            slot = i;
            break;
        }
        if (g_aCollisionTriPairCache[i].time > maxTime)
        {
            maxTime = g_aCollisionTriPairCache[i].time;
            slot = i;
        }
    }

    g_aCollisionTriPairCache[slot].time = 0;
    g_aCollisionTriPairCache[slot].normal = *normal;
    g_aCollisionTriPairCache[slot].pos = *pos;
    g_aCollisionTriPairCache[slot].triId0 = triId0;
    g_aCollisionTriPairCache[slot].triId1 = triId1;
}

s32 Collision_ProcessDividingAxis(DividingAxis *axis)
{
    s32 result = 1;
    f32 t0;
    f32 t1;

    if (_fabs(axis->dir) < 1e-9f)
    {
        if (axis->maxA < axis->minB || axis->maxB < axis->minA)
        {
            return 0;
        }
        return 1;
    }

    if (axis->minA > axis->maxB)
    {
        if (axis->dir > 0.0f)
        {
            return 0;
        }
        t0 = (axis->maxB - axis->minA) / axis->dir;
        if (t0 > axis->tExit)
        {
            return 0;
        }
        t1 = (axis->minB - axis->maxA) / axis->dir;
        if (t0 > axis->tEnter)
        {
            result = 2;
            axis->tEnter = t0;
        }
        if (t1 < axis->tExit)
        {
            axis->tExit = t1;
        }
    }
    else if (axis->maxA < axis->minB)
    {
        if (axis->dir <= 0.0f)
        {
            return 0;
        }
        t0 = (axis->minB - axis->maxA) / axis->dir;
        t1 = (axis->maxB - axis->minA) / axis->dir;
        if (t0 > axis->tEnter)
        {
            result = 3;
            axis->tEnter = t0;
        }
        if (t1 < axis->tExit)
        {
            axis->tExit = t1;
        }
    }
    else if (axis->dir > 0.0f)
    {
        t1 = (axis->maxB - axis->minA) / axis->dir;
        if (t1 < axis->tExit)
        {
            axis->tExit = t1;
        }
    }
    else
    {
        t1 = (axis->minB - axis->maxA) / axis->dir;
        if (t1 < axis->tExit)
        {
            axis->tExit = t1;
        }
    }

    if (axis->tExit < axis->tEnter)
    {
        return 0;
    }
    return result;
}

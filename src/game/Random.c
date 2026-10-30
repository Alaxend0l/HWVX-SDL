#include "common.h"

typedef struct RandomNumberGenerator {
    Vector4 vectors[0x300];
    u32 unk3000;
    u32 unk3004;
    u32 index;
    f32 floats[0x400];
    u32 unk400C;
} RandomNumberGenerator;

RandomNumberGenerator g_randomNumberGenerator;


void RandomNumberGenerator_Init(void) {
    Vector4 axis __attribute__((aligned(16)));
    Vector4 q __attribute__((aligned(16)));
    f32 mtx[4][4] __attribute__((aligned(16)));
    u32 i;

    srand(OSGetTick());
    for (i = 0; i < 0x100; i++) {
        axis.x = (rand() & 0xFF) - 127;
        axis.y = (rand() & 0xFF) - 127;
        axis.z = (rand() & 0xFF) - 127;
        GenerateQuaternion(&axis, &q, (3.1415927f * rand()) / 32768.0f);
        QuaternionToSubMatrix(&q, mtx);
        g_randomNumberGenerator.vectors[i * 3 + 0].x = mtx[0][0];
        g_randomNumberGenerator.vectors[i * 3 + 0].y = mtx[0][1];
        g_randomNumberGenerator.vectors[i * 3 + 0].z = mtx[0][2];
        g_randomNumberGenerator.vectors[i * 3 + 1].x = mtx[1][0];
        g_randomNumberGenerator.vectors[i * 3 + 1].y = mtx[1][1];
        g_randomNumberGenerator.vectors[i * 3 + 1].z = mtx[1][2];
        g_randomNumberGenerator.vectors[i * 3 + 2].x = mtx[2][0];
        g_randomNumberGenerator.vectors[i * 3 + 2].y = mtx[2][1];
        g_randomNumberGenerator.vectors[i * 3 + 2].z = mtx[2][2];
    }
    g_randomNumberGenerator.unk3000 = 0;
    g_randomNumberGenerator.unk3004 = 0;
    g_randomNumberGenerator.index = 0;
    for (i = 0; i < 0x400; i++) {
        g_randomNumberGenerator.floats[i] = rand() / 32768.0f;
    }
}

void RandomNumberGenerator_GetRandomQuaternion(Vector4 *q) {
    Vector4 axis __attribute__((aligned(16)));
    f32 angle;

    angle = RandomAdd(0.0f, 6.2831855f);
    RandomNumberGenerator_GetRandomVector(&axis);
    GenerateQuaternion(&axis, q, angle);
}

void RandomNumberGenerator_GetRandomSubMatrix(Vector4 *m) {
    RandomNumberGenerator *rng = &g_randomNumberGenerator;
    s32 i = rng->unk3000 * 3;

    m[0].x = rng->vectors[i].x;
    m[0].y = rng->vectors[i].y;
    m[0].z = rng->vectors[i++].z;
    m[1].x = rng->vectors[i].x;
    m[1].y = rng->vectors[i].y;
    m[1].z = rng->vectors[i++].z;
    m[2].x = rng->vectors[i].x;
    m[2].y = rng->vectors[i].y;
    m[2].z = rng->vectors[i].z;
    if (++rng->unk3000 >= 0x100) {
        rng->unk3000 = 0;
    }
}

void RandomNumberGenerator_GetRandomVector(Vector4 *v) {
    *v = g_randomNumberGenerator.vectors[RandomAdd_s32(0, 0x300)];
}

s32 RandomNumberGenerator_GetRandomInteger(s32 min, s32 max) {
    return RandomAdd_s32(min, max - min);
}

f32 RandomNumberGenerator_GetRandomFloat(f32 min, f32 max) {
    RandomNumberGenerator *rng = &g_randomNumberGenerator;
    if (++rng->index >= 0x400) {
        rng->index = 0;
    }
    return rng->floats[rng->index] * (max - min) + min;
}

f32 RandomAdd(f32 base, f32 range) {
    RandomNumberGenerator *rng = &g_randomNumberGenerator;
    if (++rng->index >= 0x400) {
        rng->index = 0;
    }
    return rng->floats[rng->index] * range + base;
}

s32 RandomAdd_s32(s32 base, s32 range) {
    RandomNumberGenerator *rng = &g_randomNumberGenerator;
    if (++rng->index >= 0x400) {
        rng->index = 0;
    }
    return rng->floats[rng->index] * (f32)range + (f32)base;
}

f32 RandomDirVar(f32 base, f32 var) {
    RandomNumberGenerator *rng = &g_randomNumberGenerator;
    if (++rng->index >= 0x400) {
        rng->index = 0;
    }
    return (base - var) + rng->floats[rng->index] * (var * 2.0f);
}


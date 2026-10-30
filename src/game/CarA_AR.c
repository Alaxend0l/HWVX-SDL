#include "common.h"



s32 Pod_Init(u8 *pod, u8 *init) {
    u8 *motion;

    Thing_Init(pod, 0);
    motion = pod + 0xB0;
    SLOT_STORE((u8 *)(pod) + 0x110, SLOT_LOAD(void *, (u8 *)(init) + 0x20));
    M2C_FIELD(pod, s32 *, 0x114) = 0;
    M2C_FIELD(pod, s32 *, 0x118) = 0;
    M2C_FIELD(pod, s32 *, 0x40) = M2C_FIELD(init, s32 *, 0x0);
    M2C_FIELD(pod, s32 *, 0x44) = M2C_FIELD(init, s32 *, 0x4);
    M2C_FIELD(pod, s32 *, 0x48) = M2C_FIELD(init, s32 *, 0x8);
    M2C_FIELD(pod, s32 *, 0x4C) = M2C_FIELD(init, s32 *, 0xC);
    M2C_FIELD(pod, s32 *, 0xB0) = M2C_FIELD(SLOT_LOAD(u8 *, (u8 *)(pod) + 0x110), s32 *, 0x3C);
    M2C_FIELD(pod, f32 *, 0xD4) = 0.0f;
    MotionState_Init(motion, 0);
    MotionState_BuildMatricesWorld(pod + 0x50, pod + 0x40, motion, 1.0f);
    return 1;
}

/* port: takes a pointer, as the vtable calls it; the decomp has s32 */
void Pod_Release(void *arg0) {
    MotionState_Release((s32)HND((u8 *)arg0 + 0xB0));
    Thing_Release(arg0);
}

s32 Pod_Move(u8 *pod) {
    typedef struct {
        u8 pad0[0x94];
        u16 counts[7];
    } PodPlayer;
    extern Matrix4 g_identity;
    u8 *creature;
    u8 *next;
    Vector4 pos __attribute__((aligned(16)));

    XformVec(pod + 0x10, (Vector4 *)(SLOT_LOAD(u8 *, (pod + 0x110)) + 0x10), (Vector4 *)(pod + 0xF0));
    XformVec(pod + 0x10, SLOT_LOAD(Vector4 *, (pod + 0x110)), (Vector4 *)(pod + 0x100));
    XformVec(pod + 0x10, (Vector4 *)(SLOT_LOAD(u8 *, (pod + 0x110)) + 0x20), &pos);
    MotionState_BuildMatricesWorld(pod + 0x50, &pos, pod + 0xB0, 1.0f);
    MotionState_Heartbeat(pod + 0xB0, &g_identity, pod);

    if ((*(u32 *)(pod + 0x74) & 0x10) && !(*(u32 *)(pod + 0x74) & 0x20) && pod[0xCC] == 2) {
        if (*(f32 *)(pod + 0xD0) == *(f32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (pod + 0xC8)) + 0x28)) + 4)) {
            pod[0xCC] = 0;
            *(f32 *)(pod + 0xD0) = *SLOT_LOAD(f32 *, (SLOT_LOAD(u8 *, (pod + 0xC8)) + 0x20));
        } else {
            *(f32 *)(pod + 0xD0) = *(f32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (pod + 0xC8)) + 0x24)) + 4);
        }
    }

    if (*(u32 *)(pod + 0x74) & 0x100000) {
        for (creature = SLOT_LOAD(u8 *, (pod + 0x88)); creature != NULL; creature = next) {
            next = SLOT_LOAD(u8 *, (creature + 0xA4));
            if (SLOT_LOAD(u8 *, (creature + 0x8C)) == g_creatureVTable && *(u32 *)(SLOT_LOAD(u8 *, (creature + 0x94)) + 0xD8) != 0) {
                Vector4 dir __attribute__((aligned(16)));
                u8 type;

                *(f32 *)(creature + 0x1C0) = 1.0f;
                ThingManagerList_Remove((void **)(pod + 0x88), creature);
                *(Vector4 *)(creature + 0x40) = *(Vector4 *)(pod + 0xF0);
                ThingManagerList_Append(&g_thingManager_DARYL.activeList, creature);
                Thing_AddToGrid(creature, 6);
                *(s32 *)(creature + 0x1B0) = -1;
                *(Vector4 *)(creature + 0x190) = *(Vector4 *)(creature + 0x40);
                *(f32 *)(creature + 0x194) -= 1.0f;
                PSVECSubtract((Vector4 *)(pod + 0x100), (Vector4 *)(pod + 0x40), (Vector4 *)creature);
                *(f32 *)(creature + 0x4) = 0.0f;
                if (PSVECSquareMag((Vector4 *)creature)) {
                    PSVECNormalize((Vector4 *)creature, (Vector4 *)creature);
                }
                dir = *(Vector4 *)creature;
                *(f32 *)(creature + 0xB0) = atan2(dir.x, dir.z);
                SinCosRad((f32 *)(creature + 0x54), (f32 *)(creature + 0x5C), 0.5f * *(f32 *)(creature + 0xB0));
                *(f32 *)(creature + 0x50) = 0.0f;
                *(f32 *)(creature + 0x58) = 0.0f;
                {
                    f32 ww = *(f32 *)(creature + 0x5C) * *(f32 *)(creature + 0x5C);
                    f32 yy = *(f32 *)(creature + 0x54) * *(f32 *)(creature + 0x54);
                    f32 inv = 1.0f / (yy + ww);
                    f32 m = inv * (ww - yy);

                    inv *= 2.0f;
                    *(f32 *)(creature + 0x10) = m;
                    *(f32 *)(creature + 0x24) = 1.0f;
                    *(f32 *)(creature + 0x38) = m;
                    *(f32 *)(creature + 0x14) = 0.0f;
                    *(f32 *)(creature + 0x18) = inv * (*(f32 *)(creature + 0x5C) * *(f32 *)(creature + 0x54));
                    *(f32 *)(creature + 0x20) = 0.0f;
                    *(f32 *)(creature + 0x28) = 0.0f;
                    *(f32 *)(creature + 0x30) = inv * (*(f32 *)(creature + 0x5C) * *(f32 *)(creature + 0x54));
                    *(f32 *)(creature + 0x34) = 0.0f;
                    *(f32 *)(creature + 0x3C) = 0.0f;
                    *(f32 *)(creature + 0x2C) = 0.0f;
                    *(f32 *)(creature + 0x1C) = 0.0f;
                    *(f32 *)(creature + 0x4C) = 1.0f;
                }
                MotionState_BuildMatricesWorld(creature + 0x50, creature + 0x40, creature + 0xCC, 1.0f);
                *(f32 *)(creature + 0xB4) = 0.0f;
                *(f32 *)(creature + 0x0) *= 15.0f;
                *(f32 *)(creature + 0x4) = 10.0f;
                *(f32 *)(creature + 0x8) *= 15.0f;
                *(u32 *)(creature + 0x74) &= ~0x10;
                Creature_MotionStateInit(creature, 0xF, 0, 0);
                *(u32 *)(creature + 0x74) |= 0x20000;
                *(s32 *)(SLOT_LOAD(u8 *, (pod + 0x94)) + 0x7C) += *(s32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (creature + 0xB8)) + 0x54)) + 0xC);
                (SLOT_LOAD(u8 *, (pod + 0x94)))[0x71] = 1;
                type = (SLOT_LOAD(u8 *, (creature + 0xB8)))[0x58];
                if (type < 7) {
                    if ((SLOT_LOAD(PodPlayer *, (pod + 0x94)))->counts[type] != 0) {
                        (SLOT_LOAD(PodPlayer *, (pod + 0x94)))->counts[type]--;
                    }
                } else {
                    (SLOT_LOAD(PodPlayer *, (pod + 0x94)))->counts[type]++;
                }
                (SLOT_LOAD(u8 *, (pod + 0x94)))[0xA4] = 1;
                *(s32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (pod + 0x94)) + 0x2C)) + 0x78) += *SLOT_LOAD(s32 *, (SLOT_LOAD(u8 *, (creature + 0xB8)) + 0x54));
                Trigger_CreatureDroppedOff(SLOT_LOAD(void *, (SLOT_LOAD(u8 *, (pod + 0x94)) + 0x2C)), creature);
                if (SLOT_LOAD(u8 *, (creature + 0x90)) != NULL) {
                    *(u32 *)(SLOT_LOAD(u8 *, (creature + 0x90)) + 0x20) = 0;
                }
                if (*(u32 *)(pod + 0x118) != 0) {
                    (*(u32 *)(pod + 0x118))--;
                }
                *(u32 *)(pod + 0x74) &= ~0x20;
            }
        }
        *(u32 *)(pod + 0x74) &= ~0x100000;
    }
    return 1;
}

void Pod_Draw(u8 *pod) {
    typedef struct PodPart {
        u8 flags;
        u8 xform;
        u8 pad2[2];
        u16 mtxIndex;
        u8 pad6[2];
        PTR32(void) geom;
        PTR32(Vector4) offset;
        u8 pad10[0xC];
    } PodPart;
    typedef struct PodPartList {
        u32 count;
        PTR32(PodPart) parts;
    } PodPartList;
    PodPartList *list;
    u32 i;
    PodPart *part;
    u32 bit;
    void *geom;
    Matrix4 mtx;

    if (SLOT_LOAD(Matrix4 *, (pod + 0xC0)) != NULL) {
        MotionState_Draw(pod + 0xB0);
    } else {
        MotionState_Draw2(pod + 0xB0, HND(pod + 0x10), SLOT_LOAD(Matrix4 *, (pod + 0xC0)));
    }
    list = SLOT_LOAD(PodPartList *, (SLOT_LOAD(u8 *, (pod + 0xB0)) + 0x38));
    if (list != NULL) {
        for (i = 0; i < list->count; i++) {
            part = &GETP(list->parts)[i];
            geom = GETP(part->geom);
            bit = *(u32 *)(pod + 0x114) & (1 << i);
            if (bit && !(part->flags & 1)) {
                continue;
            }
            if (!bit && (part->flags & 1)) {
                continue;
            }
            mtx = (SLOT_LOAD(Matrix4 *, (pod + 0xC0)))[part->mtxIndex];
            if (GETP(list->parts)[i].xform) {
                XformVec(&mtx, GETP(GETP(list->parts)[i].offset), &mtx.v[3]);
            }
            Gfx_DrawGeometry(HND(geom), HND(&mtx));
        }
    }
}

s32 Pod_Invalidate(s32 arg0, u32 arg1, u32 arg2) {
    u32 temp_r0;

    temp_r0 = *(u32 *)PTR((arg0 + 0x110));
    if ((temp_r0 >= arg1) && (temp_r0 < arg2)) {
        return 1;
    }
    return 0;
}


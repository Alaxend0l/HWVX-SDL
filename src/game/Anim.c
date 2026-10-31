#include "common.h"

/* The drafts moved in from src/auto use the dump's Vector; same layout. */
typedef Vector4 Vector;

typedef struct Anim {
    u8 pad0[4];
    s32 kind;           /* 0x04 & 2: compound, & 8: hybrid (AnimExtractKeyFrame) */
    f32 minTime;
    f32 maxTime;
    u32 numBones;
    u32 numKeysPerFrame;
    PTR32(u32) mask;
    PTR32(u16) flags;
    PTR32(f32) times;
    PTR32(Vector4) data;
    u32 numFrames;
    PTR32(f32) staticData;
    u32 numCompound;
} Anim;

/* A bone's constant rotation in the key data (0x20 bytes). */
typedef struct AnimStaticRot {
    Vector4 unk0;
    Quaternion rot;             /* 0x10 */
} AnimStaticRot;

typedef struct AnimBone {
    Quaternion rot;
    f32 pos[3];
    union {
        u32 info;
        struct {
            u8 numChildren;
            u8 pop;
            u8 next;
            u8 pad;
        };
    };
} AnimBone;


void AnimExtractKeyFrame_NonCompound(Anim *anim, AnimBone *outp, f32 time) {

}

void AnimExtractKeyFrame_Compound(Anim *anim, AnimBone *outp, f32 time) {
    Vector4 tv __attribute__((aligned(16)));
    f32 values[448];
    u16 *flags;
    AnimBone *out;
    u32 numBones;
    u32 *mask;
    u32 bits;
    u32 bit;
    f32 *stat;
    f32 *dyn;
    Vector4 *plane;
    u32 i;
    s32 idx;
    u16 f;

    out = outp;
    bits = 0;
    bit = 1;
    flags = GETP(anim->flags);
    numBones = anim->numBones;
    mask = GETP(anim->mask);
    if (time < anim->minTime) {
        time = anim->minTime;
    }
    if (time > anim->maxTime) {
        time = anim->maxTime;
    }
    if (mask != NULL) {
        bits = *mask;
    }
    stat = GETP(anim->staticData);
    dyn = values;
    tv.z = time;
    tv.y = time * time;
    tv.x = tv.y * time;
    tv.w = 1.0f;
    idx = 0;
    for (i = 1; i < anim->numFrames; i++) {
        if (time <= GETP(anim->times)[i]) {
            idx = i - 1;
            break;
        }
    }
    plane = &GETP(anim->data)[idx * anim->numCompound];
    for (i = 0; i < anim->numCompound; i++) {
        values[i] = Distance_Plane(&plane[i], &tv);
    }
    for (i = 0; i < numBones; i++) {
        f = *flags;
        if (f & 1) {
            if (bits & bit) {
                out->pos[0] = *dyn++;
            } else {
                out->pos[0] = *stat++;
            }
            if (!(bit <<= 1)) {
                bits = *++mask;
                bit = 1;
            }
        }
        if (f & 2) {
            if (bits & bit) {
                out->pos[1] = *dyn++;
            } else {
                out->pos[1] = *stat++;
            }
            if (!(bit <<= 1)) {
                bits = *++mask;
                bit = 1;
            }
        }
        if (f & 4) {
            if (bits & bit) {
                out->pos[2] = *dyn++;
            } else {
                out->pos[2] = *stat++;
            }
            if (!(bit <<= 1)) {
                bits = *++mask;
                bit = 1;
            }
        }
        if (f & 8) {
            if (bits & bit) {
                out->rot.w = *dyn++;
            } else {
                out->rot.w = *stat++;
            }
            if (!(bit <<= 1)) {
                bits = *++mask;
                bit = 1;
            }
        }
        if (f & 0x10) {
            if (bits & bit) {
                out->rot.x = *dyn++;
            } else {
                out->rot.x = *stat++;
            }
            if (!(bit <<= 1)) {
                bits = *++mask;
                bit = 1;
            }
        }
        if (f & 0x20) {
            if (bits & bit) {
                out->rot.y = *dyn++;
            } else {
                out->rot.y = *stat++;
            }
            if (!(bit <<= 1)) {
                bits = *++mask;
                bit = 1;
            }
        }
        if (f & 0x40) {
            if (bits & bit) {
                out->rot.z = *dyn++;
            } else {
                out->rot.z = *stat++;
            }
            if (!(bit <<= 1)) {
                bits = *++mask;
                bit = 1;
            }
        }
        flags++;
        out++;
    }
}

typedef struct AnimTrack {
    u8 type;
    u8 pad1;
    u16 numKeys;
    PTR32(f32) times;
    PTR32(__typeof__(f32[16])) coefs;
    PTR32(struct AnimTrack) next;
    f32 value[4];
} AnimTrack;

static inline s32 AnimTrack_FindKey(f32 *times, s32 numKeys, f32 time) {
    s32 key = 1;
    u32 step = numKeys;
    f32 *t;

    do {
        t = &times[key];
        step = (step + 1) >> 1;
        if (time > t[0]) {
            key += step;
            if (key >= numKeys) {
                return numKeys - 1;
            }
        } else if (time <= t[-1]) {
            key -= step;
        } else {
            return key;
        }
    } while (key > 0);
    return 0;
}

static inline AnimTrack *AnimTrack_Eval3(AnimTrack *track, f32 time, f32 *out) {
    s32 key = AnimTrack_FindKey(GETP(track->times), track->numKeys, time);

    out[0] = time * (time * (time * GETP(track->coefs)[key][0] + GETP(track->coefs)[key][1]) + GETP(track->coefs)[key][2]) + GETP(track->coefs)[key][3];
    out[1] = time * (time * (time * GETP(track->coefs)[key][4] + GETP(track->coefs)[key][5]) + GETP(track->coefs)[key][6]) + GETP(track->coefs)[key][7];
    out[2] = time * (time * (time * GETP(track->coefs)[key][8] + GETP(track->coefs)[key][9]) + GETP(track->coefs)[key][10]) + GETP(track->coefs)[key][11];
    return GETP(track->next);
}

static inline AnimTrack *AnimTrack_Eval4(AnimTrack *track, f32 time, f32 *out) {
    s32 key = AnimTrack_FindKey(GETP(track->times), track->numKeys, time);

    out[0] = time * (time * (time * GETP(track->coefs)[key][0] + GETP(track->coefs)[key][1]) + GETP(track->coefs)[key][2]) + GETP(track->coefs)[key][3];
    out[1] = time * (time * (time * GETP(track->coefs)[key][4] + GETP(track->coefs)[key][5]) + GETP(track->coefs)[key][6]) + GETP(track->coefs)[key][7];
    out[2] = time * (time * (time * GETP(track->coefs)[key][8] + GETP(track->coefs)[key][9]) + GETP(track->coefs)[key][10]) + GETP(track->coefs)[key][11];
    out[3] = time * (time * (time * GETP(track->coefs)[key][12] + GETP(track->coefs)[key][13]) + GETP(track->coefs)[key][14]) + GETP(track->coefs)[key][15];
    return GETP(track->next);
}


void AnimExtractKeyFrame_CompoundHybrid(Anim *anim, AnimBone *outp, f32 time) {
    Vector4 tv __attribute__((aligned(16)));
    u32 i;
    u32 *mask;
    AnimBone *out;
    u8 *data;
    u32 bit;
    u32 save;
    s32 idx;

    out = outp;
    idx = 0;
    mask = GETP(anim->mask);
    if (time < anim->minTime) {
        time = anim->minTime;
    }
    if (time > anim->maxTime) {
        time = anim->maxTime;
    }
    for (i = 1; i < anim->numFrames; i++) {
        if (time <= GETP(anim->times)[i]) {
            idx = i - 1;
            break;
        }
    }
    data = (u8 *)GETP(anim->data) + ((anim->numKeysPerFrame - anim->numCompound) * sizeof(AnimBone) + anim->numCompound * 2 * sizeof(AnimBone)) * idx;
    tv.y = time * time;
    tv.x = time * tv.y;
    tv.z = time;
    tv.w = 1.0f;
    for (i = 0, bit = 0; i < anim->numBones; i++) {
        if (GETP(anim->flags)[i] & 7) {
            if (*mask & (1 << bit++)) {
                save = out->info;
                Distance_Plane_4((Vector4 *)data, &tv, out->pos);
                out->info = save;
                data += 0x40;
            } else {
                out->pos[0] = ((AnimBone *)data)->pos[0];
                out->pos[1] = ((AnimBone *)data)->pos[1];
                out->pos[2] = ((AnimBone *)data)->pos[2];
                data += 0x20;
            }
            if (bit == 32) {
                bit = 0;
                mask++;
            }
        }
        if (GETP(anim->flags)[i] & 0x78) {
            if (*mask & (1 << bit++)) {
                Distance_Plane_4((Vector4 *)data, &tv, &out->rot);
                data += 0x40;
            } else {
                CopyVector((Vector4 *)&out->rot, (Vector4 *)&((AnimStaticRot *)data)->rot);
                data += 0x20;
            }
            if (bit == 32) {
                bit = 0;
                mask++;
            }
        }
        out++;
    }
}

static inline AnimTrack *AnimTrack_Eval3s(AnimTrack *track, f32 time, f32 *out) {
    s32 numKeys = track->numKeys;
    f32 *times = GETP(track->times);
    u32 step = numKeys;
    s32 key = 1;

    while (1) {
        step = (step + 1) >> 1;
        if (time > times[key]) {
            key += step;
            if (key >= numKeys) {
                key = numKeys - 1;
                break;
            }
        } else if (time <= times[key - 1]) {
            key -= step;
        } else {
            break;
        }
        if (key <= 0) {
            key = 0;
            break;
        }
    }
    out[0] = time * (time * (time * GETP(track->coefs)[key][0] + GETP(track->coefs)[key][1]) + GETP(track->coefs)[key][2]) + GETP(track->coefs)[key][3];
    out[1] = time * (time * (time * GETP(track->coefs)[key][4] + GETP(track->coefs)[key][5]) + GETP(track->coefs)[key][6]) + GETP(track->coefs)[key][7];
    out[2] = time * (time * (time * GETP(track->coefs)[key][8] + GETP(track->coefs)[key][9]) + GETP(track->coefs)[key][10]) + GETP(track->coefs)[key][11];
    return GETP(track->next);
}

void AnimExtractKeyFrame_Hybrid(void *animp, void *outp, f32 time) {
    typedef struct {
        u8 pad0[8];
        f32 minTime;
        f32 maxTime;
        u32 numBones;
        u32 numKeysPerFrame;
        PTR32(u32) mask;
        PTR32(u16) flags;
        PTR32(f32) times;
        PTR32(AnimTrack) data;
    } Anim;
    typedef struct {
        Vector rot;
        f32 pos[3];
        u32 info;
    } Bone;
    u32 i;
    u16 *flags;
    Bone *out;
    u32 bit;
    AnimTrack *track;
    Anim *anim;

    anim = (Anim *)animp;
    out = (Bone *)outp;
    flags = GETP(anim->flags);
    track = GETP(anim->data);
    bit = 1;
    if (time < anim->minTime) {
        time = anim->minTime;
    }
    if (time > anim->maxTime) {
        time = anim->maxTime;
    }
    for (i = 0; i < anim->numBones; i++) {
        if (*flags & 1 | 2 | 4) {
            if (track->type == 3) {
                track = AnimTrack_Eval3s(track, time, out->pos);
            } else {
                out->pos[0] = track->value[0];
                out->pos[1] = track->value[1];
                out->pos[2] = track->value[2];
                track++;
            }
            bit++;
            if (bit >= 32) {
                bit &= 31;
            }
        }
        if (*flags++ & 0x10 | 0x20 | 0x40 | 0x8) {
            if (track->type == 3) {
                track = AnimTrack_Eval4(track, time, (f32 *)&out->rot);
            } else {
                CopyVector(&out->rot, (Vector *)track->value);
                track++;
            }
            bit++;
            if (bit >= 32) {
                bit &= 31;
            }
        }
        out++;
    }
}



/* r3 = anim, f1 = time, r4 = output bones; forwarded unchanged. */
void AnimExtractKeyFrame(void *anim, f32 time, void *out) {
    s32 flags = ((Anim *)anim)->kind;

    if (flags & 2) {
        if (flags & 8) {
            AnimExtractKeyFrame_CompoundHybrid(anim, out, time);
        } else {
            AnimExtractKeyFrame_Compound(anim, out, time);
        }
    } else if (flags & 8) {
        AnimExtractKeyFrame_Hybrid(anim, out, time);
    } else {
        AnimExtractKeyFrame_NonCompound(anim, out, time);
    }
}

void AnimTraverseHeirarchyToMatrices(AnimBone *bones, u32 count, f32 *matrices) {
    Vector posStack[64] __attribute__((aligned(16)));
    Vector rotStack[64];
    Vector rot;
    Vector q;
    Vector pos;
    f32 *mtx;
    u32 i;
    Vector *posSp;
    Vector *rotSp;
    AnimBone *bone;
    u32 n;
    Vector *p;
    AnimBone *child;
    u32 j;

    mtx = matrices;
    bone = bones;
    posSp = posStack;
    rotSp = rotStack;
    rot.w = 1.0f;
    rot.x = rot.y = rot.z = 0.0f;
    memcpy(&pos, bones->pos, 12);
    for (i = 0; i < count; i++) {
        QuaternionMult(&rot, (Vector *)&bone->rot, &q);
        rot = q;
        QuaternionToSubMatrix(&q, mtx);
        mtx[12] = pos.x;
        mtx[13] = pos.y;
        mtx[14] = pos.z;
        if (bone->numChildren != 0) {
            child = &bone[1];
            n = bone->numChildren - 1;
            XformVec((Vector *)mtx, (Vector *)child->pos, &pos);
            if (n != 0) {
                posSp += n;
                p = posSp;
                child = &child[child->next];
                *rotSp++ = q;
                for (j = 0; j < n; j++) {
                    XformVec((Vector *)mtx, (Vector *)child->pos, --p);
                    child = &child[child->next];
                }
            }
        } else {
            pos = *--posSp;
            rotSp -= bone->pop + 1;
            rot = *rotSp++;
        }
        bone++;
        mtx += 16;
    }
}

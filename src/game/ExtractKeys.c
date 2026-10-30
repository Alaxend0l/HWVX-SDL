#include "common.h"

void ExtractF32Key(u8 *track, u8 *state, f32 dt) {
    typedef struct Key {
        f32 value;
        u8 active;
        u8 interp;
        u8 pad[2];
        f32 duration;
    } Key;
    typedef struct KeyTrack {
        u32 count;
        PTR32(Key) keys;
    } KeyTrack;
    typedef struct KeyState {
        f32 value;
        f32 slope;
        f32 time;
        u16 index;
        u16 current;
        u8 reset;
    } KeyState;
    KeyTrack *t = (KeyTrack *)track;
    KeyState *s = (KeyState *)state;
    u32 reset;
    u16 i;
    Key *keys;
    Key *key;
    f32 f;

    reset = 0;
    if (s->reset) {
        s->reset = 0;
        reset = 1;
        s->index = 0;
        s->time = 0.0f;
    }
    if (s->index >= t->count) {
        return;
    }
    s->time += dt;
    if (reset || s->time >= GETP(t->keys)[s->index].duration) {
        s->current = 0;
        for (;;) {
            if (s->time < GETP(t->keys)[s->index].duration) {
                break;
            }
            s->current = s->index;
            s->time -= GETP(t->keys)[s->index].duration;
            s->index++;
            if (s->index >= t->count) {
                for (i = 0; i < t->count; i++) {
                    if (GETP(t->keys)[i].active) {
                        s->index = i;
                        break;
                    }
                }
                if (s->index >= t->count) {
                    s->value = GETP(t->keys)[s->current].value;
                    return;
                }
                break;
            }
        }
        s->slope = GETP(t->keys)[s->index].value - GETP(t->keys)[s->current].value;
        key = &GETP(t->keys)[s->index];
        if (key->interp == 0) {
            f = 0.0f;
            if (key->duration > 0.0f) {
                f = 1.0f / key->duration;
            }
            s->slope *= f;
        }
    }
    keys = GETP(t->keys);
    key = &keys[s->index];
    if (key->interp == 0) {
        s->value = s->slope * s->time + keys[s->current].value;
        return;
    }
    f = s->time / key->duration;
    if (key->interp == 1) {
        f = 1.0f - (1.0f - f) * (1.0f - f);
    } else if (key->interp == 2) {
        f = f * f;
    } else {
        f = 0.0f;
    }
    s->value = f * s->slope + keys[s->current].value;
}

void ExtractColorKey(void *track, void *state, f32 dt) {
    typedef struct KeyColor {
        u8 r;
        u8 g;
        u8 b;
        u8 a;
    } KeyColor;
    typedef struct Key {
        KeyColor c;
        u8 active;
        u8 interp;
        u8 pad[2];
        f32 duration;
    } Key;
    typedef struct KeyTrack {
        u32 count;
        PTR32(Key) keys;
    } KeyTrack;
    typedef struct KeyState {
        f32 slopeR;
        f32 slopeG;
        f32 slopeB;
        f32 slopeA;
        f32 r;
        f32 g;
        f32 b;
        f32 a;
        f32 time;
        KeyColor c;
        u16 index;
        u16 current;
        u8 reset;
    } KeyState;
    KeyTrack *t = (KeyTrack *)track;
    KeyState *s = (KeyState *)state;
    u32 reset;
    u16 i;
    Key *keys;
    Key *key;
    f32 f;

    reset = 0;
    if (s->reset) {
        s->reset = 0;
        reset = 1;
        s->index = 0;
        s->time = 0.0f;
    }
    if (s->index >= t->count) {
        return;
    }
    s->time += dt;
    if (reset || s->time >= GETP(t->keys)[s->index].duration) {
        s->current = 0;
        for (;;) {
            if (s->time < GETP(t->keys)[s->index].duration) {
                break;
            }
            s->current = s->index;
            s->time -= GETP(t->keys)[s->index].duration;
            s->index++;
            if (s->index >= t->count) {
                for (i = 0; i < t->count; i++) {
                    if (GETP(t->keys)[i].active) {
                        s->index = i;
                        break;
                    }
                }
                if (s->index >= t->count) {
                    s->c = GETP(t->keys)[s->current].c;
                    return;
                }
                break;
            }
        }
        s->slopeR = GETP(t->keys)[s->index].c.r - GETP(t->keys)[s->current].c.r;
        s->slopeG = GETP(t->keys)[s->index].c.g - GETP(t->keys)[s->current].c.g;
        s->slopeB = GETP(t->keys)[s->index].c.b - GETP(t->keys)[s->current].c.b;
        s->slopeA = GETP(t->keys)[s->index].c.a - GETP(t->keys)[s->current].c.a;
        key = &GETP(t->keys)[s->index];
        if (key->interp == 0) {
            f = 0.0f;
            if (key->duration > 0.0f) {
                f = 1.0f / key->duration;
            }
            s->slopeR *= f;
            s->slopeG *= f;
            s->slopeB *= f;
            s->slopeA *= f;
        }
    }
    keys = GETP(t->keys);
    key = &keys[s->index];
    if (key->interp == 0) {
        s->r = s->slopeR * s->time + keys[s->current].c.r;
        s->g = s->slopeG * s->time + GETP(t->keys)[s->current].c.g;
        s->b = s->slopeB * s->time + GETP(t->keys)[s->current].c.b;
        s->a = s->slopeA * s->time + GETP(t->keys)[s->current].c.a;
        s->c.r = s->r;
        s->c.g = s->g;
        s->c.b = s->b;
        s->c.a = s->a;
        return;
    }
    f = 0.0f;
    if (key->duration) {
        f = s->time / key->duration;
    }
    if (key->interp == 1) {
        f = 1.0f - (1.0f - f) * (1.0f - f);
    } else if (key->interp == 2) {
        f = f * f;
    } else {
        f = 0.0f;
    }
    s->r = f * s->slopeR + keys[s->current].c.r;
    s->g = f * s->slopeG + GETP(t->keys)[s->current].c.g;
    s->b = f * s->slopeB + GETP(t->keys)[s->current].c.b;
    s->a = f * s->slopeA + GETP(t->keys)[s->current].c.a;
    s->c.r = s->r;
    s->c.g = s->g;
    s->c.b = s->b;
    s->c.a = s->a;
}

void ExtractScaleKey(void *track, void *state, f32 dt) {
    typedef struct Key {
        Vector4 v;
        u8 active;
        u8 interp;
        u8 pad[2];
        f32 duration;
        u8 pad2[8];
    } Key;
    typedef struct KeyTrack {
        u32 count;
        PTR32(Key) keys;
    } KeyTrack;
    typedef struct KeyState {
        Vector4 v;
        f32 slopeX;
        f32 slopeY;
        f32 slopeZ;
        f32 time;
        u16 index;
        u16 current;
        u8 reset;
    } KeyState;
    KeyTrack *t = (KeyTrack *)track;
    KeyState *s = (KeyState *)state;
    u32 reset;
    u16 i;
    Key *keys;
    Key *key;
    f32 f;

    reset = 0;
    if (s->reset) {
        s->reset = 0;
        reset = 1;
        s->index = 0;
        s->time = 0.0f;
    }
    if (s->index >= t->count) {
        return;
    }
    s->time += dt;
    if (reset || s->time >= GETP(t->keys)[s->index].duration) {
        s->current = 0;
        for (;;) {
            if (s->time < GETP(t->keys)[s->index].duration) {
                break;
            }
            s->current = s->index;
            s->time -= GETP(t->keys)[s->index].duration;
            s->index++;
            if (s->index >= t->count) {
                for (i = 0; i < t->count; i++) {
                    if (GETP(t->keys)[i].active) {
                        s->index = i;
                        break;
                    }
                }
                if (s->index >= t->count) {
                    s->v = GETP(t->keys)[s->current].v;
                    return;
                }
                break;
            }
        }
        s->slopeX = GETP(t->keys)[s->index].v.x - GETP(t->keys)[s->current].v.x;
        s->slopeY = GETP(t->keys)[s->index].v.y - GETP(t->keys)[s->current].v.y;
        s->slopeZ = GETP(t->keys)[s->index].v.z - GETP(t->keys)[s->current].v.z;
        key = &GETP(t->keys)[s->index];
        if (key->interp == 0) {
            f = 0.0f;
            if (key->duration > 0.0f) {
                f = 1.0f / key->duration;
            }
            s->slopeX *= f;
            s->slopeY *= f;
            s->slopeZ *= f;
        }
    }
    keys = GETP(t->keys);
    key = &keys[s->index];
    if (key->interp == 0) {
        s->v.x = s->slopeX * s->time + keys[s->current].v.x;
        s->v.y = s->slopeY * s->time + GETP(t->keys)[s->current].v.y;
        s->v.z = s->slopeZ * s->time + GETP(t->keys)[s->current].v.z;
        return;
    }
    f = 0.0f;
    if (key->duration) {
        f = s->time / key->duration;
    }
    if (key->interp == 1) {
        f = 1.0f - (1.0f - f) * (1.0f - f);
    } else if (key->interp == 2) {
        f = f * f;
    } else {
        f = 0.0f;
    }
    s->v.x = f * s->slopeX + keys[s->current].v.x;
    s->v.y = f * s->slopeY + GETP(t->keys)[s->current].v.y;
    s->v.z = f * s->slopeZ + GETP(t->keys)[s->current].v.z;
}

void ExtractPointKey(u8 *track, u8 *state, f32 dt) {
    typedef struct Key {
        f32 x;
        f32 y;
        f32 duration;
        u8 active;
        u8 interp;
        u8 pad[2];
    } Key;
    typedef struct KeyTrack {
        u32 count;
        PTR32(Key) keys;
    } KeyTrack;
    typedef struct KeyState {
        f32 x;
        f32 y;
        f32 slopeX;
        f32 slopeY;
        f32 time;
        u16 index;
        u16 current;
        u8 reset;
    } KeyState;
    KeyTrack *t = (KeyTrack *)track;
    KeyState *s = (KeyState *)state;
    u32 reset;
    u16 i;
    Key *keys;
    Key *key;
    f32 f;

    reset = 0;
    if (s->reset) {
        s->reset = 0;
        reset = 1;
        s->index = 0;
        s->time = 0.0f;
    }
    if (s->index >= t->count) {
        return;
    }
    s->time += dt;
    if (reset || s->time >= GETP(t->keys)[s->index].duration) {
        s->current = 0;
        for (;;) {
            if (s->time < GETP(t->keys)[s->index].duration) {
                break;
            }
            s->current = s->index;
            s->time -= GETP(t->keys)[s->index].duration;
            s->index++;
            if (s->index >= t->count) {
                for (i = 0; i < t->count; i++) {
                    if (GETP(t->keys)[i].active) {
                        s->index = i;
                        break;
                    }
                }
                if (s->index >= t->count) {
                    s->x = GETP(t->keys)[s->current].x;
                    s->y = GETP(t->keys)[s->current].y;
                    return;
                }
                break;
            }
        }
        s->slopeX = GETP(t->keys)[s->index].x - GETP(t->keys)[s->current].x;
        s->slopeY = GETP(t->keys)[s->index].y - GETP(t->keys)[s->current].y;
        key = &GETP(t->keys)[s->index];
        if (key->interp == 0) {
            f = 0.0f;
            if (key->duration > 0.0f) {
                f = 1.0f / key->duration;
            }
            s->slopeX *= f;
            s->slopeY *= f;
        }
    }
    keys = GETP(t->keys);
    key = &keys[s->index];
    if (key->interp == 0) {
        s->x = s->slopeX * s->time + keys[s->current].x;
        s->y = s->slopeY * s->time + GETP(t->keys)[s->current].y;
        return;
    }
    f = s->time / key->duration;
    if (key->interp == 1) {
        f = 1.0f - (1.0f - f) * (1.0f - f);
    } else if (key->interp == 2) {
        f = f * f;
    } else {
        f = 0.0f;
    }
    s->x = f * s->slopeX + keys[s->current].x;
    s->y = f * s->slopeY + GETP(t->keys)[s->current].y;
}

void ExtractActiveKey(u8 *track, u8 *state, f32 dt) {
    typedef struct Key {
        u8 value;
        u8 active;
        u8 pad[2];
        f32 duration;
    } Key;
    typedef struct KeyTrack {
        u32 count;
        PTR32(Key) keys;
    } KeyTrack;
    typedef struct KeyState {
        f32 time;
        u16 index;
        u16 current;
        u8 value;
        u8 reset;
    } KeyState;
    KeyTrack *t = (KeyTrack *)track;
    KeyState *s = (KeyState *)state;
    u32 reset;
    u16 i;

    reset = 0;
    if (s->reset) {
        s->reset = 0;
        reset = 1;
        s->index = 0;
        s->time = 0.0f;
    }
    if (s->index >= t->count) {
        return;
    }
    s->time += dt;
    if (!reset && !(s->time >= GETP(t->keys)[s->index].duration)) {
        return;
    }
    s->current = 0;
    for (;;) {
        if (s->time < GETP(t->keys)[s->index].duration) {
            break;
        }
        s->current = s->index;
        s->time -= GETP(t->keys)[s->index].duration;
        s->index++;
        if (s->index >= t->count) {
            for (i = 0; i < t->count; i++) {
                if (GETP(t->keys)[i].active) {
                    s->index = i;
                    break;
                }
            }
            if (s->index >= t->count) {
                s->value = GETP(t->keys)[s->current].value;
                return;
            }
            break;
        }
    }
    s->value = GETP(t->keys)[s->current].value;
}

void ExtractUVKey(void *track, void *state, f32 dt) {
    typedef struct UVKey {
        f32 u0;
        f32 v0;
        f32 u1;
        f32 v1;
        f32 u2;
        f32 v2;
        f32 u3;
        f32 v3;
        u8 active;
        u8 interp;
        u8 pad[2];
        f32 duration;
    } UVKey;
    typedef struct UVKeyTrack {
        u32 count;
        PTR32(UVKey) keys;
    } UVKeyTrack;
    typedef struct UVKeyState {
        UVKey v;
        f32 slopeU0;
        f32 slopeV0;
        f32 slopeU1;
        f32 slopeV1;
        f32 slopeU2;
        f32 slopeV2;
        f32 slopeU3;
        f32 slopeV3;
        u8 pad[8];
        f32 time;
        u16 index;
        u16 current;
        u8 reset;
    } UVKeyState;
    UVKeyTrack *t = (UVKeyTrack *)track;
    UVKeyState *s = (UVKeyState *)state;
    u32 reset;
    u16 i;
    UVKey *keys;
    UVKey *key;
    f32 f;

    reset = 0;
    if (s->reset) {
        s->reset = 0;
        reset = 1;
        s->index = 0;
        s->time = 0.0f;
    }
    if (s->index >= t->count) {
        return;
    }
    s->time += dt;
    if (reset || s->time >= GETP(t->keys)[s->index].duration) {
        s->current = 0;
        for (;;) {
            if (s->time < GETP(t->keys)[s->index].duration) {
                break;
            }
            s->current = s->index;
            s->time -= GETP(t->keys)[s->index].duration;
            s->index++;
            if (s->index >= t->count) {
                for (i = 0; i < t->count; i++) {
                    if (GETP(t->keys)[i].active) {
                        s->index = i;
                        break;
                    }
                }
                if (s->index >= t->count) {
                    s->v = GETP(t->keys)[s->current];
                    return;
                }
                break;
            }
        }
        s->slopeU0 = GETP(t->keys)[s->index].u0 - GETP(t->keys)[s->current].u0;
        s->slopeV0 = GETP(t->keys)[s->index].v0 - GETP(t->keys)[s->current].v0;
        s->slopeU3 = GETP(t->keys)[s->index].u3 - GETP(t->keys)[s->current].u3;
        s->slopeV3 = GETP(t->keys)[s->index].v3 - GETP(t->keys)[s->current].v3;
        key = &GETP(t->keys)[s->index];
        if (key->interp == 0) {
            f = 0.0f;
            if (key->duration > 0.0f) {
                f = 1.0f / key->duration;
            }
            s->slopeU0 *= f;
            s->slopeV0 *= f;
            s->slopeU3 *= f;
            s->slopeV3 *= f;
        }
    }
    keys = GETP(t->keys);
    key = &keys[s->index];
    if (key->interp == 0) {
        s->v.u0 = s->slopeU0 * s->time + keys[s->current].u0;
        s->v.v0 = s->slopeV0 * s->time + GETP(t->keys)[s->current].v0;
        s->v.u3 = s->slopeU3 * s->time + GETP(t->keys)[s->current].u3;
        s->v.v3 = s->slopeV3 * s->time + GETP(t->keys)[s->current].v3;
        return;
    }
    f = s->time / key->duration;
    if (key->interp == 1) {
        f = 1.0f - (1.0f - f) * (1.0f - f);
    } else if (key->interp == 2) {
        f = f * f;
    } else {
        f = 0.0f;
    }
    s->v.u0 = f * s->slopeU0 + keys[s->current].u0;
    s->v.v0 = f * s->slopeV0 + GETP(t->keys)[s->current].v0;
    s->v.u1 = f * s->slopeU1 + GETP(t->keys)[s->current].u1;
    s->v.v1 = f * s->slopeV1 + GETP(t->keys)[s->current].v1;
    s->v.u2 = f * s->slopeU2 + GETP(t->keys)[s->current].u2;
    s->v.v2 = f * s->slopeV2 + GETP(t->keys)[s->current].v2;
    s->v.u3 = f * s->slopeU3 + GETP(t->keys)[s->current].u3;
    s->v.v3 = f * s->slopeV3 + GETP(t->keys)[s->current].v3;
}

void ExtractTextureKey(TextureKeyAnim *anim, TextureKeyState *state, f32 dt) {
    if (state->reset) {
        if (anim->numKeys == 0) {
            state->texture = 0;
            return;
        }
        state->texture = GETP(anim->keys)[0];
        state->key = 0;
        state->time = 0.0f;
        state->reset = 0;
    }
    state->time += dt;
    if (state->time >= anim->frameTime) {
        state->key++;
        if (state->key == anim->numKeys) {
            state->key = 0;
        }
        state->texture = GETP(anim->keys)[state->key];
        state->time -= anim->frameTime;
    }
}


s32 ExtractSoundKey(u8 *track, u8 *state, void *pos, f32 dt) {
    typedef struct Sound {
        u8 type;
        u8 unk1[2];
        u8 owner;
        u8 unk4[4];
        f32 volume;
        f32 pitch;
        f32 pitchRange;
        u32 sample;            /* sound effect id (pack sound relocation) */
        PTR32(u8) range;
    } Sound;
    typedef struct Key {
        f32 duration;
        u8 active;
        u8 pad[3];
        u32 numSounds;
        PTR32(PTR32(Sound)) sounds;
    } Key;
    typedef struct KeyTrack {
        u32 count;
        PTR32(Key) keys;
    } KeyTrack;
    typedef struct KeyState {
        s32 unk0;
        u16 index;
        u16 current;
        f32 time;
        u8 reset;
    } KeyState;
    KeyTrack *t = (KeyTrack *)track;
    KeyState *s = (KeyState *)state;
    u32 reset;
    u16 i;
    s32 ret;
    Sound *snd;
    s32 idx;

    s->unk0 = 0;
    reset = 0;
    ret = -1;
    if (s->reset) {
        s->reset = 0;
        reset = 1;
        s->index = 0;
        s->time = 0.0f;
    }
    if (s->index >= t->count) {
        return -2;
    }
    s->time += dt;
    if (reset || s->time >= GETP(t->keys)[s->index].duration) {
        s->current = 0;
        for (;;) {
            if (s->time < GETP(t->keys)[s->index].duration) {
                break;
            }
            s->current = s->index;
            if (GETP(t->keys)[s->index].numSounds != 0) {
                idx = RandomAdd_s32(0, GETP(t->keys)[s->index].numSounds);
                snd = GETP(GETP(GETP(t->keys)[s->index].sounds)[idx]);
                if (snd != NULL) {
                    if (snd->type == 1) {
                        void *voice;
                        f32 pitch;

                        voice = SoundAudioSample_Play(snd->sample);
                        pitch = RandomAdd(snd->pitch, snd->pitchRange);
                        SoundAudioSample_Set(voice, 0, 32767.0f * snd->volume);
                        SoundAudioSample_Set(voice, 1, 65536.0f * pitch);
                    } else {
                        s32 src;
                        u32 sample;  /* sound effect id; SoundSource_Set copies 4 bytes */
                        f32 pitch;

                        src = SoundManager_CreateSource(snd->owner);
                        sample = snd->sample;
                        pitch = RandomAdd(snd->pitch, snd->pitchRange);
                        SoundSource_Set(src, 8, &sample);
                        SoundSource_Set(src, 5, pos);
                        SoundSource_Set(src, 0, &snd->volume);
                        SoundSource_Set(src, 1, &pitch);
                        if (snd->type == 0) {
                            SoundSource_Set(src, 2, GETP(snd->range));
                            SoundSource_Set(src, 3, GETP(snd->range) + 4);
                        }
                        SoundSource_Play(src);
                        if (snd->owner) {
                            ret = src;
                        }
                    }
                }
            }
            s->time -= GETP(t->keys)[s->index].duration;
            s->index++;
            if (s->index >= t->count) {
                for (i = 0; i < t->count; i++) {
                    if (GETP(t->keys)[i].active) {
                        s->index = i;
                        break;
                    }
                }
                if (s->index >= t->count) {
                    if (ret < 0) {
                        return -2;
                    }
                    return ret;
                }
                break;
            }
        }
    }
    return ret;
}


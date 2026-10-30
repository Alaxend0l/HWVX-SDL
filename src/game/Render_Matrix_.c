#include "common.h"

typedef struct GfxWindow {
    f32 unk0[4][4];
    f32 unk40[4][4];
    f32 unk80[4][4];
    f32 unkC0[4][4];
    f32 unk100[4][4];
    u8 unk140[0x180 - 0x140];
    Vector4 cameraPos;
    Vector4 cameraRight;
    Vector4 cameraUp;
    Vector4 cameraAt;
    Vector4 clipPlanes[6];
    Vector4 shadowClipPlanes[5];
    f32 fov;
    f32 unk274;
    u8 unk278[0x280 - 0x278];
    u32 width;
    u32 height;
    u8 unk288[0x2B0 - 0x288];
    u32 unk2B0;
    u32 unk2B4;
    u32 unk2B8;
    u32 unk2BC;
    u8 unk2C0[0x2CC - 0x2C0];
    f32 unk2CC;
} GfxWindow;

typedef struct GfxState {
    u8 unk0[0x93];
    u8 unk93;
    u8 unk94[0xA0 - 0x94];
} GfxState;

extern GfxState g_state;
extern f32 g_identity[4][4];
static f32 GXCameraSpace[3][4];
Vector4 g_cameraPos_Backup;
Vector4 g_cameraFacing_BackUp;
Vector4 g_cameraUp_BackUp;
Vector4 g_cameraRight_BackUp;

GfxWindow *GetRenderWindow(void);

void Gfx_SetWorldMatrix(f32 (*matrix)[4]) {
    if (matrix == NULL) {
        if (g_state.unk93) {
            return;
        }
        g_state.unk93 = 1;
        matrix = g_identity;
    } else {
        g_state.unk93 = 0;
    }
    memcpy(GetRenderWindow()->unk0, matrix, 0x40);
    MultiplyMatrix4x4(GetRenderWindow()->unk0, GetRenderWindow()->unk40, GetRenderWindow()->unk80);
    MultiplyMatrix4x4(GetRenderWindow()->unk80, GetRenderWindow()->unkC0, GetRenderWindow()->unk100);
    MatrixToMtxTranspose(GXCameraSpace, GetRenderWindow()->unk80);
    GXLoadPosMtxImm(GXCameraSpace, 0);
    GXSetCurrentMtx(0);
}

void Gfx_SetCameraMatrix(Vector4 *pos, Vector4 *right, Vector4 *up, Vector4 *at) {
    f32 m[4][4] __attribute__((aligned(16)));
    Vector4 defPos __attribute__((aligned(16)));
    Vector4 defRight __attribute__((aligned(16)));
    Vector4 defUp __attribute__((aligned(16)));
    Vector4 defAt __attribute__((aligned(16)));

    if (pos == NULL) {
        pos = &defPos;
        defPos.x = 0.0f;
        defPos.y = 0.0f;
        defPos.z = 0.0f;
    }
    if (right == NULL) {
        right = &defRight;
        up = &defUp;
        at = &defAt;
        defRight.x = 0.0f;
        defRight.y = 0.0f;
        defRight.z = -1.0f;
        defUp.x = 0.0f;
        defUp.y = 1.0f;
        defUp.z = 0.0f;
        defAt.x = -1.0f;
        defAt.y = 0.0f;
        defAt.z = 0.0f;
    }
    GetRenderWindow()->cameraPos = *pos;
    GetRenderWindow()->cameraRight = *right;
    GetRenderWindow()->cameraUp = *up;
    GetRenderWindow()->cameraAt = *at;
    m[0][0] = -at->x;
    m[1][0] = -at->y;
    m[2][0] = -at->z;
    m[0][1] = up->x;
    m[1][1] = up->y;
    m[2][1] = up->z;
    m[0][2] = -right->x;
    m[1][2] = -right->y;
    m[2][2] = -right->z;
    m[3][0] = -(m[0][0] * pos->x + m[1][0] * pos->y + m[2][0] * pos->z);
    m[3][1] = -(m[0][1] * pos->x + m[1][1] * pos->y + m[2][1] * pos->z);
    m[3][2] = -(m[0][2] * pos->x + m[1][2] * pos->y + m[2][2] * pos->z);
    m[2][3] = 0.0f;
    m[1][3] = 0.0f;
    m[0][3] = 0.0f;
    m[3][3] = 1.0f;
    memcpy(GetRenderWindow()->unk40, m, 0x40);
    MultiplyMatrix4x4(GetRenderWindow()->unk0, GetRenderWindow()->unk40, GetRenderWindow()->unk80);
    MultiplyMatrix4x4(GetRenderWindow()->unk80, GetRenderWindow()->unkC0, GetRenderWindow()->unk100);
    MatrixToMtxTranspose(GXCameraSpace, GetRenderWindow()->unk80);
    GXLoadPosMtxImm(GXCameraSpace, 0);
    GXSetCurrentMtx(0);
    Render_SetClipPlanes(GetRenderWindow()->fov);
    g_state.unk93 = 0;
}

void Gfx_SetProjectionMatrix(f32 fov) {
    f32 mtx[4][4];
    f32 angle;
    f32 value;

    angle = fov;
    value = sin(0.017453292f * (0.9f * fov));
    *(f32 *)((u8 *)GetRenderWindow() + 0x274) = value;
    *(f32 *)((u8 *)GetRenderWindow() + 0x270) = fov;
    fov = 90.0f / fov;
    value = -(fov * *(u32 *)((u8 *)GetRenderWindow() + 0x2B4));
    *(f32 *)((u8 *)GetRenderWindow() + 0xC0) = value;
    value = 1.333f * (fov * *(u32 *)((u8 *)GetRenderWindow() + 0x2BC));
    *(f32 *)((u8 *)GetRenderWindow() + 0xD4) = value;
    value = 1.0f;
    *(f32 *)((u8 *)GetRenderWindow() + 0xEC) = value;
    value = 20000.0f;
    *(f32 *)((u8 *)GetRenderWindow() + 0xF8) = value;
    value = *(u32 *)((u8 *)GetRenderWindow() + 0x2B4);
    *(f32 *)((u8 *)GetRenderWindow() + 0xE0) = value;
    value = *(u32 *)((u8 *)GetRenderWindow() + 0x2BC);
    *(f32 *)((u8 *)GetRenderWindow() + 0xE4) = value;
    Gfx_SetCameraMatrix((u8 *)GetRenderWindow() + 0x180, (u8 *)GetRenderWindow() + 0x190, (u8 *)GetRenderWindow() + 0x1A0, (u8 *)GetRenderWindow() + 0x1B0);
    value = 1.0 / sin(0.008726646f * Gfx_GetProjectionMatrix());
    *(f32 *)((u8 *)GetRenderWindow() + 0x2CC) = value;
    value = *(u32 *)((u8 *)GetRenderWindow() + 0x284);
    value = *(u32 *)((u8 *)GetRenderWindow() + 0x280) / value;
    fov = *(u32 *)((u8 *)GetRenderWindow() + 0x280);
    C_MTXPerspective(mtx, 1.0714285f * angle * *(u32 *)((u8 *)GetRenderWindow() + 0x284) / fov, value, 1.0f, 20000.0f);
    GXSetProjection(mtx, 0);
}

f32 Gfx_GetProjectionMatrix(void) {
    return GetRenderWindow()->fov;
}

void Gfx_SetOrthoMatrix(void) {
    f32 mtx[3][4];
    f32 proj[3][4];
    f32 width;

    width = GetRenderWindow()->width;
    C_MTXOrtho(proj, 0.0f, GetRenderWindow()->height, 0.0f, width, 1.0f, -1.0f);
    GXSetProjection(proj, 1);
    PSMTXIdentity(mtx);
    GXLoadPosMtxImm(mtx, 0);
    GXSetCurrentMtx(0);
}

void Render_SaveCamera(void) {
    g_cameraPos_Backup = GetRenderWindow()->cameraPos;
    g_cameraFacing_BackUp = GetRenderWindow()->cameraRight;
    g_cameraUp_BackUp = GetRenderWindow()->cameraUp;
    g_cameraRight_BackUp = GetRenderWindow()->cameraAt;
}

void Render_LoadCamera(void) {
    GetRenderWindow()->cameraPos = g_cameraPos_Backup;
    GetRenderWindow()->cameraRight = g_cameraFacing_BackUp;
    GetRenderWindow()->cameraUp = g_cameraUp_BackUp;
    GetRenderWindow()->cameraAt = g_cameraRight_BackUp;
    Gfx_SetProjectionMatrix(Gfx_GetProjectionMatrix());
}

void Render_SetClipPlanes(f32 fov) {
    Vector4 out __attribute__((aligned(16)));
    Vector4 in __attribute__((aligned(16)));
    Matrix4 m;
    f32 one;
    f32 adj;
    f32 c;
    f32 s;
    f32 hAngle;
    f32 vAngle;
    f32 vc;
    f32 vs;
    f32 hc;
    f32 hs;
    f32 nhs;

    adj = fov + 8.7f * sin(2.0 * (0.017453293 * fov));
    fov = 0.5f * (0.017453292f * adj);
    c = cos(fov);
    s = sin(fov);
    fov = 0.75f * s / c;
    vAngle = atan(fov);
    hAngle = atan(fov * (4096.0f / GetRenderWindow()->width));
    vc = cos(vAngle);
    vs = sin(vAngle);
    hc = cos(hAngle);
    hs = sin(hAngle);
    one = 1.0f;
    cos(one);

    m.v[0].x = GetRenderWindow()->cameraAt.x;
    m.v[0].y = GetRenderWindow()->cameraAt.y;
    m.v[0].z = GetRenderWindow()->cameraAt.z;
    m.v[0].w = 0.0f;
    m.v[1].x = GetRenderWindow()->cameraUp.x;
    m.v[1].y = GetRenderWindow()->cameraUp.y;
    m.v[1].z = GetRenderWindow()->cameraUp.z;
    m.v[1].w = 0.0f;
    m.v[2].x = GetRenderWindow()->cameraRight.x;
    m.v[2].y = GetRenderWindow()->cameraRight.y;
    m.v[2].z = GetRenderWindow()->cameraRight.z;
    m.v[2].w = 0.0f;
    m.v[3].x = 0.0f;
    m.v[3].y = 0.0f;
    m.v[3].z = 0.0f;
    m.v[3].w = 1.0f;

    in.x = 0.0f;
    in.y = vc;
    in.z = -vs;
    XformVecOrientation(&m, &in, &out);
    GetRenderWindow()->clipPlanes[0].x = out.x;
    GetRenderWindow()->clipPlanes[0].y = out.y;
    GetRenderWindow()->clipPlanes[0].z = out.z;
    GetRenderWindow()->clipPlanes[0].w = -PSVECDotProduct(&out, &GetRenderWindow()->cameraPos);

    in.x = 0.0f;
    in.y = -vc;
    in.z = -vs;
    XformVecOrientation(&m, &in, &out);
    GetRenderWindow()->clipPlanes[1].x = out.x;
    GetRenderWindow()->clipPlanes[1].y = out.y;
    GetRenderWindow()->clipPlanes[1].z = out.z;
    GetRenderWindow()->clipPlanes[1].w = -PSVECDotProduct(&out, &GetRenderWindow()->cameraPos);

    in.x = -c;
    in.y = 0.0f;
    in.z = -s;
    XformVecOrientation(&m, &in, &out);
    GetRenderWindow()->clipPlanes[2].x = out.x;
    GetRenderWindow()->clipPlanes[2].y = out.y;
    GetRenderWindow()->clipPlanes[2].z = out.z;
    GetRenderWindow()->clipPlanes[2].w = -PSVECDotProduct(&out, &GetRenderWindow()->cameraPos);

    in.x = c;
    in.y = 0.0f;
    in.z = -s;
    XformVecOrientation(&m, &in, &out);
    GetRenderWindow()->clipPlanes[3].x = out.x;
    GetRenderWindow()->clipPlanes[3].y = out.y;
    GetRenderWindow()->clipPlanes[3].z = out.z;
    GetRenderWindow()->clipPlanes[3].w = -PSVECDotProduct(&out, &GetRenderWindow()->cameraPos);

    GetRenderWindow()->clipPlanes[4].x = -GetRenderWindow()->cameraRight.x;
    GetRenderWindow()->clipPlanes[4].y = -GetRenderWindow()->cameraRight.y;
    GetRenderWindow()->clipPlanes[4].z = -GetRenderWindow()->cameraRight.z;
    GetRenderWindow()->clipPlanes[4].w = PSVECDotProduct(&GetRenderWindow()->cameraRight, &GetRenderWindow()->cameraPos);

    nhs = -hs;
    in.x = 0.0f;
    in.y = hc;
    in.z = nhs;
    XformVecOrientation(&m, &in, &out);
    GetRenderWindow()->shadowClipPlanes[0].x = out.x;
    GetRenderWindow()->shadowClipPlanes[0].y = out.y;
    GetRenderWindow()->shadowClipPlanes[0].z = out.z;
    GetRenderWindow()->shadowClipPlanes[0].w = -PSVECDotProduct(&out, &GetRenderWindow()->cameraPos);

    in.x = 0.0f;
    in.y = -hc;
    in.z = nhs;
    XformVecOrientation(&m, &in, &out);
    GetRenderWindow()->shadowClipPlanes[1].x = out.x;
    GetRenderWindow()->shadowClipPlanes[1].y = out.y;
    GetRenderWindow()->shadowClipPlanes[1].z = out.z;
    GetRenderWindow()->shadowClipPlanes[1].w = -PSVECDotProduct(&out, &GetRenderWindow()->cameraPos);

    in.x = -hc;
    in.y = 0.0f;
    in.z = nhs;
    XformVecOrientation(&m, &in, &out);
    GetRenderWindow()->shadowClipPlanes[2].x = out.x;
    GetRenderWindow()->shadowClipPlanes[2].y = out.y;
    GetRenderWindow()->shadowClipPlanes[2].z = out.z;
    GetRenderWindow()->shadowClipPlanes[2].w = -PSVECDotProduct(&out, &GetRenderWindow()->cameraPos);

    in.x = hc;
    in.y = 0.0f;
    in.z = nhs;
    XformVecOrientation(&m, &in, &out);
    GetRenderWindow()->shadowClipPlanes[3].x = out.x;
    GetRenderWindow()->shadowClipPlanes[3].y = out.y;
    GetRenderWindow()->shadowClipPlanes[3].z = out.z;
    GetRenderWindow()->shadowClipPlanes[3].w = -PSVECDotProduct(&out, &GetRenderWindow()->cameraPos);

    GetRenderWindow()->shadowClipPlanes[4].x = -GetRenderWindow()->cameraRight.x;
    GetRenderWindow()->shadowClipPlanes[4].y = -GetRenderWindow()->cameraRight.y;
    GetRenderWindow()->shadowClipPlanes[4].z = -GetRenderWindow()->cameraRight.z;
    GetRenderWindow()->shadowClipPlanes[4].w = PSVECDotProduct(&GetRenderWindow()->cameraRight, &GetRenderWindow()->cameraPos);
}

void MultiplyMatrix4x4(f32 a[4][4], f32 b[4][4], f32 out[4][4]) {
    out[0][0] = b[0][0] * a[0][0] + b[1][0] * a[0][1] + b[2][0] * a[0][2] + b[3][0] * a[0][3];
    out[0][1] = b[0][1] * a[0][0] + b[1][1] * a[0][1] + b[2][1] * a[0][2] + b[3][1] * a[0][3];
    out[0][2] = b[0][2] * a[0][0] + b[1][2] * a[0][1] + b[2][2] * a[0][2] + b[3][2] * a[0][3];
    out[0][3] = b[0][3] * a[0][0] + b[1][3] * a[0][1] + b[2][3] * a[0][2] + b[3][3] * a[0][3];
    out[1][0] = b[0][0] * a[1][0] + b[1][0] * a[1][1] + b[2][0] * a[1][2] + b[3][0] * a[1][3];
    out[1][1] = b[0][1] * a[1][0] + b[1][1] * a[1][1] + b[2][1] * a[1][2] + b[3][1] * a[1][3];
    out[1][2] = b[0][2] * a[1][0] + b[1][2] * a[1][1] + b[2][2] * a[1][2] + b[3][2] * a[1][3];
    out[1][3] = b[0][3] * a[1][0] + b[1][3] * a[1][1] + b[2][3] * a[1][2] + b[3][3] * a[1][3];
    out[2][0] = b[0][0] * a[2][0] + b[1][0] * a[2][1] + b[2][0] * a[2][2] + b[3][0] * a[2][3];
    out[2][1] = b[0][1] * a[2][0] + b[1][1] * a[2][1] + b[2][1] * a[2][2] + b[3][1] * a[2][3];
    out[2][2] = b[0][2] * a[2][0] + b[1][2] * a[2][1] + b[2][2] * a[2][2] + b[3][2] * a[2][3];
    out[2][3] = b[0][3] * a[2][0] + b[1][3] * a[2][1] + b[2][3] * a[2][2] + b[3][3] * a[2][3];
    out[3][0] = b[0][0] * a[3][0] + b[1][0] * a[3][1] + b[2][0] * a[3][2] + b[3][0] * a[3][3];
    out[3][1] = b[0][1] * a[3][0] + b[1][1] * a[3][1] + b[2][1] * a[3][2] + b[3][1] * a[3][3];
    out[3][2] = b[0][2] * a[3][0] + b[1][2] * a[3][1] + b[2][2] * a[3][2] + b[3][2] * a[3][3];
    out[3][3] = b[0][3] * a[3][0] + b[1][3] * a[3][1] + b[2][3] * a[3][2] + b[3][3] * a[3][3];
}

void MatrixToMtxTranspose(f32 dst[3][4], f32 src[4][4]) {
    dst[0][0] = src[0][0];
    dst[0][1] = src[1][0];
    dst[0][2] = src[2][0];
    dst[0][3] = src[3][0];
    dst[1][0] = src[0][1];
    dst[1][1] = src[1][1];
    dst[1][2] = src[2][1];
    dst[1][3] = src[3][1];
    dst[2][0] = src[0][2];
    dst[2][1] = src[1][2];
    dst[2][2] = src[2][2];
    dst[2][3] = src[3][2];
}

s32 isInWindow(f32 *pos) {
    if (GetRenderWindow()->unk2B0 > pos[0] || GetRenderWindow()->unk2B4 < pos[0]) {
        return 0;
    }
    if (GetRenderWindow()->unk2B8 > pos[1] || GetRenderWindow()->unk2BC < pos[1]) {
        return 0;
    }
    pos[0] -= GetRenderWindow()->unk2B0;
    pos[1] -= GetRenderWindow()->unk2B8;
    return 1;
}

extern f32 GXCameraSpace[3][4];
void Gfx_TransformVector(f32 *in, f32 *out) {
    f32 pm[7];
    f32 vp[6];

    Render_StateSetScissor();
    GXGetViewportv(vp);
    GXGetProjectionv(pm);
    vp[3] *= 1.0714f;
    GXProject(in[0], in[1], in[2], GXCameraSpace, pm, vp, &out[0], &out[1], &out[2]);
    out[3] = -(GetRenderWindow()->unk100[3][3] +
               (in[2] * GetRenderWindow()->unk100[2][3] +
                (in[0] * GetRenderWindow()->unk100[0][3] +
                 in[1] * GetRenderWindow()->unk100[1][3])));
}


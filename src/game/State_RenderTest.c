#include "common.h"


u16 g_dirt;
s32 pCar;
s32 g_pCamera;  /* Camera handle */
#define CAM ((Camera *)PTR(g_pCamera))
f32 g_cameraAngle;
f32 g_yaw;
f32 g_cameraRange = 60.0f;

Vector4 g_carPos[80];
f32 g_carRot[80];
Vector4 g_carCamPos;
Vector4 g_carDrivePos;


s32 StateRenderTest_Init(void) {
    extern s32 g_pCamera;
    extern s8 g_cameraManagerPause;
    extern s32 pCar;
    extern u16 g_dirt;
    extern s32 g_debugDataFlag;
    s32 j;
    s32 i;

    Geometry_Init(1);
    Gfx_SetEnviroMapMode(1);
    CameraManager_Init();
    g_pCamera = HND(CameraManager_GetNewCamera());
    g_cameraManagerPause = 1;
    Gfx_SetProjectionMatrix(90.0f);
    Gfx_SetCameraMatrix(NULL, NULL, NULL, NULL);
    Gfx_SetWorldMatrix(NULL);
    Gfx_StateTextureBlendMode(2);
    pCar = HND(Geometry_Get("car"));
    ProcessManager_Init();
    ProcessManager_Add("TimeHandler", NULL, TimeHandler_Heartbeat, 0, 0);
    ProcessManager_Add("Input", Input_Init, Input_Heartbeat, 0, 0);
    Gfx_SetDebugFont(Gfx_FontManager_AddFont(Gfx_LoadTexture("font36"), 1, 18.285f, 18.285f, 18.285f));
    g_dirt = Gfx_LoadTexture("dirt");
    for (j = 0; j < 8; j++) {
        for (i = 0; i < 10; i++) {
            g_carPos[j * 10 + i].x = i * 15 - 25.0f;
            g_carPos[j * 10 + i].z = 60.0f + j * 15;
            g_carRot[j * 10 + i] = RandomNumberGenerator_GetRandomFloat(0.0f, 6.2831855f);
        }
    }
    g_carCamPos.x = 50.0f;
    g_carCamPos.y = -20.0f;
    g_carCamPos.z = 120.0f;
    g_debugDataFlag = 100;
    return 1;
}


s32 StateRenderTest_Heartbeat(void) {
    extern s32 g_pCamera;
    extern s32 pCar;
    extern u16 g_dirt;
    extern f32 g_cameraAngle;
    extern f32 g_cameraRange;
    extern f32 g_yaw;
    extern Vector4 g_jBasis;
    extern Matrix4 g_identity;
    Matrix4 mtx;
    u8 stats[0x100];
    f32 *tri;
    s32 i;

    ShowStats_StartFrame();
    ProcessManager_Heartbeat(0);
    if (Input_KeyDown(0, 2)) {
        g_cameraAngle -= 0.075f;
        if (g_cameraAngle < 0.0f) {
            g_cameraAngle += 6.2831855f;
        }
    }
    if (Input_KeyDown(0, 3)) {
        g_cameraAngle += 0.075f;
        if (g_cameraAngle > 6.2831855f) {
            g_cameraAngle -= 6.2831855f;
        }
    }
    if (Input_KeyDown(0, 4) && g_cameraRange > 1.0f) {
        g_cameraRange -= 1.0f;
    }
    if (Input_KeyDown(0, 5)) {
        g_cameraRange += 1.0f;
    }
    CAM->pos.y = 0.0f;
    CAM->pos.x = g_cameraRange * (f32)sin(g_cameraAngle) + 50.0f;
    CAM->pos.z = g_cameraRange * (f32)cos(g_cameraAngle) + 120.0f;
    PSVECSubtract(&g_carCamPos, &CAM->pos, &CAM->dir);
    if (PSVECSquareMag(&CAM->dir)) {
        PSVECNormalize(&CAM->dir, &CAM->dir);
    }
    AccumulateVector(&CAM->dir, &g_jBasis, &CAM->up, -CAM->dir.y);
    if (PSVECSquareMag(&CAM->up)) {
        PSVECNormalize(&CAM->up, &CAM->up);
    }
    PSVECCrossProduct(&CAM->up, &CAM->dir, &CAM->right);
    g_yaw += 0.01f;
    if (g_yaw > 6.2831855f) {
        g_yaw -= 6.2831855f;
    }
    g_carDrivePos.y = -20.0f;
    g_carDrivePos.x = 150.0f * (f32)sin(g_yaw);
    g_carDrivePos.z = 150.0f * (f32)cos(g_yaw) + 150.0f;
    for (i = 0; i < 80; i++) {
        g_carRot[i] += 0.02f;
        if (g_carRot[i] > 6.2831855f) {
            g_carRot[i] -= 6.2831855f;
        }
    }
    Camera_PrepareForRender(PTR(g_pCamera));
    ShowStats_BeginScene();
    tri = Gfx_Begin_GroupTriFlat(g_dirt, 0, 1);
    ((s32 *)tri)[0] = ((s32 *)tri)[1] = ((s32 *)tri)[2] = ((s32 *)tri)[3] = 0x80;
    tri[5] = tri[9] = tri[13] = -21.75f;
    tri[4] = tri[8] = -150.0f;
    tri[12] = 230.0f;
    tri[6] = -60.0f;
    tri[10] = tri[14] = 300.0f;
    tri[7] = tri[11] = tri[17] = 0.0f;
    tri[18] = tri[16] = tri[19] = 4.0f;
    ((s32 *)tri)[20] = ((s32 *)tri)[21] = ((s32 *)tri)[22] = ((s32 *)tri)[23] = 0x80;
    tri[25] = tri[29] = tri[33] = -21.75f;
    tri[24] = tri[28] = 230.0f;
    tri[32] = -150.0f;
    tri[26] = 300.0f;
    tri[30] = tri[34] = -60.0f;
    tri[27] = tri[31] = tri[37] = 4.0f;
    tri[36] = tri[38] = tri[39] = 0.0f;
    Gfx_End_GroupTri(2);
    for (i = 0; i < 80; i++) {
        memcpy(&mtx, &g_identity, sizeof(Matrix4));
        SetOrientation(&mtx, g_carRot[i], 0.0f, 0.0f);
        mtx.v[3].x = g_carPos[i].x;
        mtx.v[3].y = -20.0f;
        mtx.v[3].z = g_carPos[i].z;
        Gfx_DrawGeometry(pCar, HND(&mtx));
    }
    memcpy(&mtx, &g_identity, sizeof(Matrix4));
    SetOrientation(&mtx, 1.5707964f + g_yaw, 0.0f, 0.0f);
    mtx.v[3].x = g_carDrivePos.x;
    mtx.v[3].y = g_carDrivePos.y;
    mtx.v[3].z = g_carDrivePos.z;
    Gfx_DrawGeometry(pCar, HND(&mtx));
    ProcessManager_Render(1);
    ShowStats_EndScene();
    Gfx_GetRenderStats(stats);
    return 1;
}



s32 StateRenderTest_Shutdown(void) {
    return 1;
}

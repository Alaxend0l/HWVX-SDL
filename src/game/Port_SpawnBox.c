/* The spawn envelope: every car's collision box at once (Port_SpawnBox.h). Port-only file. */
#include <SDL3/SDL.h>
#include "common.h"
#include "Port_Content.h"
#include "Port_SpawnBox.h"
#include "World.h"

extern StreamingPack *g_StreamingDataPacks[11];

static int s_done[2], s_cars[2];
static f32 s_min[2][3], s_max[2][3];

/* CarAInfo's collider (0xA0); the collider's type (0x10) and box (0x44: min, max corners) */
static int CarBox(DataPack *dp, f32 *lo, f32 *hi) {
    u8 *base = dp != NULL ? GETP(dp->base) : NULL, *tab, *info, *col;
    const f32 *box;

    if (base == NULL || dp->dataSize < 8 || *(u32 *)base == 0) {
        return 0;
    }
    tab = Port_HandleToPtr(*(u32 *)(base + 4));
    info = tab != NULL ? Port_HandleToPtr(*(u32 *)tab) : NULL;
    col = info != NULL ? Port_HandleToPtr(*(u32 *)(info + 0xA0)) : NULL;
    if (col == NULL || col[0x10] != 0) {
        return 0;   /* a hull (none in retail data) */
    }
    box = Port_HandleToPtr(*(u32 *)(col + 0x44));
    if (box == NULL) {
        return 0;
    }
    lo[0] = box[0], lo[1] = box[1], lo[2] = box[2];
    hi[0] = box[4], hi[1] = box[5], hi[2] = box[6];
    return lo[0] <= hi[0] && lo[1] <= hi[1] && lo[2] <= hi[2];
}

int Port_SpawnEnvelope(float min[3], float max[3], int *cars, int all) {
    static const f32 retailMin[3] = { -5.0f, -1.4f, -14.0f }, retailMax[3] = { 5.0f, 5.25f, 13.0f };
    static char name[] = "HW_Cars";
    u32 i, n;
    int k;

    all = all != 0;
    if (!s_done[all]) {
        u64 t0 = SDL_GetTicksNS();

        s_done[all] = 1;
        n = all ? Port_ContentCount(PORT_CONTENT_CAR) : Port_ContentBaseCount(PORT_CONTENT_CAR);
        for (i = 0; i < n; i++) {
            s32 slot = FileStreaming_LoadDataPack(name, 0, i, 0);
            /* the slot's pack as loaded, read only: FileStreaming_InitializeDataPack would mark
             * it refreshed without refreshing it, and its release would then free texture ids
             * the pack never had (live textures of other packs) */
            StreamingPack *sp = slot >= 0 ? &g_StreamingDataPacks[0][slot] : NULL;
            DataPack *dp = sp != NULL && sp->used && sp->loaded ? &sp->pack : NULL;
            f32 lo[3], hi[3];

            if (CarBox(dp, lo, hi)) {
                for (k = 0; k < 3; k++) {
                    s_min[all][k] = s_cars[all] == 0 || lo[k] < s_min[all][k] ? lo[k] : s_min[all][k];
                    s_max[all][k] = s_cars[all] == 0 || hi[k] > s_max[all][k] ? hi[k] : s_max[all][k];
                }
                s_cars[all]++;
            }
            if (slot >= 0) {
                FileStreaming_ReleaseDataPack((u32)slot, 0);
                FileStreaming_HeartBeat();
            }
        }
        Port_Log("spawn box: %d of %u %scars: x %.2f..%.2f, y %.2f..%.2f, z %.2f..%.2f (%.0f ms)", s_cars[all], (unsigned)n,
                 all ? "" : "base ", (double)s_min[all][0], (double)s_max[all][0], (double)s_min[all][1],
                 (double)s_max[all][1], (double)s_min[all][2], (double)s_max[all][2],
                 (double)(SDL_GetTicksNS() - t0) / 1e6);
    }
    for (k = 0; k < 3; k++) {
        min[k] = s_cars[all] ? s_min[all][k] : retailMin[k];
        max[k] = s_cars[all] ? s_max[all][k] : retailMax[k];
    }
    if (cars != NULL) {
        *cars = s_cars[all];
    }
    return s_cars[all] > 0;
}

int Port_SpawnEnvelopeKnown(float min[3], float max[3], int all) {
    static const f32 retailMin[3] = { -5.0f, -1.4f, -14.0f }, retailMax[3] = { 5.0f, 5.25f, 13.0f };
    int k;

    all = all != 0;
    for (k = 0; k < 3; k++) {
        min[k] = s_cars[all] ? s_min[all][k] : retailMin[k];
        max[k] = s_cars[all] ? s_max[all][k] : retailMax[k];
    }
    return s_done[all];
}

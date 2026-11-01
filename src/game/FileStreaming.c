/* World streaming: the quadrants near the cameras are loaded from the
 * world's .xgc on the loading thread, their objects spawned/released as they
 * become active/inactive, and their item spawn points respawn pickups. Also
 * the pool of streamed data packs (g_StreamingDataPacks). */
#include "common.h"
#include "Gfx.h"
#include "World.h"
#include "ThingManager.h"

extern StreamingPack *g_StreamingDataPacks[11];
extern u32 g_MaxStreamingDataPacks[11];
extern s32 g_numQuadPacksInMemory;
extern s32 g_numQuadPacksToLoad;
extern s32 g_itemTimer;
extern void *g_pDataPackToLoad;
extern s8 g_bLoadingThreadRunning;
extern s8 g_bLoadQuadrants;
extern s8 g_bLoadDataPack;
extern s32 g_numDataPacksToRelease;
extern s32 g_numQuadPacksToRelease;
extern void *g_pPacksToBeReleased[10];
extern void *g_pQuadPackToBeReleased[11];
extern s32 g_dataPackToLoadType;
extern WorldQuad *g_LoadedQuadrants[11];
extern void *g_pWorldQuadrant;
extern s32 g_quadFileID;
extern void (*DestroyMusic)(void);
void EndThread(void);

extern s32 g_loadingThreadID;
extern void *g_QuadrantsToLoad[11];
s32 RunThread(void *func, s32 priority);

extern s32 g_iDeltaTimeMS;
extern Vector4 g_zeroVector __attribute__((aligned(16)));



void FileStreaming_Initialize(void)
{
    u32 i;
    u32 size;

    g_numQuadPacksInMemory = 0;
    g_numQuadPacksToLoad = 0;
    g_itemTimer = 0;
    for (i = 0; i < 11; i++) {
        if (i != 7 && g_MaxStreamingDataPacks[i] != 0) {
            size = g_MaxStreamingDataPacks[i] * sizeof(StreamingPack);
            g_StreamingDataPacks[i] = (StreamingPack *) Allocate(size, 0, "");
            memset(g_StreamingDataPacks[i], 0, size);
        } else {
            g_StreamingDataPacks[i] = NULL;
        }
    }
    g_pDataPackToLoad = NULL;
}

void FileStreaming_Destroy(void)
{
    u32 i;

    for (i = 0; i < 11; i++) {
        FileStreaming_ReleaseAllDataPacks(i, 1);
        Deallocate(g_StreamingDataPacks[i]);
    }
}

u8 IsLoadingThreadRunning(void)
{
    return g_bLoadingThreadRunning;
}

s32 FileStreaming_GetNumAvailableQuadPacks(void)
{
    return 0xB;
}

void FileStreaming_ReleaseQuadPacks(void)
{
    u32 i;

    while (g_bLoadingThreadRunning)
    {
    }
    for (i = 0; i < (u32)g_numQuadPacksInMemory; i++)
    {
        if (g_LoadedQuadrants[i] != NULL && g_LoadedQuadrants[i]->loaded != 0)
        {
            DataPack_ReleaseQuadPack(g_LoadedQuadrants[i]);
        }
    }
    if (g_pWorldQuadrant != NULL && ((WorldQuad *)g_pWorldQuadrant)->loaded != 0)
    {
        DataPack_ReleaseQuadPack(g_pWorldQuadrant);
    }
    g_numQuadPacksInMemory = 0;
    g_numQuadPacksToLoad = 0;
    FileStreaming_HeartBeat();
    FileStreaming_HeartBeat();
    FileStreaming_SetActiveQuadrants(0, 0);
    g_itemTimer = 0;
    if (g_quadFileID != -1)
    {
        File_Close(g_quadFileID);
        g_quadFileID = -1;
    }
}

/* Find the world quadrant (the one without a visibility BSP) and turn the
 * other quadrants' neighbour ids into indices. */
void FileStreaming_InitializeAllQuads(void)
{
    World *world = GETP(g_worldData.world);
    u32 i;
    u32 j;
    u32 k;
    WorldQuad *quad;

    if (world == NULL)
    {
        return;
    }
    for (i = 0; i < world->numQuads; i++)
    {
        quad = &GETP(world->quads)[i];
        if (quad->bsp.h == 0)
        {
            g_pWorldQuadrant = quad;
        }
        else
        {
            for (j = 0; j < quad->numNeighbors; j++)
            {
                for (k = 0; k < world->numQuads; k++)
                {
                    if (k != i && GETP(world->quads)[k].id == GETP(quad->neighbors)[j])
                    {
                        GETP(quad->neighbors)[j] = k;
                        break;
                    }
                }
            }
        }
    }
}

void FileStreaming_ResetQuadResources(void)
{
    u32 i;
    u32 j;
    s8 paused;
    QuadInstance *st;
    QuadFxPoint *dyn;
    QuadItem *item;
    WorldQuad *quad;

    paused = Trigger_IsTACSystemPaused();
    Trigger_PauseTACSystem(1);
    g_LoadedQuadrants[g_numQuadPacksInMemory] = g_pWorldQuadrant;
    for (i = 0; i < g_numQuadPacksInMemory + 1; i++) {
        if (g_LoadedQuadrants[i] != NULL && GETP(g_LoadedQuadrants[i]->contents)->dirty) {
            GETP(g_LoadedQuadrants[i]->contents)->dirty = 0;
            for (j = 0; j < GETP(g_LoadedQuadrants[i]->contents)->numInstances; j++) {
                st = &GETP(GETP(g_LoadedQuadrants[i]->contents)->instances)[j];
                if (GETP(st->thing) != NULL && !THINGRECORD_IS_STATIC(GETP(st->record))) {
                    THING_VFN(GETP(st->thing), THING_V_RELEASE, void (*)(void *))(GETP(st->thing));
                    SETP(st->thing, NULL);
                    st->mode = 1;
                }
            }
            for (j = 0; j < GETP(g_LoadedQuadrants[i]->contents)->numFxPoints; j++) {
                dyn = &GETP(GETP(g_LoadedQuadrants[i]->contents)->fxPoints)[j];
                if (GETP(dyn->thing) != NULL) {
                    THING_VFN(GETP(dyn->thing), THING_V_RELEASE, void (*)(void *))(GETP(dyn->thing));
                    SETP(dyn->thing, NULL);
                    dyn->mode = 1;
                }
            }
            for (j = 0; j < GETP(g_LoadedQuadrants[i]->contents)->numItems; j++) {
                item = &GETP(GETP(g_LoadedQuadrants[i]->contents)->items)[j];
                if (GETP(item->thing) != NULL) {
                    THING_VFN(GETP(item->thing), THING_V_RELEASE, void (*)(void *))(GETP(item->thing));
                    SETP(item->thing, NULL);
                    item->mode = QUAD_ITEM_ON;
                }
            }
        }
    }
    for (i = 0; i < ((World *)GETP(g_worldData.world))->numQuads; i++) {
        quad = &GETP(((World *)GETP(g_worldData.world))->quads)[i];
        for (j = 0; j < GETP(quad->contents)->numFxPoints; j++) {
            GETP(GETP(quad->contents)->fxPoints)[j].mode = GETP(GETP(quad->contents)->fxPoints)[j].defMode;
        }
        for (j = 0; j < GETP(quad->contents)->numItems; j++) {
            GETP(GETP(quad->contents)->items)[j].mode = GETP(GETP(quad->contents)->items)[j].defMode;
            GETP(GETP(quad->contents)->items)[j].spawned = 0;
        }
        for (j = 0; j < GETP(quad->contents)->numInstances; j++) {
            GETP(GETP(quad->contents)->instances)[j].mode = GETP(GETP(quad->contents)->instances)[j].defMode;
        }
    }
    FileStreaming_SetActiveQuadrants((void **)g_LoadedQuadrants, g_numQuadPacksInMemory + 1);
    Trigger_PauseTACSystem(paused);
}


/* ActionDestructibleChangeMode / ActionFXPointChangeMode data for one entry. */
typedef struct QuadModeAction {
    u32 mode;
    s32 count;                  /* always 1 */
    PTR32(void) entry;
} QuadModeAction;

/* Make `quads` the active quadrants: release the things of the loaded
 * quadrants no longer in it (queueing their packs for release), then spawn
 * the objects of newly active ones. Static scenery isn't spawned; its shared
 * slaves get a holder and it gets a culling radius instead. */
void FileStreaming_SetActiveQuadrants(void **quads, u32 count) {
    WorldQuad **active = (WorldQuad **)quads;
    DestructibleParams sp __attribute__((aligned(16)));
    WorldQuad *release[12];
    EmitterParams dp __attribute__((aligned(16)));
    QuadModeAction destAction;
    QuadModeAction fxAction;
    u32 numRelease;
    u32 i;
    u32 k;
    u32 j;
    WorldQuad *quad;
    s8 paused;

    numRelease = 0;
    paused = Trigger_IsTACSystemPaused();
    for (i = 0; i < g_numQuadPacksInMemory; i++) {
        quad = g_LoadedQuadrants[i];
        for (j = 0; j < count; j++) {
            if (quad == active[j]) {
                break;
            }
        }
        if (j == count && quad->bsp.h != 0) {
            release[numRelease++] = quad;
        }
    }
    Trigger_PauseTACSystem(1);
    for (i = 0; i < numRelease; i++) {
        quad = release[i];
        if (GETP(quad->contents)->dirty) {
            QuadContents *c = GETP(release[i]->contents);

            c->dirty = 0;
            for (j = 0; j < c->numInstances; j++) {
                QuadInstance *st = &GETP(c->instances)[j];

                if (GETP(st->thing) != NULL && !THINGRECORD_IS_STATIC(GETP(st->record))) {
                    THING_VFN(GETP(st->thing), THING_V_RELEASE, void (*)(void *))(GETP(st->thing));
                    SETP(st->thing, NULL);
                }
            }
            for (j = 0; j < c->numFxPoints; j++) {
                QuadFxPoint *dyn = &GETP(c->fxPoints)[j];

                if (GETP(dyn->thing) != NULL) {
                    THING_VFN(GETP(dyn->thing), THING_V_RELEASE, void (*)(void *))(GETP(dyn->thing));
                    SETP(dyn->thing, NULL);
                }
            }
            for (j = 0; j < c->numItems; j++) {
                QuadItem *item = &GETP(c->items)[j];

                if (GETP(item->thing) != NULL && GETP(item->record)->type != ITEM_TYPE_CARRIED) {
                    if (item->timer == 0) {
                        item->spawned = 0;
                    }
                    THING_VFN(GETP(item->thing), THING_V_RELEASE, void (*)(void *))(GETP(item->thing));
                    SETP(item->thing, NULL);
                }
            }
            if (release[i]->loaded) {
                g_pQuadPackToBeReleased[g_numQuadPacksToRelease++] = release[i];
            }
        }
    }
    ThingManagerList_RemoveReleasedThings(&g_thingManager_DARYL.activeList);
    Trigger_PauseTACSystem(paused);
    g_numQuadPacksInMemory = 0;
    for (i = 0; i < count; i++) {
        QuadContents *c = GETP(active[i]->contents);

        if (active[i]->bsp.h != 0) {
            g_LoadedQuadrants[g_numQuadPacksInMemory++] = active[i];
        }
        if (!c->dirty) {
            c->dirty = 1;
            for (j = 0; j < c->numInstances; j++) {
                QuadInstance *st = &GETP(c->instances)[j];
                ThingRecord *record = GETP(st->record);

                if (st->mode == QUAD_OBJECT_DESTROYED && st->respawn) {
                    st->mode = 1;
                } else if (st->mode == QUAD_OBJECT_DESTROYED) {
                    continue;
                } else if (THINGRECORD_IS_STATIC(record)) {
                    SlaveList *slaves = (SlaveList *)record->slaves;

                    for (k = 0; k < slaves->count; k++) {
                        SharedSlaves *shared = GETP(GETP(slaves->triggers)[k].data);

                        if (shared->active == 0) {
                            shared->active = 1;
                            GETP(shared->list.triggers)[0].holder = SharedSlaveHolder_Get(shared);
                        }
                    }
                    if (record->noDraw == 0) {
                        st->radius = GetBoundingBoxRadius((Vector4 *)((Geometry *)GETP(record->geometry))->bboxMin, &g_zeroVector);
                    } else {
                        st->radius = 1.0f;   /* port: the original loads 1.0f here (@661); the decomp had 1000000.0f */
                    }
                    continue;
                }
                sp.unk4C = 0;
                sp.moveType = 0;
                /* the original passes 0.0f (@662), i.e. "no per-instance draw distance":
                 * Destructible_Init reads this word as drawDist and then falls back to
                 * the record's. The decomp had 1.0f, which culled them beyond 1 unit. */
                sp.drawDist = st->hasDrawDist ? st->drawDist : 0.0f;
                sp.pos = st->pos;
                SETP(sp.record, record);
                sp.quat = st->quat;
                sp.angVel = g_zeroVector;
                sp.vel = g_zeroVector;
                SETP(st->thing, ThingManager_DARYL_CreateThing(THING_TYPE_DESTRUCTIBLE, &sp));
                if (GETP(st->thing) != NULL) {
                    SETP(GETP(st->thing)->trigger, st);  /* the thing's spawn entry */
                    destAction.mode = st->mode;
                    destAction.count = 1;
                    SETP(destAction.entry, st);
                    ActionDestructibleChangeMode((u32 *)&destAction);
                }
            }
            for (j = 0; j < c->numFxPoints; j++) {
                QuadFxPoint *dyn = &GETP(c->fxPoints)[j];

                if (GETP(dyn->emitter) != NULL) {
                    dp.pos = dyn->pos;
                    SETP(dp.emitter, GETP(dyn->emitter));
                    dp.quat = dyn->quat;
                    dp.drawDist = dyn->hasDrawDist ? dyn->drawDist : 0.0f;   /* port: 0.0f in the original (@662) */
                    dp.owner = 0;
                    SETP(dyn->thing, ThingManager_DARYL_CreateThing(THING_TYPE_EMITTER, &dp));
                }
                if (GETP(dyn->corona) != NULL) {
                    CoronaHolder_Add(GETP(dyn->corona), &dyn->pos);
                }
                fxAction.mode = dyn->mode;
                fxAction.count = 1;
                SETP(fxAction.entry, dyn);
                ActionFXPointChangeMode((s32 *)&fxAction);
            }
        }
    }
}


/* Every 1.5 s, count down the item spawn points of the loaded quadrants and
 * (re)spawn their pickups, with the item's spawn effect. */
void FileStreaming_HeartBeatItems(void)
{
    DebrisParams p __attribute__((aligned(16)));
    ItemParams params __attribute__((aligned(16)));
    u32 i;
    u32 j;
    QuadItem *item;
    ItemThing *thing;

    g_LoadedQuadrants[g_numQuadPacksInMemory] = g_pWorldQuadrant;
    g_itemTimer += g_iDeltaTimeMS;
    if (g_itemTimer > 1500) {
        g_itemTimer -= 1500;
        for (i = 0; i < g_numQuadPacksInMemory + 1; i++) {
            QuadContents *c = GETP(g_LoadedQuadrants[i]->contents);

            for (j = 0; j < c->numItems; j++) {
                item = &GETP(c->items)[j];
                if ((item->timer != 0 || !item->spawned) && GETP(item->record) != NULL && GETP(item->thing) == NULL && item->mode == QUAD_ITEM_ON) {
                    item->timer -= 1500;
                    if (item->timer <= 0) {
                        item->timer = 0;
                        params.pos = item->pos;
                        params.pos.y += GETP(item->record)->height;
                        params.vel.x = 0.0f;
                        params.vel.y = 0.0f;
                        params.vel.z = 0.0f;
                        SETP(params.record, GETP(item->record));
                        params.quat.w = 1.0f;
                        params.quat.x = 0.0f;
                        params.quat.y = 0.0f;
                        params.quat.z = 0.0f;
                        /* port: the original always stores it, 0.0f ("the default")
                         * unless the point has its own; the decomp left it unset,
                         * so items got stack garbage and could be culled at once */
                        params.drawDist = item->hasDrawDist ? item->drawDist : 0.0f;
                        params.unk38 = item->unk35;
                        thing = (ItemThing *)ThingManager_DARYL_CreateThing(THING_TYPE_ITEM, &params);
                        if (thing) {
                            item->spawned = 1;
                            if (GameSettings_GetCodeVariable(0x12D, 0) && !GameSettings_GetCodeVariable(0x136, 0)) {
                                item->respawn = 0;
                            }
                            SETP(item->thing, thing);
                            SETP(thing->spawner, item);
                            SETP(thing->base.trigger, item);
                            thing->base.flags |= 1;
                            if (item->respawn) {
                                SETP(thing->spawner, item);
                            }
                            if (GETP(item->record)->spawnDebris) {
                                p.debris = GETP(item->record)->spawnDebris;
                                p.pos = item->pos;
                                p.quat = params.quat;
                                p.vel.z = 0.0f;
                                p.vel.y = 0.0f;
                                p.vel.x = 0.0f;
                                p.owner = 0;
                                p.unk48 = 0.0f;
                                p.unk4C = 0.0f;
                                p.angVel = g_zeroVector;
                                ThingManager_DARYL_CreateThing(THING_TYPE_EVENT_SEQ, &p);
                            }
                        }
                    }
                }
            }
        }
    }
}
void FileStreaming_LoadQuadPacks(void **quads, u32 count, s8 threaded)
{
    u32 i;
    WorldQuad *quad;

    if (g_bLoadingThreadRunning || (u32)g_numQuadPacksToRelease != 0)
    {
        return;
    }
    for (i = 0; i < count; i++)
    {
        quad = quads[i];
        if (GETP(quad->geoms) != NULL && quad->loaded == 0)
        {
            /* in memory: only the pointers need fixing up */
            DataPack_RefreshQuadPackPtrs(PTR(g_worldData.quadPack), quads[i]);
            quad->loaded = 1;
        }
        else if (quad->numGeoms == 0)
        {
            quad->loaded = 1;
        }
        else if (quad->loaded == 0)
        {
            g_QuadrantsToLoad[g_numQuadPacksToLoad++] = quad;
        }
    }
    if ((u32)g_numQuadPacksToLoad != 0)
    {
        if (threaded)
        {
            g_bLoadQuadrants = 1;
            g_bLoadingThreadRunning = 1;
            g_loadingThreadID = RunThread(FileStreaming_LoadingThread, 0x63);
        }
        else
        {
            LoadQuadrants(1);
        }
    }
}

void FileStreaming_LoadingThread(void)
{
    g_bLoadingThreadRunning = 1;
    if (g_bLoadQuadrants)
    {
        LoadQuadrants(0);
    }
    if (g_bLoadDataPack)
    {
        LoadDataPack();
    }
    g_bLoadQuadrants = 0;
    g_bLoadDataPack = 0;
    g_bLoadingThreadRunning = 0;
    EndThread();
}

void LoadQuadrants(s8 refresh) {
    typedef struct TocEntry {
        u32 version;
        u32 type;
        u32 world;
        u32 id;
        u32 size;
        u32 offset;
    } TocEntry;
    static TocEntry tocHeader[50];
    char path[0x80];
    char name[0x80];
    char msg[0x100];
    u32 i;
    u32 j;
    s8 error;
    u32 count;
    WorldQuad *quad;
    u32 alt;

    strcpy(name, GETP(((World *)GETP(g_worldData.world))->name));
    strcpy(path, GETP(((World *)GETP(g_worldData.world))->name));
    strcat(path, ".xgc");
    if (g_quadFileID == -1) {
        error = 0;
        g_quadFileID = File_Open(0, path, 0, NULL);
        if (g_quadFileID != -1) {
            File_Read(g_quadFileID, 0x10, tocHeader);
            Port_SwapArray32(tocHeader, 4);  /* big-endian file */
            if (tocHeader[0].version != 0x020100A0 || tocHeader[0].type != 0x040100AE) {
                error = 1;
            } else if (tocHeader[0].world != 0xB7) {
                Dprintf("\n*The world format is different %s. Packfile:%d,  Code:%d\n", GETP(((World *)GETP(g_worldData.world))->collisionPackName), tocHeader[0].world, 0xB7);
                error = 1;
            } else {
                count = tocHeader[0].id;
                File_Read(g_quadFileID, count * sizeof(TocEntry), tocHeader);
                Port_SwapArray32(tocHeader, count * sizeof(TocEntry) / 4);
                for (i = 0; i < ((World *)GETP(g_worldData.world))->numQuads; i++) {
                    quad = &GETP(((World *)GETP(g_worldData.world))->quads)[i];
                    alt = 0;
                    for (j = 0; j < count; j++) {
                        u32 id = tocHeader[j].id;
                        u32 type = tocHeader[j].type;
                        u32 index = id >> 16;

                        if (type == 7 && ((World *)GETP(g_worldData.world))->id == tocHeader[j].world &&
                            quad->packId == id && index == quad->numGeoms) {
                            if (tocHeader[j].version != 0xB7) {
                                Dprintf("\n*The world format is different %s. Packfile:%d,  Code:%d\n", GETP(((World *)GETP(g_worldData.world))->name), tocHeader[j].version, 0xB7);
                            } else {
                                quad->size = tocHeader[j].size;
                                quad->fileOffset = (count * sizeof(TocEntry) + 0x10) + tocHeader[j].offset;
                            }
                            break;
                        }
                        if (type == 7 && ((World *)GETP(g_worldData.world))->id == tocHeader[j].world &&
                            (u16)quad->packId == (u16)id) {
                            alt = index;
                        }
                    }
                    if (j == count) {
                        if (alt == 0) {
                            sprintf(msg, "The quadrant pack could not be found, Pack ID = %d Num GX = %d\nYou need to repack the world using the latest BrainCore\n", (u16)quad->packId, quad->numGeoms);
                        } else {
                            sprintf(msg, "The quadrant pack could not be found, Pack ID = %d Num GX = %d\nThe quadrant pack contains a pack with the same ID however the Num GX = %d\nYou need to repack the world using the latest BrainCore\n", (u16)quad->packId, quad->numGeoms, alt);
                        }
                        Dprintf(msg);
                    }
                }
            }
        }
        if (error || g_quadFileID == -1) {
            g_numQuadPacksToLoad = 0;
            return;
        }
    }
    for (i = 0; i < g_numQuadPacksToLoad; i++) {
        if (GETP(((WorldQuad *)g_QuadrantsToLoad[i])->geoms) == NULL) {
            DataPack_LoadQuadPack(g_quadFileID, g_QuadrantsToLoad[i]);
            if (refresh) {
                DataPack_RefreshQuadPackPtrs(PTR(g_worldData.quadPack), g_QuadrantsToLoad[i]);
                ((WorldQuad *)g_QuadrantsToLoad[i])->loaded = 1;
            }
        }
    }
    g_numQuadPacksToLoad = 0;
}

/* Find or claim a pool slot for the pack `name`/`id` of `type` and load it
 * (on the loading thread if `threaded`). Returns the slot, -1 if none. */
s32 FileStreaming_LoadDataPack(void *name, s32 type, u32 id, s8 threaded) {
    StreamingPack *packs;
    u32 i;
    u32 slot;

    slot = -1;
    if (type >= 11 || (g_bLoadingThreadRunning && threaded)) {
        return -1;
    }
    if (g_StreamingDataPacks[type] == NULL) {
        FileStreaming_Initialize();
    }
    packs = g_StreamingDataPacks[type];
    for (i = 0; i < g_MaxStreamingDataPacks[type]; i++) {
        if (packs[i].used && id == packs[i].id && strcmp(name, GETP(packs[i].name)) == 0 && packs[i].refs != 0) {
            /* sic: tests pack.soundIDs for "loaded" */
            if (GETP(packs[i].pack.soundIDs) == NULL) {
                packs[i].used = 0;
                packs[i].refs = 0;
                packs[i].loaded = 0;
                packs[i].refreshed = 0;
            } else {
                packs[i].refs++;
                return i;
            }
        }
        if (slot == -1 && !packs[i].used) {
            slot = i;
        }
    }
    if (slot == -1) {
        return -1;
    }
    packs[slot].used = 1;
    packs[slot].loaded = 0;
    packs[slot].refreshed = 0;
    SETP(packs[slot].name, name);
    packs[slot].id = id;
    packs[slot].refs = 1;
    packs[slot].unk10 = 0;
    g_dataPackToLoadType = type;
    g_pDataPackToLoad = &packs[slot];
    if (threaded) {
        if (!g_bLoadingThreadRunning) {
            g_bLoadDataPack = 1;
            g_bLoadingThreadRunning = 1;
            g_loadingThreadID = RunThread(FileStreaming_LoadingThread, 0x63);
        }
    } else {
        LoadDataPack();
    }
    return slot;
}

s32 FileStreaming_GetPackID(char *name, s32 type, u32 id)
{
    StreamingPack *pack;
    u32 i;

    if (type >= 11)
    {
        return -1;
    }
    for (i = 0; i < g_MaxStreamingDataPacks[type]; i++)
    {
        pack = &g_StreamingDataPacks[type][i];
        if (pack->used != 0 && id == pack->id)
        {
            if (name == GETP(pack->name) || strcmp(name, GETP(pack->name)) == 0)
            {
                return i;
            }
        }
    }
    return -1;
}


s32 FileStreaming_ReleaseAllDataPacks(s32 type, s8 wait)
{
    StreamingPack *packs;
    u32 i;

    if (type >= 11)
    {
        return 0;
    }
    for (i = 0; i < g_MaxStreamingDataPacks[type]; i++)
    {
        packs = g_StreamingDataPacks[type];
        if (packs != NULL && packs[i].loaded != 0)
        {
            packs[i].refs = 1;
            FileStreaming_ReleaseDataPack(i, type);
        }
    }
    if (wait)
    {
        FileStreaming_HeartBeat();
        FileStreaming_HeartBeat();
    }
    return 1;
}

/* Drop a reference; the last one invalidates the things using the pack's
 * data and queues it for release (FileStreaming_HeartBeat). */
s32 FileStreaming_ReleaseDataPack(u32 index, s32 type)
{
    StreamingPack *pack;

    if (type >= 11 || index >= g_MaxStreamingDataPacks[type])
    {
        return 0;
    }
    pack = &g_StreamingDataPacks[type][index];
    if (pack->used == 0)
    {
        return 1;
    }
    if (pack->loaded == 0)
    {
        pack->used = 0;
        return 1;
    }
    if (--pack->refs != 0)
    {
        return 1;
    }
    if (pack->refreshed == 0)
    {
        pack->pack.numTextures = 0;
    }
    ThingManager_Invalidate(pack->pack.orderedList.h, pack->pack.orderedList.h + pack->pack.dataSize);
    if (pack->pack.songLoaded != 0)
    {
        DestroyMusic();
    }
    g_pPacksToBeReleased[g_numDataPacksToRelease++] = pack;
    return 1;
}


void FileStreaming_HeartBeat(void)
{
    u32 i;

    for (i = 0; i < g_numDataPacksToRelease; i++)
    {
        DataPack_ReleaseDataPack(&((StreamingPack *)g_pPacksToBeReleased[i])->pack);
        ((StreamingPack *)g_pPacksToBeReleased[i])->used = 0;
    }
    g_numDataPacksToRelease = 0;
    for (i = 0; i < g_numQuadPacksToRelease; i++)
    {
        DataPack_ReleaseQuadPack(g_pQuadPackToBeReleased[i]);
    }
    g_numQuadPacksToRelease = 0;
}


/* The loaded pack in a slot, its pointers fixed up (once) if `refresh`. */
void *FileStreaming_InitializeDataPack(u32 index, s32 type, s8 refresh)
{
    StreamingPack *pack;

    if (type >= 11 || index >= g_MaxStreamingDataPacks[type])
    {
        return NULL;
    }
    pack = &g_StreamingDataPacks[type][index];
    if (pack->used == 0 || pack->loaded == 0)
    {
        return NULL;
    }
    if (pack->refreshed == 0 && refresh)
    {
        DataPack_RefreshPtrs(&pack->pack);
    }
    pack->refreshed = 1;
    return &pack->pack;
}




/* Load the pack FileStreaming_LoadDataPack queued (loading thread). */
void LoadDataPack(void)
{
    StreamingPack *pack;

    pack = g_pDataPackToLoad;
    if (pack != NULL)
    {
        g_pDataPackToLoad = NULL;
        pack->loaded = 1;
        DataPack_LoadFile(GETP(pack->name), HND(&pack->pack), g_dataPackToLoadType, pack->id);
    }
}

/* port: an optional longer draw distance (graphics.cfg "drawdist=normal|far|all",
 * HWVX_DRAWDIST; the launcher's Settings page, src/port/gfx_settings.c).
 *
 * The game streams a world's render geometry by quadrant: each camera's
 * quadrant and its neighbours are "visible" (World_Render), and
 * FileStreaming_SetActiveQuadrants makes them the active set, which is what
 * spawns their destructibles, emitters, coronas and pickups and lets their
 * items respawn. All of that is game logic, so this file never touches it:
 * no `visible` flag, no g_LoadedQuadrants, no loading-thread state, no load
 * or release of a game quadrant. It keeps an extra set of its own instead:
 *
 *  - Far: the quadrants two neighbour steps from each camera's; All: every
 *    quadrant. Those the game isn't drawing this frame are drawn here, before
 *    the game's own (World_Render calls Port_DrawDistRender).
 *  - A quadrant the game has loaded anyway (a world of at most 11 quadrants
 *    loads them all at the start) is drawn from the game's copy. Otherwise
 *    this file loads a copy of its own (a "shadow" WorldQuad sharing the
 *    game's contents) into its own heap past the game heap (os.c
 *    Port_ExtraHeap), on the main thread while the game's loader is idle,
 *    a couple a frame. Shadows go when the setting goes back to Normal or
 *    the world is unloaded (FileStreaming_ReleaseQuadPacks).
 *  - Drawn: the geometry, the static instances (as RenderQuadrant, without
 *    their shared slaves: slave things are the game's), and, for quadrants
 *    the game has never made active, the destructibles in their placed pose
 *    if shown (mode 1): a stand-in, the real ones spawn when the game
 *    activates the quadrant. Emitters, coronas and pickups of extra
 *    quadrants are not drawn: they are things, and things are logic.
 *  - A shadow's shaders are not put in the game's ShaderHolder (128 entries,
 *    no bounds check): this file applies the same animation to them itself.
 *    Nor are its vertex colour lists added to VertexColorSeqManager (64, the
 *    same).
 *  - The far clipping plane (Gfx_GeometryHandler.c) and fog (Render_.c) are
 *    pushed out with it (Port_DrawDistFarPlane, Port_DrawDistFog), and the
 *    projection's far plane (Render_Matrix_.c) for All.
 *
 * Port_DrawDistLevel is Normal in the attract demo, input playback and online
 * play, so those are the same for everyone.
 *
 * The viewer's 3D view (the data browser; HWVX_VIEWER only, never the game's
 * play or a test drive) has three switches of its own (Port_DrawDistViewer):
 * all quadrants loaded (All's set, whatever the setting), geometry clipping
 * off (no far plane, no draw distances or frustum culls, the last LOD past its
 * range, the projection's far plane out) and fog off (the sky keeps its own,
 * Port_RenderGroupFog). Defaults: as without them. */
#include <SDL3/SDL.h>
#include "common.h"
#include "World.h"
#include "Shader.h"
#include "Gfx.h"
#include "Port_Swap.h"
#include "Port_Online.h"

extern s32 g_quadFileID;
extern s8 g_bLoadingThreadRunning;
extern ShaderHolder *g_pShaderHolder;
extern volatile u32 g_portDiscReads;
extern Vector4 g_zeroVector __attribute__((aligned(16)));

/* How many shadows may load in one frame, and the time after which no more
 * start (a GameCube quadrant takes 0.2 - 2 ms). */
#define LOADS_PER_FRAME 2
#define LOAD_BUDGET_NS 4000000ull
/* A shadow no longer wanted stays this many frames, so driving back and
 * forth over a quadrant border doesn't load and drop the same ones. */
#define KEEP_FRAMES 300

typedef struct ShadowGX {
    ShadedGeom *gx;
    Shader *shader;
} ShadowGX;

typedef struct Extra {
    WorldQuad *shadow;     /* our copy, loaded; NULL: none */
    ShadowGX *gx;          /* its shaded geometry (ShaderHolder_AddGX's list) */
    u32 numGX;
    u32 unwanted;          /* frames since it was last wanted */
    u8 failed;             /* the copy couldn't be made: don't try again */
    u8 radiusDone;         /* the static instances' culling radius set */
} Extra;

static World *s_world;
static Extra *s_extra;
static u32 s_numExtra;
static u8 *s_wanted;
static s32 s_level;        /* this frame's Port_DrawDistLevel */
static u32 s_numShadows;
static u32 s_shadowBytes;

int Port_ViewerOn(void);   /* viewer.c */

/* The viewer's switches (Port_DrawDistViewer). */
static s32 s_vClip = 1, s_vAll = 0, s_vFog = 1;

void Port_DrawDistViewer(s32 clip, s32 allQuads, s32 fog) {
    s_vClip = clip != 0;
    s_vAll = allQuads != 0;
    s_vFog = fog != 0;
}

void Port_DrawDistViewerGet(s32 *clip, s32 *allQuads, s32 *fog) {
    *clip = s_vClip;
    *allQuads = s_vAll;
    *fog = s_vFog;
}

/* The viewer's geometry clipping is off: nothing is culled for its distance
 * or for being outside the view (World.c, Gfx_GeometryHandler.c). */
s32 Port_DrawDistNoCull(void) {
    return !s_vClip && Port_ViewerOn();
}

/* The draw distance this frame: the setting, except Normal in the attract
 * demo (code variable 0x198), input playback and online play. */
s32 Port_DrawDistLevel(void) {
    extern s32 g_playbackStatus;
    s32 level = Port_DrawDistSetting();

    if (Port_ViewerOn() && s_vAll) {
        return PORT_DRAWDIST_ALL;   /* the viewer's "load all quadrants" */
    }
    if (level == PORT_DRAWDIST_NORMAL || GameSettings_GetCodeVariable(0x198, 0) != 0 || g_playbackStatus == 2 ||
        Port_OnlineActive()) {
        return PORT_DRAWDIST_NORMAL;
    }
    return level;
}

/* Whether this frame draws further than the game (Render_.c's group fog). */
s32 Port_DrawDistActive(void) {
    return s_level != PORT_DRAWDIST_NORMAL || (Port_ViewerOn() && (!s_vFog || !s_vClip));
}

/* The far clipping plane to cull geometry at (Gfx_GeometryHandler.c): the
 * world's, doubled for Far, none for All (0: no plane). */
f32 Port_DrawDistFarPlane(f32 farPlane) {
    if (Port_ViewerOn()) {
        /* the viewer: its clipping switch, whatever quadrants are loaded */
        return !s_vClip ? 0.0f : s_level == PORT_DRAWDIST_FAR ? farPlane * 2.0f : farPlane;
    }
    switch (s_level) {
    case PORT_DRAWDIST_FAR:
        return farPlane * 2.0f;
    case PORT_DRAWDIST_ALL:
        return 0.0f;
    }
    return farPlane;
}

/* The fog's range (Render_.c), pushed out as far as the far plane is for Far.
 * All: no distance fog (0 returned: draw without fog). */
s32 Port_DrawDistFog(f32 *start, f32 *end) {
    if (Port_ViewerOn()) {
        if (!s_vFog) {
            return 0;
        }
        if (!s_vClip || s_level == PORT_DRAWDIST_FAR) {
            *start *= s_vClip ? 2.0f : 4.0f;   /* out with the geometry it no longer clips */
            *end *= s_vClip ? 2.0f : 4.0f;
        }
        return 1;
    }
    switch (s_level) {
    case PORT_DRAWDIST_FAR:
        *start *= 2.0f;
        *end *= 2.0f;
        break;
    case PORT_DRAWDIST_ALL:
        return 0;
    }
    return 1;
}

/* The projection's far plane (Render_Matrix_.c): the game's 20000 units, or
 * enough for any world with All. */
f32 Port_DrawDistProjectionFar(f32 farPlane) {
    if (Port_ViewerOn()) {
        return !s_vClip ? farPlane * 4.0f : farPlane;
    }
    return s_level == PORT_DRAWDIST_ALL ? farPlane * 4.0f : farPlane;
}

static void ReleaseShadow(Extra *e) {
    s32 old;

    if (e->shadow == NULL) {
        return;
    }
    s_shadowBytes -= e->shadow->size;
    s_numShadows--;
    old = OSSetCurrentHeap(Port_ExtraHeap());
    /* its geometry was never in ShaderHolder or VertexColorSeqManager, so
     * their removals find nothing */
    DataPack_ReleaseQuadPack((QuadPack *)e->shadow);
    Deallocate(e->shadow);
    OSSetCurrentHeap(old);
    SDL_free(e->gx);
    e->shadow = NULL;
    e->gx = NULL;
    e->numGX = 0;
}

/* Every shadow goes (the world unloads, or the setting is Normal again). */
void Port_DrawDistReleaseAll(void) {
    u32 i;

    for (i = 0; i < s_numExtra; i++) {
        ReleaseShadow(&s_extra[i]);
    }
    SDL_free(s_extra);
    SDL_free(s_wanted);
    s_extra = NULL;
    s_wanted = NULL;
    s_numExtra = 0;
    s_world = NULL;
    s_level = PORT_DRAWDIST_NORMAL;
}

/* DataPack_RefreshQuadPackPtrs for a shadow: its texture ids from the world's
 * texture pack, its shaders noted here instead of in ShaderHolder, no vertex
 * colours. 0 if it references another pack (no retail quadrant does). */
static s32 RefreshShadow(Extra *e) {
    DataPack *pack = PTR(g_worldData.quadPack);
    QuadPackHeader *header = GETP(((QuadPack *)e->shadow)->header);
    u8 *base = (u8 *)PTR(((QuadPack *)e->shadow)->gxBase);
    s32 *reloc;
    s32 *shader;
    u32 i;
    u32 index;

    if (pack == NULL || header == NULL) {
        return 0;
    }
    reloc = GETP(header->relocs);
    for (i = 0; i < header->numRelocs; i++, reloc += 2) {
        if (reloc[1] & 0xFFFF0000) {
            return 0;
        }
    }
    reloc = GETP(header->relocs);
    for (i = 0; i < header->numRelocs; i++, reloc += 2) {
        PortSwap_StoreTextureID(base, reloc[0], GETP(pack->textureIDs)[reloc[1]]);
    }
    e->gx = header->numShaders != 0 ? SDL_calloc(header->numShaders, sizeof(ShadowGX)) : NULL;
    shader = GETP(header->shaderList);
    for (i = 0; i < header->numShaders; i++, shader += 2) {
        index = GETP(pack->shaderIDs)[shader[1]];
        if (e->gx != NULL && index < g_pShaderHolder->numShaders) {
            e->gx[e->numGX].gx = (ShadedGeom *)(base + shader[0]);
            e->gx[e->numGX].shader = GETP(g_pShaderHolder->shaders[index]);
            e->numGX++;
        }
    }
    return 1;
}

static int s_copying, s_needsRef;

int Port_DrawDistSkipRef(void) {
    s_needsRef |= s_copying;
    return s_copying;
}

/* Loads our own copy of `quad`'s geometry. */
static void LoadShadow(Extra *e, WorldQuad *quad, u32 index) {
    WorldQuad *sh;
    s32 old;
    u32 reads = g_portDiscReads;
    u64 t0 = SDL_GetTicksNS();
    u32 inUse;
    u32 size;

    old = OSSetCurrentHeap(Port_ExtraHeap());
    sh = Allocate(sizeof(WorldQuad), 0, "");
    if (sh != NULL) {
        *sh = *quad;
        /* only what says where its pack is: none of the game's copy */
        SETP(sh->geoms, NULL);
        SETP(((QuadPack *)sh)->vertices, NULL);
        SETP(((QuadPack *)sh)->header, NULL);
        ((QuadPack *)sh)->gxBase = 0;
        SETP(((QuadPack *)sh)->alloc, NULL);
        sh->loaded = 0;
        sh->visible = 0;
        s_copying = 1;
        s_needsRef = 0;
        DataPack_LoadQuadPack(g_quadFileID, (QuadPack *)sh);
        s_copying = 0;
    }
    OSSetCurrentHeap(old);
    g_portDiscReads = reads;   /* not the game's reads: HWVX_AUTOINPUT's "+N" waits for those */
    if (sh == NULL) {
        e->failed = 1;
        return;
    }
    e->shadow = sh;
    s_numShadows++;
    s_shadowBytes += sh->size;
    if (s_needsRef) {
        /* its textures are in another pack: only the game loads those (the copy would load
         * it into our heap) */
        Port_Log("drawdist: quadrant %u: needs another pack, not copied", index);
        ReleaseShadow(e);
        e->failed = 1;
        return;
    }
    if (GETP(sh->geoms) == NULL || GETP(((QuadPack *)sh)->alloc) == NULL || !RefreshShadow(e)) {
        Port_Log("drawdist: quadrant %u: could not load a copy", index);
        ReleaseShadow(e);
        e->failed = 1;
        return;
    }
    sh->loaded = 1;
    Port_HeapUsage(Port_ExtraHeap(), &inUse, &size);
    Port_Log("drawdist: quadrant %u loaded (%u KB, %.2f ms); %u copies, %u KB, heap %u / %u KB", index,
             sh->size >> 10, (SDL_GetTicksNS() - t0) / 1e6, s_numShadows, s_shadowBytes >> 10, inUse >> 10,
             size >> 10);
}

/* ShaderHolder_Heartbeat's second half, for a shadow's geometry: the shader
 * states the game animated this frame. */
static void AnimateShaders(Extra *e) {
    u32 i;

    for (i = 0; i < e->numGX; i++) {
        Shader *shader = e->gx[i].shader;
        ShadedGeom *gx = e->gx[i].gx;
        ShaderParams *mtx = GETP(gx->params);

        if (GETP(shader->def)->isMovie != 0) {
            gx->texture = MovieManager_GetTextureIndex();
        }
        if (shader->texture.texture != 0) {
            gx->texture = shader->texture.texture;
        }
        if (mtx != NULL) {
            if (shader->hasColorKey != 0) {
                mtx->color = shader->color.color;
                mtx->mode = 2;
            }
            if (shader->hasPointKey != 0) {
                mtx->scrollX = shader->point.x;
                mtx->scrollY = shader->point.y;
            }
        }
    }
}

/* Whether an instance is within its draw distance (RenderQuadrant's test). */
static s32 InDrawDist(QuadInstance *inst, ThingRecord *model) {
    f32 distSq = Gfx_GetMinCameraDistSquared(&inst->pos);

    if (Port_DrawDistNoCull()) {
        return 1;
    }
    if ((u32)ViewManager_GetViewFormat() > 1) {
        distSq *= 4.0f;
    }
    if (inst->hasDrawDist) {
        return distSq <= inst->drawDist * inst->drawDist;
    }
    return !model->hasDrawDist || distSq <= model->drawDist * model->drawDist;
}

/* Draws `quad` (the game's copy or ours, `contents` the game's): its
 * geometry, its static instances, and the stand-ins for destructibles the
 * game hasn't spawned. */
static void DrawQuad(Extra *e, WorldQuad *quad, QuadContents *c) {
    typedef struct DrawThing {
        Thing base;
        u8 padAC[0x44];
    } DrawThing;
    DrawThing thing __attribute__((aligned(16)));
    QuadInstance *inst;
    ThingRecord *model;
    u32 i;
    s32 stand;

    for (i = 0; i < quad->numGeoms; i++) {
        Gfx_DrawGeometry(HND(GETP(GETP(quad->geoms)[i].geom)), HND(NULL));
    }
    if (c == NULL) {
        return;
    }
    for (i = 0; i < c->numInstances; i++) {
        inst = &GETP(c->instances)[i];
        model = GETP(inst->record);
        if (model == NULL || model->noDraw != 0 || GETP(model->geometry) == NULL) {
            continue;
        }
        if (THINGRECORD_IS_STATIC(model)) {
            if (!e->radiusDone) {
                /* what FileStreaming_SetActiveQuadrants sets when the game activates it */
                inst->radius = GetBoundingBoxRadius((Vector4 *)((Geometry *)GETP(model->geometry))->bboxMin,
                                                    &g_zeroVector);
            }
            stand = 0;
        } else {
            /* the game's destructible, if it has spawned one, draws itself */
            stand = c->dirty == 0 && GETP(inst->thing) == NULL &&
                    (inst->mode == 1 || (inst->mode == QUAD_OBJECT_DESTROYED && inst->respawn));
            if (!stand) {
                continue;
            }
        }
        if (!InDrawDist(inst, model)) {
            continue;
        }
        if (stand || Gfx_FrustrumCullSphereTestAllWindows(&inst->pos, inst->radius)) {
            thing.base.flags = 0;
            thing.base.quat = inst->quat;
            QuaternionToSubMatrix(&thing.base.quat, THING_MTX(&thing.base));
            thing.base.pos = inst->pos;
            thing.base.pos.w = 1.0f;
            Gfx_DrawGeometry(HND(GETP(model->geometry)), HND(THING_MTX(&thing.base)));
        }
    }
    e->radiusDone = 1;
}

/* Marks the quadrants within `depth` neighbour steps of `quad`. */
static void Want(World *world, WorldQuad *quad, u32 depth) {
    u32 j;

    s_wanted[quad - GETP(world->quads)] = 1;
    if (depth == 0) {
        return;
    }
    for (j = 0; j < quad->numNeighbors; j++) {
        Want(world, &GETP(world->quads)[GETP(quad->neighbors)[j]], depth - 1);
    }
}

/* World_Render, once the game has found each camera's quadrant and marked
 * the visible ones: draw (loading if need be) the extra quadrants. */
void Port_DrawDistRender(World *world, WorldQuad **current, u32 numCameras, WorldQuad *hub) {
    WorldQuad *quad;
    Extra *e;
    u32 i;
    u32 loads = 0;
    u64 t0;

    s_level = Port_DrawDistLevel();
    if (s_level == PORT_DRAWDIST_NORMAL) {
        if (s_numExtra != 0) {
            Port_DrawDistReleaseAll();
        }
        return;
    }
    if (world != s_world || s_extra == NULL) {
        Port_DrawDistReleaseAll();
        s_level = Port_DrawDistLevel();
        s_extra = SDL_calloc(world->numQuads, sizeof(Extra));
        s_wanted = SDL_calloc(world->numQuads, 1);
        if (s_extra == NULL || s_wanted == NULL) {
            Port_DrawDistReleaseAll();
            return;
        }
        s_numExtra = world->numQuads;
        s_world = world;
        Port_Log("drawdist: %s, %u quadrants, far plane %.0f, fog %.0f - %.0f",
                 s_level == PORT_DRAWDIST_ALL ? "everything" : "far", world->numQuads,
                 GETP(world->gameData) != NULL ? GETP(world->gameData)->farPlane : 0.0f,
                 GETP(world->gameData) != NULL ? GETP(world->gameData)->fogNear : 0.0f,
                 GETP(world->gameData) != NULL ? GETP(world->gameData)->fogFar : 0.0f);
    }
    SDL_memset(s_wanted, s_level == PORT_DRAWDIST_ALL, s_numExtra);
    if (s_level == PORT_DRAWDIST_FAR) {
        for (i = 0; i < numCameras && i < 5; i++) {
            if (current[i] != NULL) {
                Want(world, current[i], 2);
            }
        }
    }
    t0 = SDL_GetTicksNS();
    for (i = 0; i < s_numExtra; i++) {
        quad = &GETP(world->quads)[i];
        e = &s_extra[i];
        if (!s_wanted[i] || quad == hub || GETP(quad->bsp) == NULL) {
            /* not ours to draw: keep a copy a while, then let it go */
            if (e->shadow != NULL && ++e->unwanted > KEEP_FRAMES) {
                ReleaseShadow(e);
            }
            continue;
        }
        e->unwanted = 0;
        if (quad->visible) {
            continue;   /* the game draws it (or is loading it) */
        }
        if (quad->loaded && GETP(quad->geoms) != NULL) {
            DrawQuad(e, quad, GETP(quad->contents));   /* the game's copy, loaded but not shown */
            continue;
        }
        if (GETP(quad->geoms) != NULL) {
            continue;   /* the game's loader has just read it: shown next frame */
        }
        if (e->shadow == NULL && !e->failed && quad->numGeoms != 0 && quad->fileOffset != 0 &&
            g_quadFileID != -1 && !g_bLoadingThreadRunning && loads < LOADS_PER_FRAME &&
            SDL_GetTicksNS() - t0 < LOAD_BUDGET_NS) {
            LoadShadow(e, quad, i);
            loads++;
        }
        if (e->shadow != NULL) {
            AnimateShaders(e);
            DrawQuad(e, e->shadow, GETP(quad->contents));
        }
    }
}

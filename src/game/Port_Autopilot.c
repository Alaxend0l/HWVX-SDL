/* port, testing: HWVX_AUTOPILOT makes player 1's car drive itself, with the
 * game's own AI driving code (AI_CarA.c), so a scripted test drive plays the
 * level, or crosses the world, instead of idling at the start. A tool for
 * repeatable tests, not a general driver. Off by default, not in any menu;
 * deterministic like the rest of the game under HWVX_DETERMINISTIC (no
 * clock, no random numbers of its own).
 *
 * HWVX_AUTOPILOT=1 (objective), the kind of drive by mode:
 *  - Battle (progressions 3, 7): the game's combat AI the whole time, as a
 *    retail battle opponent: a real AICar run on player 1's car with the
 *    car's own AI script (CarAInfo's, for the difficulty), targets, weapons
 *    and items included.
 *  - Otherwise the level's current objective, where the HUD arrow points
 *    (g_TargetArrow, InterfaceManager.c): its position is only the
 *    destination. The route there is planned over the world's nav graph
 *    (NavHolder_FindPath, A*, as the AI does) from the nav point nearest the
 *    car to the one nearest the objective, followed as an AI car follows a
 *    path (AI_Car_SteerAlongPath looks one point ahead), then the last
 *    stretch is driven straight at the objective (AI_Car_DriveToTarget).
 *    Re-planned when the objective changes, when it moves 300 units from
 *    where the route ends, or when the car is 300 units off the route for
 *    3 s. When the objective is an enemy car (a fight), the combat AI drives
 *    until the objective moves on; so does it while objectives are left but
 *    the arrow points nowhere (Mission 1's "disable 2 enemies"). A world
 *    without nav points: straight at the objective. Drag races are driving
 *    only.
 *  - No objective (Joyride): the tour below.
 * HWVX_AUTOPILOT=tour: coverage, driving only, whatever the mode: nav points
 * in a fixed order (each at least 400 units away) along their nav paths, or,
 * with no nav points, each quadrant's centre in turn (the middle of what it
 * holds). HWVX_AUTOPILOT_ROUTE="x,z;x,z;..." overrides both: those points
 * in turn, planned over the nav graph the same way.
 *
 * Driving only, outside fights: steering, throttle, brake/reverse, the
 * sliding turn and the unstick button reach the player; none of the AI's
 * async events run then (weapons, scans, state choice).
 *
 * The log: "autopilot: plan ..." (route length, points), "reached point",
 * "re-plan", "objective N reached at T s", round results; a run ends with
 * "autopilot: PASSED" (the level won: a battle's series, or every objective
 * of the level done (World.objectives' done variables) when the level holds
 * the car for its results; or HWVX_AUTOPILOT_OBJECTIVES=N reached)
 * or "autopilot: FAILED" (lost; stuck: under 20 units moved in 10 s;
 * unsticking (reversing) 5 s; falling 200 below the lowest nav point / goal;
 * moved 500 units in a frame (the game reset the car); no goal; past
 * HWVX_AUTOPILOT_TIMEOUT s, default 600), then the game quits. Tour goals
 * not reached in HWVX_AUTOPILOT_GOAL_SECONDS (30) are skipped and logged.
 *
 * Player 1's control reader (Player_DefaultControlReader) asks
 * Port_AutopilotDrive first. The stand-in AICars live in host memory
 * (outside the game heap, so the game's allocations stay where they were)
 * and get player 1's Player header each frame; the AI code sets their
 * controls and those are copied back. Not in the attract demo, playback or
 * online play, where the pad (or the network) drives. */
#include <SDL3/SDL.h>
#include "common.h"
#include "CarA.h"
#include "AI.h"
#include "World.h"
#include "Port_Online.h"

void Port_AICarFollowPath(AICar *ai);
void Port_AICarAttack(AICar *ai, Thing *enemy, AIStateSlot *fallback);
extern u32 g_iDeltaTimeMS;

typedef struct Driver {
    AICar ai;
    AICarScript script;
    AIStateSlot slot;
    AIStateInfo info;
} Driver;

enum { KIND_OBJECTIVE, KIND_TOUR, KIND_ROUTE, KIND_BATTLE };

static Driver *s_drv;       /* driving only: a stand-in with a script of its own */
static AICar *s_combat;     /* the combat AI: a real AICar with the car's script */
static Player *s_fighter;   /* player 1, while the combat AI drives this frame */
static u32 s_shots;         /* frames the combat AI fired in */
static World *s_world;
static s32 s_kind;
static s32 s_drag;
static s32 s_done;
static u32 s_timeMs;
static u32 s_goals;         /* tour/route goals begun */
static s32 s_goalIndex;
static Vector4 s_goal;      /* the current destination */
static s32 s_haveGoal;
static u32 s_goalMs;
static u32 s_offMs;
static u32 s_unstickMs;
static u32 s_stillMs;
static Vector4 s_stillPos;
static Vector4 s_lastPos;
static Vector4 s_planStart;
static Vector4 s_planEnd;   /* where the planned route ends (its last nav point) */
static f32 s_lowestY;
static u32 s_objectiveId;
static u32 s_objectives;
static u32 s_lastPathIndex;
static s32 s_waiting;       /* the round was over last frame */
static u32 s_wins;
static f32 s_routePts[64][2];
static u32 s_numRoutePts;

static s32 EnvInt(const char *name, s32 def) {
    const char *e = getenv(name);

    return e != NULL && atoi(e) > 0 ? atoi(e) : def;
}

static s32 Mode(void) {
    static int mode = -1;   /* 0 off, 1 objective, 2 tour */

    if (mode < 0) {
        const char *e = getenv("HWVX_AUTOPILOT");

        mode = e == NULL ? 0 : strcmp(e, "tour") == 0 ? 2 : atoi(e) != 0;
    }
    return mode;
}

static void ParseRoute(void) {
    const char *e = getenv("HWVX_AUTOPILOT_ROUTE");
    float x;
    float z;
    int n;

    s_numRoutePts = 0;
    while (e != NULL && *e && s_numRoutePts < 64 && sscanf(e, "%f,%f%n", &x, &z, &n) == 2) {
        s_routePts[s_numRoutePts][0] = x;
        s_routePts[s_numRoutePts][1] = z;
        s_numRoutePts++;
        e += n;
        while (*e == ';' || *e == ' ') {
            e++;
        }
    }
}

static f32 Seconds(void) {
    return s_timeMs / 1000.0f;
}

/* Ends the run: the verdict in the log, then quit. */
static void End(const char *verdict, const char *why, Thing *car) {
    s_done = 1;
    Port_Log("autopilot: %s: %s (%.1f s, %u objectives, at %.0f %.0f %.0f)", verdict, why, Seconds(), s_objectives,
             car->pos.x, car->pos.y, car->pos.z);
    OSResetSystem(0, 0, 0);
}

/* The stand-ins live outside the game heap (host memory; their pointers
 * get static handles), so the game's allocations stay where they were. A
 * fresh pair per level, never freed: the AI's event queue may still hold
 * the old ones. */
static s32 Allocated(void **p, u32 size) {
    *p = SDL_aligned_alloc(64, size);
    if (*p != NULL) {
        memset(*p, 0, size);
    }
    return *p != NULL;
}

/* ---- planning over the nav graph ---------------------------------------- */

static s32 NearestNavPoint(Vector4 *pos) {
    u32 n = NavHolder_GetTotalNavPoints();
    f32 best = 1e30f;
    s32 k = -1;
    u32 i;

    for (i = 0; i < n; i++) {
        NavPoint *p = NavHolder_GetNavPoint(i);
        f32 d = PSVECSquareDistance(&p->pos, pos);

        if (p->enabled && d < best) {
            best = d;
            k = (s32)i;
        }
    }
    return k;
}

/* Plans the route to s_goal: path[] over the nav graph (empty without one,
 * or if no path is found: then straight at it). */
static void Plan(Thing *car, const char *why) {
    AICar *ai = &s_drv->ai;
    s32 from;
    s32 dest;
    f32 len = 0.0f;
    u32 n = 0;
    u32 i;

    memset(ai->path, 0, sizeof(ai->path));
    ai->pathIndex = 32;
    s_planStart = car->pos;
    s_planEnd = s_goal;
    s_offMs = 0;
    s_lastPathIndex = (u32)-1;
    if (NavHolder_GetTotalNavPoints() != 0) {
        from = THING_VFN(car, THING_V_GET_NEAREST_NAV_POINT, s32 (*)(Thing *))(car);
        dest = NearestNavPoint(&s_goal);
        if (from >= 0 && dest >= 0 && from != dest) {
            SETP(s_drv->slot.target, NavHolder_GetNavPoint(dest));
            AI_Car_PrepareNavPath(ai, from, dest);
        }
        for (i = 0; i < 32 && GETP(ai->path[i]) != NULL; i++, n++) {
            if (i > 0) {
                len += Sqrt(PSVECSquareDistance(&GETP(ai->path[i - 1])->pos, &GETP(ai->path[i])->pos));
            }
            s_planEnd = GETP(ai->path[i])->pos;
        }
    }
    Port_Log("autopilot: plan (%s): to %.0f %.0f %.0f, %u nav points, %.0f units, then %.0f straight", why, s_goal.x,
             s_goal.y, s_goal.z, n, len, Sqrt(PSVECSquareDistance(&s_planEnd, &s_goal)));
}

/* Distance squared, on the ground plan, from the car to the route segment
 * it is on. */
static f32 OffRouteSq(AICar *ai, Vector4 *pos) {
    NavPoint *next = GETP(ai->path[ai->pathIndex]);
    Vector4 *a = ai->pathIndex > 0 && GETP(ai->path[ai->pathIndex - 1]) != NULL ? &GETP(ai->path[ai->pathIndex - 1])->pos
                                                                                : &s_planStart;
    Vector4 ab;
    Vector4 ap;
    Vector4 c;
    f32 t;
    f32 len;

    PSVECSubtract(&next->pos, a, &ab);
    PSVECSubtract(pos, a, &ap);
    ab.y = ap.y = 0.0f;
    len = PSVECDotProduct(&ab, &ab);
    t = len > 0.0f ? PSVECDotProduct(&ap, &ab) / len : 0.0f;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    PSVECScale(&ab, &c, t);
    PSVECSubtract(&ap, &c, &c);
    return PSVECDotProduct(&c, &c);
}

/* One frame of driving to s_goal along the planned route. */
static void DriveRoute(Thing *car) {
    AICar *ai = &s_drv->ai;
    Vector4 goal;

    if (ai->pathIndex < 32 && GETP(ai->path[ai->pathIndex]) != NULL) {
        if (ai->pathIndex != s_lastPathIndex && s_lastPathIndex != (u32)-1) {
            Port_Log("autopilot: reached point %u of the route at %.1f s", ai->pathIndex, Seconds());
        }
        s_lastPathIndex = ai->pathIndex;
        s_offMs = OffRouteSq(ai, &car->pos) > 300.0f * 300.0f ? s_offMs + g_iDeltaTimeMS : 0;
        if (s_offMs > 3000) {
            Plan(car, "300 units off the route for 3 s");
        }
    }
    if (ai->pathIndex < 32 && GETP(ai->path[ai->pathIndex]) != NULL) {
        Port_AICarFollowPath(ai);
    } else if (!AI_Car_CheckIfStuck(ai)) {
        /* the last stretch, as AI_Car_SeekPoint drives to a target standing still */
        goal = s_goal;
        goal.y = car->pos.y;
        ai->steerPoint = goal;
        AI_Car_DriveToTarget(ai, &goal, 0.0f);
    }
}

/* ---- tour goals ---------------------------------------------------------- */

static s32 QuadCentre(u32 q, Vector4 *out) {
    WorldQuad *quad = &GETP(WORLD()->quads)[q];
    QuadContents *c = GETP(quad->contents);
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
    u32 n = 0;
    u32 i;

    if (GETP(quad->bsp) == NULL || c == NULL) {
        return 0;
    }
    for (i = 0; i < c->numInstances; i++, n++) {
        x += GETP(c->instances)[i].pos.x, y += GETP(c->instances)[i].pos.y, z += GETP(c->instances)[i].pos.z;
    }
    for (i = 0; i < c->numFxPoints; i++, n++) {
        x += GETP(c->fxPoints)[i].pos.x, y += GETP(c->fxPoints)[i].pos.y, z += GETP(c->fxPoints)[i].pos.z;
    }
    for (i = 0; i < c->numItems; i++, n++) {
        x += GETP(c->items)[i].pos.x, y += GETP(c->items)[i].pos.y, z += GETP(c->items)[i].pos.z;
    }
    if (n == 0) {
        return 0;
    }
    out->x = x / n;
    out->y = y / n;
    out->z = z / n;
    out->w = 1.0f;
    return 1;
}

/* The next tour / route goal into s_goal; 0 if there is none. */
static s32 NextGoal(Thing *car) {
    u32 tries;
    u32 n;
    NavPoint *p;

    s_goalMs = 0;
    if (s_kind == KIND_ROUTE) {
        s_goalIndex = (s_goalIndex + 1) % (s32)s_numRoutePts;
        s_goal.x = s_routePts[s_goalIndex][0];
        s_goal.z = s_routePts[s_goalIndex][1];
        s_goal.y = car->pos.y;
        s_goals++;
        return 1;
    }
    n = NavHolder_GetTotalNavPoints();
    if (n != 0) {
        for (tries = 0; tries < 64; tries++) {
            p = NavHolder_GetNavPoint((s32)((s_goals * 7919u + tries * 31u + 1u) % n));
            if (p != NULL && p->enabled && PSVECSquareDistance(&p->pos, &car->pos) > 400.0f * 400.0f) {
                s_goal = p->pos;
                s_goals++;
                return 1;
            }
        }
        return 0;
    }
    n = WORLD()->numQuads;
    for (tries = 0; tries < n; tries++) {
        s_goalIndex = (s_goalIndex + 1) % (s32)n;
        if (QuadCentre(s_goalIndex, &s_goal) && PSVECSquareDistance(&s_goal, &car->pos) > 100.0f * 100.0f) {
            s_goals++;
            Port_Log("autopilot: goal %u: quadrant %d's centre", s_goals, s_goalIndex);
            return 1;
        }
    }
    return 0;
}

static s32 GoalReached(Thing *car) {
    AICar *ai = &s_drv->ai;
    f32 dx = car->pos.x - s_goal.x;
    f32 dz = car->pos.z - s_goal.z;

    return (ai->pathIndex >= 32 || GETP(ai->path[ai->pathIndex]) == NULL) && dx * dx + dz * dz < 80.0f * 80.0f;
}

/* ---- checks -------------------------------------------------------------- */

static s32 Watch(AICar *ai, Thing *car) {
    u32 dt = g_iDeltaTimeMS;

    if (PSVECSquareDistance(&car->pos, &s_lastPos) > 500.0f * 500.0f) {
        End("FAILED", "the car moved over 500 units in a frame (reset by the game)", car);
        return 0;
    }
    s_lastPos = car->pos;
    if (car->pos.y < s_lowestY - 200.0f) {
        End("FAILED", "fell below the lowest nav point / goal", car);
        return 0;
    }
    s_unstickMs = ai->stuckTimer > 0 ? s_unstickMs + dt : 0;
    if (s_unstickMs > 5000) {
        End("FAILED", "unsticking (reversing) for 5 s", car);
        return 0;
    }
    if (PSVECSquareDistance(&car->pos, &s_stillPos) > 20.0f * 20.0f) {
        s_stillPos = car->pos;
        s_stillMs = 0;
    } else if ((s_stillMs += dt) > 10000) {
        End("FAILED", "stuck: under 20 units moved in 10 s", car);
        return 0;
    }
    return 1;
}

/* Battles: player 1's kills and the most any other player has, on change. */
static void LogKills(Player *player1) {
    static s32 lastMine = -1;
    static s32 lastBest = -1;
    s32 best = 0;
    u8 i;
    u8 n = PlayerManager_PlayerCount();

    if (s_kind != KIND_BATTLE) {
        return;
    }
    for (i = 0; i < n; i++) {
        Player *p = PlayerManager_GetPlayerByIndex(i);

        if (p != player1 && p->kills > best) {
            best = p->kills;
        }
    }
    if (player1->kills != lastMine || best != lastBest) {
        lastMine = player1->kills;
        lastBest = best;
        Port_Log("autopilot: kills at %.1f s: player 1 %d, best opponent %d", Seconds(), lastMine, best);
    }
}

/* The round's end: log the result; the run ends with the level (or a
 * battle's series). */
static s32 WatchRound(Player *player1, Thing *car) {
    extern s8 g_bWaitingForRoundReset;
    extern u32 g_playerNumThatWon;
    u32 wins = GameSettings_GetCodeVariable(0x191, 1);
    s32 over = g_bWaitingForRoundReset != 0;
    s32 won;

    LogKills(player1);
    if (over && !s_waiting) {
        won = wins > s_wins || g_playerNumThatWon == 1;
        Port_Log("autopilot: round over at %.1f s: %s (wins %u, winner %d)", Seconds(), won ? "won" : "lost", wins,
                 (s32)g_playerNumThatWon);
        s_wins = wins;
        if (s_kind == KIND_BATTLE) {
            /* a battle test asks for a fight, not a win: the first round,
             * with shots fired */
            Port_Log("autopilot: battle round: player 1 %d kills, %u frames firing", player1->kills, s_shots);
            End(s_shots != 0 ? "PASSED" : "FAILED", s_shots != 0 ? (won ? "fought and won the round" : "fought, lost the round")
                                                                 : "never fired", car);
            s_waiting = 1;
            return 0;
        }
        {
            End(won ? "PASSED" : "FAILED", won ? "the level won" : "the level lost", car);
            s_waiting = 1;
            return 0;
        }
    }
    s_waiting = over;
    return 1;
}

/* The level's objectives (World.objectives) that have a done variable:
 * how many, and how many of them are done. */
static void Objectives(u32 *total, u32 *done) {
    World *w = WORLD();
    u32 i;

    *total = *done = 0;
    for (i = 0; i < w->numObjectives; i++) {
        WorldObjectiveInfo *info = GETP(GETP(w->objectives)[i].info);

        if (info != NULL && GETP(info->done) != NULL) {
            (*total)++;
            *done += GETP(info->done)->value != 0.0f;
        }
    }
}

/* Held by the level (a results or "press A" screen): the level is won once
 * every objective is done. */
static void WatchHeld(Thing *car) {
    u32 total;
    u32 done;

    Objectives(&total, &done);
    if (total != 0 && done == total && s_kind != KIND_BATTLE) {
        Port_Log("autopilot: all %u objectives done at %.1f s", total, Seconds());
        End("PASSED", "the level won", car);
    }
}

/* ---- the combat AI -------------------------------------------------------- */

/* The car's own AI script (CarAInfo's set, at 0x90), for the difficulty, as
 * State_GamePlay.c picks an AI car's. */
static u32 CarScript(Thing *car) {
    AIScriptSet *set = GETP(((CarA *)car)->vehicleIface);
    s32 d = GameSettings_GetDifficulty();
    u32 i;

    if (set == NULL) {
        return 0;
    }
    if (d >= 0 && d < 4 && set->scripts[d] != 0) {
        return set->scripts[d];
    }
    for (i = 0; i < 4; i++) {
        if (set->scripts[i] != 0) {
            return set->scripts[i];
        }
    }
    return 0;
}

/* The enemy of a mission fight: the arrow's car, else the nearest other car. */
static Thing *Enemy(Thing *car) {
    extern s32 g_TargetArrow[7];
    Thing *t = PTR(g_TargetArrow[3]);
    Thing *cars[8];
    u32 n;
    u32 i;
    f32 best = 1e30f;

    if (t != NULL && t != car && GETP(t->vtable) == (u32 *)g_carAVTable) {
        return t;
    }
    t = NULL;
    n = ThingManager_GetActiveCars((void **)cars, 8);
    for (i = 0; i < n; i++) {
        if (cars[i] != car && !(cars[i]->flags & 0x1000) && PSVECSquareDistance(&cars[i]->pos, &car->pos) < best) {
            best = PSVECSquareDistance(&cars[i]->pos, &car->pos);
            t = cars[i];
        }
    }
    return t;
}

/* One frame of the combat AI on player 1's car; 0 if the car has no script. */
static s32 Fight(Player *player, Thing *car) {
    AIPlayerParams params;
    CarA *carA = (CarA *)car;
    s32 health;
    f32 grip;

    if (s_combat == NULL) {
        memset(&params, 0, sizeof(params));
        params.script = CarScript(car);
        if (params.script == 0 || !Allocated((void **)&s_combat, sizeof(AICar))) {
            return 0;
        }
        params.kind = AI_KIND_CAR;
        memcpy(s_combat, player, sizeof(s_combat->base.header));
        AI_Car_Init(s_combat, &params);
        Port_Log("autopilot: the combat AI takes over at %.1f s", Seconds());
    }
    memcpy(s_combat, player, sizeof(s_combat->base.header));
    SETP(AI_PLAYER(s_combat)->controlReader, AI_Car_Heartbeat);
    /* the AI's reset scales its car's health and grip by the script: not
     * player 1's (a retail AI car's is made for that) */
    health = car->health;
    grip = carA->gripScale;
    AI_Car_Heartbeat(s_combat);
    if (!(player->flags & 1)) {
        /* the car's script targets human cars only: give it the enemy when it
         * has no car to fight (a mission's, or a battle's, AI opponents) */
        Port_AICarAttack(s_combat, Enemy(car), &s_drv->slot);
    }
    car->health = health;
    carA->gripScale = grip;
    memcpy(player, s_combat, 0x20);   /* every control: weapons and items too */
    s_fighter = player;
    return 1;
}

/* After the AI's async events (PlayerManager_Heartbeat), before the cars
 * read their controls: the weapon event fires and switches through its
 * player's controls, which for the combat stand-in aren't player 1's; copy
 * them over, as a retail AI car's event writes its own. */
void Port_AutopilotAfterAIEvents(void) {
    if (s_fighter != NULL && s_combat != NULL) {
        memcpy(s_fighter, s_combat, 0x20);
        if (AI_PLAYER(s_combat)->buttonsHeld & 0x800) {
            if (s_shots++ == 0) {
                Port_Log("autopilot: first shot at %.1f s", Seconds());
            }
        }
    }
    s_fighter = NULL;
}

/* ---- the frame ------------------------------------------------------------ */

static void StartLevel(Thing *car) {
    s32 prog;
    s32 sub;
    u32 i;
    Vector4 c;
    f32 best = 1e30f;

    /* fresh stand-ins: the AI's event queue may still hold the old ones */
    s_drv = NULL;
    s_combat = NULL;
    if (!Allocated((void **)&s_drv, sizeof(Driver))) {
        return;
    }
    s_world = WORLD();
    StateProgressionManager_GetProgressionID(&prog, &sub);
    ParseRoute();
    s_drag = prog == 2 || prog == 5;
    s_kind = s_numRoutePts != 0 ? KIND_ROUTE : Mode() == 2 ? KIND_TOUR : prog == 3 || prog == 7 ? KIND_BATTLE
                                                                                                   : KIND_OBJECTIVE;
    s_done = 0;
    s_timeMs = 0;
    s_goals = 0;
    s_goalIndex = -1;
    s_haveGoal = 0;
    s_objectiveId = 0;
    s_objectives = 0;
    s_waiting = 0;
    s_wins = GameSettings_GetCodeVariable(0x191, 1);
    s_shots = 0;
    s_lastPos = s_stillPos = car->pos;
    s_stillMs = s_unstickMs = 0;
    s_lowestY = car->pos.y;
    for (i = 0; i < NavHolder_GetTotalNavPoints(); i++) {
        if (((NavPoint *)NavHolder_GetNavPoint(i))->pos.y < s_lowestY) {
            s_lowestY = ((NavPoint *)NavHolder_GetNavPoint(i))->pos.y;
        }
    }
    for (i = 0; i < WORLD()->numQuads; i++) {
        if (QuadCentre(i, &c)) {
            if (c.y < s_lowestY) {
                s_lowestY = c.y;
            }
            if (NavHolder_GetTotalNavPoints() == 0 && PSVECSquareDistance(&c, &car->pos) < best) {
                best = PSVECSquareDistance(&c, &car->pos);
                s_goalIndex = (s32)i - 1;   /* the tour starts at the nearest quadrant */
            }
        }
    }
    s_drv->info.type = AI_TARGET_NAV_POINT;   /* approachNear/Far 0: no slowing for the goal */
    SETP(s_drv->slot.info, &s_drv->info);
    SETP(s_drv->ai.base.script, &s_drv->script);
    SETP(s_drv->ai.current, &s_drv->slot);
    s_drv->ai.stuckTimer = -120;
    s_drv->ai.wayPoint = -1;
    s_drv->ai.pathDest = -1;
    s_drv->ai.pathIndex = 32;
    Port_Log("autopilot: progression %d level %d: %s, %u nav points", prog, sub,
             s_kind == KIND_ROUTE ? "HWVX_AUTOPILOT_ROUTE's points" : s_kind == KIND_TOUR ? "a tour"
             : s_kind == KIND_BATTLE ? "the combat AI" : "the objectives",
             NavHolder_GetTotalNavPoints());
}

s32 Port_AutopilotDrive(Player *player) {
    extern s32 g_playbackStatus;
    extern s32 g_TargetArrow[7];
    Thing *car;
    Thing *target;
    AICar *ai;
    Player *pl;
    Vector4 *objective;
    u32 id;
    s32 tour;

    if (!Mode() || player->port != 1 || GameSettings_GetCodeVariable(0x198, 0) != 0 || g_playbackStatus == 2 ||
        Port_OnlineActive()) {
        return 0;
    }
    car = GETP(player->vehicle);
    if (car == NULL || GETP(car->vtable) != (u32 *)g_carAVTable || WORLD() == NULL) {
        return 0;
    }
    if (s_world != WORLD()) {
        StartLevel(car);
    }
    if (s_drv == NULL || s_done) {
        return 0;
    }
    s_timeMs += g_iDeltaTimeMS;
    if (player->flags & 1) {
        /* held by the level's script (intro, results): the game's controls,
         * only the round watched */
        if (WatchRound(player, car)) {
            WatchHeld(car);
        }
        return 0;
    }
    if (s_timeMs > (u32)EnvInt("HWVX_AUTOPILOT_TIMEOUT", 600) * 1000u) {
        End("FAILED", "timed out", car);
        return 0;
    }
    if (!WatchRound(player, car)) {
        return 0;
    }
    if (s_kind == KIND_BATTLE) {
        if (!Fight(player, car)) {
            End("FAILED", "the car has no AI script to fight with", car);
            return 0;
        }
        return Watch(s_combat, car);
    }
    ai = &s_drv->ai;
    tour = s_kind != KIND_OBJECTIVE;
    if (!tour) {
        objective = PTR(g_TargetArrow[0]);
        target = PTR(g_TargetArrow[3]);
        id = (u32)g_TargetArrow[0] ^ ((u32)g_TargetArrow[3] << 1);
        if (objective == NULL) {
            u32 total;
            u32 done;

            Objectives(&total, &done);
            if (total == 0 || s_drag || s_timeMs < 5000) {
                tour = 1;   /* nothing to go for (Joyride), or not yet */
            } else if (Fight(player, car)) {
                /* objectives left but no arrow: a fight ("disable 2 enemies") */
                return Watch(s_combat, car);
            } else {
                tour = 1;
            }
        } else {
            if (id != s_objectiveId) {
                if (s_objectiveId != 0) {
                    s_objectives++;
                    Port_Log("autopilot: objective %u reached at %.1f s", s_objectives, Seconds());
                    if (s_objectives >= (u32)EnvInt("HWVX_AUTOPILOT_OBJECTIVES", 1000000)) {
                        End("PASSED", "HWVX_AUTOPILOT_OBJECTIVES reached", car);
                        return 0;
                    }
                }
                s_objectiveId = id;
                s_goal = *objective;
                Plan(car, "a new objective");
            } else if (PSVECSquareDistance(objective, &s_goal) > 300.0f * 300.0f) {
                s_goal = *objective;
                Plan(car, "the objective moved");
            }
            if (!s_drag && target != NULL && target != car && GETP(target->vtable) == (u32 *)g_carAVTable &&
                Fight(player, car)) {
                /* a fight: the combat AI until the objective moves on */
                return Watch(s_combat, car);
            }
        }
    }
    /* the stand-in drives the player's car with cleared controls */
    memcpy(ai, player, sizeof(ai->base.header));
    Control_ClearControl(ai);
    if (tour) {
        s_goalMs += g_iDeltaTimeMS;
        if (!s_haveGoal || GoalReached(car) ||
            (s_goalMs > (u32)EnvInt("HWVX_AUTOPILOT_GOAL_SECONDS", 30) * 1000u &&
             (Port_Log("autopilot: skipped goal %u: not reached in time (at %.0f %.0f %.0f)", s_goals, car->pos.x,
                       car->pos.y, car->pos.z), 1))) {
            if (!NextGoal(car)) {
                End("FAILED", "no goal it can drive to", car);
                return 0;
            }
            s_haveGoal = 1;
            Plan(car, "the next goal");
        }
    }
    DriveRoute(car);
    if (!Watch(ai, car)) {
        return 0;
    }
    /* driving only: steering, throttle, brake, the sliding turn and unstick buttons */
    pl = AI_PLAYER(ai);
    player->stickX = pl->stickX;
    player->throttle = pl->throttle;
    player->brake = pl->brake;
    player->button14 = pl->button14;
    player->button15 = pl->button15;
    return 1;
}

/* Testing: HWVX_QUADLOG=1 logs the quadrant camera 1 is in whenever it
 * changes, with the count of World_Render frames ("quadrant: frame F camera
 * in Q"); tools/autopilot_drive.sh compares the routes by it. */
void Port_QuadLog(World *world, WorldQuad **current, u32 numCameras) {
    static int on = -1;
    static u32 frame;
    static s32 last = -2;
    s32 q;

    if (on < 0) {
        const char *e = getenv("HWVX_QUADLOG");

        on = e != NULL && atoi(e) != 0;
    }
    if (!on) {
        return;
    }
    frame++;
    q = numCameras != 0 && current[0] != NULL ? (s32)(current[0] - GETP(world->quads)) : -1;
    if (q != last) {
        last = q;
        Port_Log("quadrant: frame %u camera in %d", frame, q);
    }
}

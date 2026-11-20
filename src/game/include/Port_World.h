/* Custom worlds (superproject docs/plans/custom-worlds.md): a world made from
 * a modder's geometry, composed over a base level as it loads. Its archive
 * (built by `hwvx-x world`) holds only its own packs: one hub quadrant pack
 * (render geometry) and one collision pack (terrain grid; surfaces are
 * indices into the base collision pack's materials). The base level's world
 * pack is kept for everything else (scripts, chase cameras, fog, item pack)
 * and adapted in memory: one hub quadrant, the start moved, what lives in
 * the base's coordinates (volumes, speakers, creatures, teleporters, the
 * radar map) taken out of the way. Port-only file.
 *
 * HWVX_WORLD=<folder>/world.jsonc registers the world as a track (its "id",
 * a Joyride-style track over "base", a track number) whose level is the
 * base's; HWVX_START=race:mode=joyride,world=<id> starts it. Its archive is
 * "@world\<archive>" (<folder>/<archive>.xgc). */
#ifndef PORT_WORLD_H
#define PORT_WORLD_H


/* Port_ContentInit, after the base counts: registers HWVX_WORLD's track. */
void Port_WorldRegister(void);
/* DVDOpen: "@world\<file>" -> the host path of <file>.xgc in the world's
 * folder (SDL_free it), its release in *format; NULL for other names. */
char *Port_WorldResolveFile(const char *name, int *format);
/* LoadWorldProcess state 0, the world pack in (pointers not refreshed yet),
 * of registered track `track` (-1: a base level): composes the world if the
 * track is the custom world's. */
void Port_WorldLoaded(void *worldPack, int track);
/* State 2, the base collision pack in: loads the world's own (synchronous). */
void Port_WorldCollisionLoaded(void);
/* State 7, World.collision set to the base pack's grid: points it at the
 * world's grid with the base's materials. */
void Port_WorldBindCollision(void);
/* The custom world is the one loaded. */
int Port_WorldActive(void);

#endif

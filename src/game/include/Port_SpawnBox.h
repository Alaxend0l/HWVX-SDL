/* The box every start (spawn point, tile car) is shown and checked with: the envelope of every
 * car's collision box, per axis the lowest min and highest max (CarAInfo/0/collider: type 0,
 * the box's min and max corners), over the cars the content registry counts (the data's, a
 * mod's, the prototype's). In the car's frame: x its side, y up (0: where a tile car's
 * position is), z forward. Retail data, every release: x -5..5 (cars 5, 12, 17), y -1.4
 * (car 7)..5.25 (car 25, Surfin' School Bus), z -14..13 (car 25).
 * One function for the viewer's markers and checks (viewer_level.c, viewer_world.c), online's
 * spawn spots (Port_Online.c) and tools/xdl_level.py (through the viewer's `spawnbox`). */
#ifndef PORT_SPAWNBOX_H
#define PORT_SPAWNBOX_H

/* The envelope (computed once: each car's pack loaded and released); 1, or 0 if no car could
 * be read (then retail's numbers). *cars: how many were read. all: every car the registry
 * counts; 0: the data's own only (online: every machine must find the same spots, and their
 * mods may differ). */
int Port_SpawnEnvelope(float min[3], float max[3], int *cars, int all);

/* The same without computing it (drawing: no pack loads in the middle of a frame): what
 * Port_SpawnEnvelope found, or retail's numbers before it ran; 1 once it has. */
int Port_SpawnEnvelopeKnown(float min[3], float max[3], int all);

#endif

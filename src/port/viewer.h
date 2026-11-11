/* Viewer mode (viewer.c, viewer_graph.c): HWVX_VIEWER=<port> runs the game
 * without its game flow, as a preview and editing server for the data
 * browser (tools/browser). It listens on 127.0.0.1:<port> (0: any free port,
 * logged as "viewer: listening on 127.0.0.1:N") for line-delimited JSON:
 * list and load packs, read the object graph, get and set fields, select and
 * frame objects, move the camera, save a frame. The protocol is
 * docs/viewer-protocol.md. Off by default; nothing here runs without the
 * switch. */
#ifndef VIEWER_H
#define VIEWER_H

#include "port.h"

struct DataPack;

/* 1 when HWVX_VIEWER is set. */
int Port_ViewerOn(void);
/* Main_Init: instead of the game's first state. 1 on success. */
s32 Port_ViewerStart(void);
/* Main_Loop: one frame, the commands that arrived and the preview. 0 quits. */
s32 Port_ViewerFrame(void);
/* DataPack_LoadDataPack: a pack's pointer list (slot offsets from its base)
 * and its reference relocations ({slot offset, id} pairs), which the loader
 * doesn't keep. Copied; a no-op unless the viewer is on. */
void Port_ViewerNotePack(struct DataPack *pack, const u32 *pointers, u32 numPointers, const u32 *refs, u32 numRefs);

#endif

/* The PS2 prototype's content in the game being played: its import modes
 * (docs/xdl-format/README.md §9.1). Port-only. Port_Proto.c says what each
 * mode brings and how; proto.c (port.h) where the prototype is and which
 * mode the player picked. */
#ifndef PORT_PROTO_H
#define PORT_PROTO_H

#include "port.h"

struct DataPackRef;
struct DataPack;

/* Port_ContentData.c LoadIdsLock: the prototype's entries in ids.lock as the
 * mode wants them (added, kept "active": false when the mode leaves them
 * out, so their numbers and saves wait). */
void Port_ProtoSyncLock(const char *path);
/* After the registry is set up: the pictures the prototype's entries show
 * (its car select's thumbnails, a pack kept loaded for the run). */
void Port_ProtoInit(void);
/* A prototype pack's references (HW_share, its HW_Cars shared pack) are the
 * prototype's packs too: their names get the prefix. */
void Port_ProtoRefs(struct DataPackRef *refs);
/* Port_ContentPackSource: the pack of a retail pack's number the prototype
 * replaces in mode "replace" (its TOC id, *source its archive), else id
 * with *source = archive unchanged. */
u32 Port_ProtoReplacement(const char *archive, u32 type, u32 id, const char **source);
/* An entry's proto://<archive>/<type>_<id> data link: the id, *source the
 * prototype's archive name ("~HW_Cars"). id itself if the link isn't one. */
u32 Port_ProtoLink(const char *link, u32 type, u32 id, const char **source);
/* A texture of a prototype pack the registry keeps (archive "~HW_IPack",
 * rest "interface_3/texture/19"): its texture id, -1 if none. */
s32 Port_ProtoTexture(const char *archive, const char *rest);
/* FileStreaming_InitializeDataPack: a prototype pack, once fixed up (its
 * cars' camera modes the release has). */
void Port_ProtoPackLoaded(struct DataPack *pack, const char *name, u32 type, u32 id);

#endif

/* .xdl: a cache of converted packs (xdl.c). docs/xdl-format/README.md
 * (HWVX-X-FILE-EDITOR) has the design; this is its step 1, the packs as the
 * port's loader leaves them in memory, read back with every conversion
 * skipped.
 *
 * HWVX_XDL=1 loads packs from the cache where it has them; HWVX_XDL_WRITE=1
 * writes every pack loaded from its source into it. The cache is
 * <pref>/xdl/<gc|pc|ps2>/<file>.xdl, one per source file, or under
 * HWVX_XDL_DIR. A cache file whose source changed (size, modification time),
 * or written by another version of the converters, is not read.
 *
 * A cached pack is a set of segments, each the bytes of one heap block as
 * the source load left it, and fixups: every pointer slot in them, as the
 * segment it points into. The pack's slots come from its pointer list, the
 * slots a converter writes from the converter itself (Xdl_NoteSlot), and
 * nothing is guessed from what memory holds. */
#ifndef XDL_H
#define XDL_H

#include "port.h"

/* A cached pack's segments. */
enum {
    XDL_SEG_MAIN,       /* the pack's tables, blob and relocations, as read and converted */
    XDL_SEG_SOUND,      /* its sound data as CreateSFX and CreateMIDI read it */
    XDL_SEG_BANK,       /* PS2: the rebuilt song bank, freed once the song is loaded */
    XDL_SEG_SFX,        /* PS2: the decoded samples, freed once CreateSFX has them */
    XDL_SEG_RAW_HEADER, /* the bytes a small read (File_Read, under 0xC800) left in */
    XDL_SEG_RAW_SOUND,  /*   the heap buffer it borrowed: the header's, the sound's */
    XDL_SEG_RAW_MAIN,   /*   and the blob's, put back there as a cached pack loads */
    XDL_SEG_BLOCK,      /* the first block a converter allocated and the pack keeps */
    XDL_MAX_SEGS = XDL_SEG_BLOCK + 8
};

/* What the pack does with a kept block (Xdl_BlockRole). */
enum {
    XDL_ROLE_SOUNDDATA,     /* DataPack.soundData: the converted geometry */
    XDL_ROLE_KEEP,          /* PortGeometry_Keep: PS2 textures */
};

/* dvd.c: the cache of the pack file `name` (the GameCube name the game asks
 * for), whose source is `sourcePath` in format `fmt`; NULL when the cache is
 * off or there's nothing to read or write. */
void *Xdl_OpenFile(const char *name, int fmt, const char *sourcePath);
/* The same, in the cache's folder `folder` (NULL: the release's, as above):
 * the PS2 prototype's packs are in "ps2proto" (dvd.c). */
void *Xdl_OpenFileIn(const char *name, int fmt, const char *sourcePath, const char *folder);
void Xdl_CloseFile(void *xf);
/* The source file an open cache is of (NULL for NULL). */
const char *Xdl_SourcePath(void *xf);
int Xdl_Reading(void);
int Xdl_Writing(void);
/* The cache in `dir`, read and/or written, whatever the environment says:
 * a release converted ahead of time (Port_Convert.c writes it, the launcher
 * has the game read it). Before the first pack file is opened. */
void Xdl_Use(const char *dir, int reading, int writing);
/* Reading and writing on or off, the folder kept (mixed builds' first pass). */
void Xdl_SetModes(int reading, int writing);
/* 1 if the environment sets any of HWVX_XDL, HWVX_XDL_WRITE, HWVX_XDL_DIR. */
int Xdl_EnvSet(void);
/* What a cache file made now is stamped with: one with another version,
 * compiler or settings is not read. */
void Xdl_Stamp(u32 *version, u32 *compiler, u32 *settings);

/* ---- writing: recorded around a pack's source load ---------------------- */

/* A pack at `srcOffset` in its file starts loading: record it (when writing,
 * and it isn't cached yet). Every Begin is closed by an End or an Abort;
 * packs loaded inside (the packs one refers to) are recorded on their own. */
void Xdl_BeginPack(void *xf, u32 srcOffset, u32 type, u32 id);
void Xdl_AbortPack(void);
void Xdl_PackHeader(const void *header, u32 size);
/* A slot the loader itself fills in later (a quadrant's pack refs): cached
 * as it holds now, as read. */
void Xdl_KeepValue(void *slot);
/* The file bytes a small read left in its heap buffer (XDL_SEG_RAW_*). */
void Xdl_RawRead(int seg, const void *bytes, u32 size);
/* Converters: a pointer slot they stored (or cleared), and a block of the
 * game heap they allocated (`seg`: XDL_SEG_BANK, XDL_SEG_SFX or
 * XDL_SEG_BLOCK; its allocated size). Xdl_BlockDone: a block about to be
 * freed (BANK, SFX), as it is now. No-ops unless a pack is recorded. */
void Xdl_NoteSlot(void *slot);
void Xdl_NoteBlock(int seg, void *block, u32 size);
void Xdl_BlockDone(void *block);
/* The sound data, converted, just before CreateSFX reads it: `size` bytes
 * at `start` (all the read wrote: rounded up to 32 bytes), its pointer table (offsets from soundBase) and song bank. */
void Xdl_Sound(u8 *start, u32 size, const u32 *pointers, u32 numPointers, u8 *soundBase, void *songBank);
/* The load finished: the pack's tables, blob and relocations (`size` bytes
 * at `main`, as read: rounded up to 32 bytes), its lists and the block DataPack.soundData holds;
 * written to the cache. */
void Xdl_EndPack(void *soundData, u8 *main, u32 size, u8 *base, const u32 *pointers, u32 numPointers,
                 const u32 *ptrRelocs, u32 numPtrRelocs);

/* ---- reading ------------------------------------------------------------- */

typedef struct XdlPack XdlPack;

/* The cached pack at `srcOffset` of the file, or NULL. */
XdlPack *Xdl_FindPack(void *xf, u32 srcOffset);
void Xdl_FreePack(XdlPack *xp);
void Xdl_Header(XdlPack *xp, void *header, u32 size);
/* A segment's size (allocated size for BANK, SFX and blocks; 0: none). */
u32 Xdl_SegSize(XdlPack *xp, int seg);
int Xdl_BlockRole(XdlPack *xp, int seg);
/* The song bank: the segment it is in and its offset there, or -1 (none). */
int Xdl_BankRef(XdlPack *xp, u32 *offset);
void Xdl_ReadSeg(XdlPack *xp, int seg, void *dst, u32 from, u32 size);
/* Where a segment now is (NULL: gone); then the fixups whose slot and target
 * are both placed and not yet applied are applied. */
void Xdl_PlaceSeg(XdlPack *xp, int seg, void *at);

#endif

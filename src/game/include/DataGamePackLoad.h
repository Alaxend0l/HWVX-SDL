#ifndef DATAGAMEPACKLOAD_H
#define DATAGAMEPACKLOAD_H

#include "port.h"

enum DataPackArchiveMagic {
    ARCHIVE_MAGIC_1 = 0x020100A0,
    ARCHIVE_MAGIC_2 = 0x040100AE,
    ARCHIVE_VERSION = 0xB7
};

typedef struct FileTOCHeader {
    u32 magic1;
    u32 magic2;
    u32 version;
    u32 directoryCount;
    u32 unknown;
} FileTOCHeader;

typedef struct DataPackShader {
  s32 shader; /* 0x00 */
  u8 pad04[0x8];
} DataPackShader;

typedef struct DataPackRef {
  char name[0xC];        /* 0x00 */
  u32 id;                /* 0x0C */
  PTR32(struct DataPack) pack; /* 0x10 */
} DataPackRef;

/* A pack's scripts (DataPack.base): the records the game asks for by id. */
typedef struct DataPackScript {
    PTR32(u8) script;           /* 0x00 */
    u32 id;                     /* 0x04 */
} DataPackScript;

typedef struct DataPackScripts {
    u32 count;                  /* 0x00 */
    PTR32(DataPackScript) entries; /* 0x04 */
} DataPackScripts;

typedef struct DataPack {
  PTR32(struct DataPackScripts) orderedList;                /* 0x00 */
  u32 numRelocs;           /* 0x04 */
  PTR32(s32) relocs;             /* 0x08 */
  u32 numSoundRelocs;      /* 0x0C */
  PTR32(u32) soundRelocs;        /* 0x10 */
  PTR32(void) palettes;          /* 0x14 */
  u32 numShaders;          /* 0x18 */
  u32 numGX;               /* 0x1C */
  PTR32(u16) shaderIDs;          /* 0x20 */
  PTR32(u32) gxList;             /* 0x24 */
  u32 dataSize;            /* 0x28 */
  u32 numTextures;         /* 0x2C */
  PTR32(u16) textureIDs;         /* 0x30 */
  u32 numSounds;           /* 0x34 */
  PTR32(u16) soundIDs;           /* 0x38 */
  PTR32(void) soundData;         /* 0x3C */
  PTR32(u8) soundBase;           /* 0x40 */
  u32 textureData;         /* 0x44 */
  PTR32(u8) sfxData;             /* 0x48 */
  PTR32(void) paletteData;       /* 0x4C */
  PTR32(DataPackShader) shaders; /* 0x50 */
  u32 numTriggers;         /* 0x54 */
  PTR32(u8) triggers;            /* 0x58 */
  PTR32(void) song;              /* 0x5C */
  PTR32(void) songBank;          /* 0x60 */
  s8 songLoaded;           /* 0x64 */
  u8 pad65[3];             /* 0x65 */
  DataPackRef refs[2];     /* 0x68 */
  u32 soundDataSize;       /* 0x90 */
} DataPack;

typedef struct GXObject {
  u8 pad0[0x1C];
  u32 count;       /* 0x1C */
  PTR32(s32) shaderList; /* 0x20 */
} GXObject;
typedef struct QuadPackRef {
  char name[0xC]; /* 0x00 */
  u32 id;         /* 0x0C */
  PTR32(DataPack) pack; /* 0x10 */
} QuadPackRef;

/* The quad pack's header carries its pack references inline, at 0x2C. */
typedef struct QuadPackHeader {
  u32 valid;           /* 0x00 */
  u32 numPointers;     /* 0x04 */
  u32 numRelocs;       /* 0x08 */
  PTR32(s32) relocs;         /* 0x0C */
  u32 pad10[3];        /* 0x10 */
  u32 numShaders;      /* 0x1C */
  PTR32(s32) shaderList;     /* 0x20 */
  u32 dataSize;        /* 0x24 */
  u32 numTexRelocs;    /* 0x28 */
  QuadPackRef refs[2]; /* 0x2C */
} QuadPackHeader;
typedef struct QuadPack {
  u8 pad00[0xC];
  u32 enabled;    /* 0x0C */
  PTR32(void) data;     /* 0x10 */
  PTR32(void) vertices; /* 0x14 */
  u8 pad18[0x10];
  union {
    PTR32(GXObject) gxObject;            /* 0x28 */
    PTR32(struct QuadPackHeader) header; /* 0x28 */
  };
  s32 gxBase;         /* 0x2C */
  u8 pad30[0x4];
  u8 flag34; /* 0x34 */
  u8 pad35[0x7];
  s32 fileOffset; /* 0x3C */
  u32 size;       /* 0x40 */
  PTR32(void) alloc;    /* 0x44 */
} QuadPack;

#endif
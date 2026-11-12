/* Viewer mode: a pack's textures and sounds as the game has them, for the
 * data browser (the protocol's media commands, docs/viewer-protocol.md).
 *
 * Nothing here decodes on its own: a texture is its texture object
 * (m_gxTexObjArray, what Render_CreateTextureSlot made of the pack's header)
 * decoded by the renderer's own decoder (gx_tex.c GX_PortDecodeTexObj: the
 * GameCube's tiled formats, the PC's DXT and RGB(A), the PS2's palettised
 * textures as the loader expanded them, a mod's colour recipe, the PC's
 * squared colours); a sound is the sample the mixer plays, in ARAM after
 * the loader's conversions (PCM16 swapped to host order, the PS2's SPU-ADPCM
 * decoded: Port_Data.c, Port_PS2.c) and CreateSFX_; a song is the MIDI
 * player's copy (CreateMIDI) and its bank's samples (CreateMIDI_).
 *
 * Decoded pixels and samples go to the browser through a shared memory
 * block (the "media blob", not the socket): the reply names it, its key, and
 * the serial number the payload was written with. Play and stop go through
 * the game's own voices (GetVoice, SetVoiceState) and music (SetMusicState),
 * so a sound is heard as the game mixes it.
 *
 * Packs load with their sounds in viewer mode (the sound manager is started
 * here); HWVX_VIEWER_AUDIO=0 keeps them out as before. */
#include "port_internal.h"
#include "gx_render.h"
#include "viewer_internal.h"
#include "Voice.h"

#define MEDIA_MAGIC 0x4D565748u     /* "HWVM" */
#define MEDIA_VERSION 1
#define MEDIA_HEADER 64u
#define MAX_PLAYING 16
#define VOICE_ID(v) BE32((v)->id)  /* as Voice.c reads a voice's id */

/* The blob's header, little-endian as the hosts are. */
typedef struct MediaHeader {
    u32 magic, version;
    u64 key;
    u32 serial;         /* the reply's: a later request rewrites the payload */
    u32 length;         /* the payload's bytes */
    u32 dataOffset;     /* MEDIA_HEADER */
    u32 capacity;       /* bytes after the header */
    u32 reserved[8];
} MediaHeader;
_Static_assert(sizeof(MediaHeader) == MEDIA_HEADER, "MediaHeader layout");

/* MIDI.c's types, as CreateMIDI leaves them (swapped to host order) */
typedef struct MidiInst {
    u32 addr;           /* ARAM: samples for 16 bits (CreateMIDI_), else bytes */
    u32 length;
    u32 loop;           /* from addr; ~0: one-shot */
    u32 rate;           /* Hz at the base note */
    u8 field10;
    u8 bits;            /* 8, 16, else ADPCM */
    u8 field12, field13;
    u8 prog;            /* 0x80: a drum (one per note of lo..hi) */
    u8 field15;
    u8 lo, hi;          /* note range */
    u16 attack, decay, sustain, release;
} MidiInst;
_Static_assert(sizeof(MidiInst) == 0x20, "MIDI_INST layout");

typedef struct __attribute__((packed)) MidiHeader {
    char riff[4];
    u32 size;
    char form[4];       /* "MIDI" (an IT module: "IMPM" at 0) */
    char mthd[4];
    u32 mthdSize;
    u16 format, numTracks, division;
    char mtrk[4];
    u32 trackSize;
    u8 track[];
} MidiHeader;

typedef struct MidiSong {
    u32 tick, tickTime, time, tempo, tempoScale;
} MidiSong;

extern GXTexObj m_gxTexObjArray[511];
extern SFX g_sound[0xFE];
extern MidiHeader *g_pHdr;      /* MIDI.c: the song CreateMIDI holds */
extern u8 *g_pSnd;              /* MIDI.c: its bank (MIDI_SND: count at 0x10, entries at 0x20) */
extern MidiSong g_song;         /* MIDI.c: the playing position */
extern s32 g_musicState;        /* 1 playing, 2 paused, 3 stopped */
extern void (*SetMusicState)(s32);
extern s32 (*DestroyMusic)(void);
u32 GetNoteRate_(u32 rate, u8 note);

static struct {
    int audio;                  /* packs load their sounds */
    ViewerShm shm;
    u64 key;
    u32 serial, gen;
    u32 voices[MAX_PLAYING];    /* voice ids the viewer started */
    int songPlaying;
    DataPack *songPack;         /* whose song the MIDI player holds */
    char playing[200];          /* what was played last */
} m;

static const char *const s_formatNames[] = {
    "I4", "I8", "IA4", "IA8", "RGB565", "RGB5A3", "RGBA8", "?7", "C4", "C8", "C14X2", "?11", "?12", "?13", "CMPR",
};

static const char *FormatName(int fmt) {
    static const char *const pc[] = { "PC_RGB8", "PC_RGBA8", "PC_?2", "PC_?3", "PC_DXT1", "PC_DXT5" };

    if (fmt >= 0 && fmt < (int)SDL_arraysize(s_formatNames)) {
        return s_formatNames[fmt];
    }
    if (fmt >= GX_TF_PORT && fmt - GX_TF_PORT < (int)SDL_arraysize(pc)) {
        return pc[fmt - GX_TF_PORT];
    }
    return "?";
}

static const char *const s_wrapNames[] = { "clamp", "repeat", "mirror" };

void ViewerMedia_Start(void) {
    const char *e = SDL_getenv("HWVX_VIEWER_AUDIO");

    m.audio = !(e != NULL && *e == '0');
    if (m.audio) {
        SoundManager_Init();    /* as a state's start does: packs load their sounds and songs */
    }
    DataPack_EnableLoadingSounds(m.audio ? 1 : 0);
}

void ViewerMedia_Shutdown(void) {
    ViewerShm_Destroy(&m.shm);
}

/* ---- the blob ---------------------------------------------------------------- */

/* Room for `size` bytes of payload; NULL with a message. */
static u8 *BlobBegin(u32 size, char *err, size_t errSize) {
    MediaHeader *h;

    if (m.shm.base == NULL || m.shm.size - MEDIA_HEADER < size) {
        size_t cap = (size_t)size + size / 4 + 0x10000;
        char tag[40];

        ViewerShm_Destroy(&m.shm);
        m.key = Viewer_RandomU64();
        SDL_snprintf(tag, sizeof(tag), "%016llx-m%u", (unsigned long long)m.key, m.gen++);
        if (!ViewerShm_Create(&m.shm, MEDIA_HEADER + cap, tag)) {
            SDL_snprintf(err, errSize, "no shared memory for %u bytes", size);
            return NULL;
        }
        h = (MediaHeader *)m.shm.base;
        h->magic = MEDIA_MAGIC;
        h->version = MEDIA_VERSION;
        h->key = m.key;
        h->dataOffset = MEDIA_HEADER;
        h->capacity = (u32)cap;
    }
    h = (MediaHeader *)m.shm.base;
    h->serial = 0;      /* being written */
    SDL_MemoryBarrierRelease();
    return m.shm.base + MEDIA_HEADER;
}

/* The payload is in: the reply's "blob". */
static void BlobEnd(JsonWriter *w, u32 length) {
    MediaHeader *h = (MediaHeader *)m.shm.base;
    char key[24];

    if (++m.serial == 0) {
        m.serial = 1;
    }
    h->length = length;
    SDL_MemoryBarrierRelease();
    h->serial = m.serial;
    SDL_snprintf(key, sizeof(key), "%016llx", (unsigned long long)m.key);
    Jw_BeginObject(w, "blob", 1);
    Jw_String(w, "name", m.shm.name);
    Jw_String(w, "key", key);
    Jw_Int(w, "size", (long long)m.shm.size);
    Jw_Int(w, "serial", m.serial);
    Jw_Int(w, "offset", MEDIA_HEADER);
    Jw_Int(w, "length", length);
    Jw_EndObject(w);
}

/* ---- packs and nodes ------------------------------------------------------- */

DataPack *Viewer_FindDataPack(const char *name, VPack **out) {
    int i, r;
    char full[200];

    *out = NULL;
    if (name == NULL) {
        return NULL;
    }
    if (SDL_strncmp(name, "base://", 7) == 0) {
        name += 7;
    }
    for (i = 0; i < VIEWER_MAX_PACKS; i++) {
        VPack *vp = &g_viewerPacks[i];

        SDL_snprintf(full, sizeof(full), "%s/%s", vp->archiveName, vp->name);
        if (vp->used && SDL_strcasecmp(full, name) == 0) {
            *out = vp;
            return vp->pack;
        }
    }
    /* a share pack a loaded one refers to (refs: the archive's name and the id) */
    for (i = 0; i < VIEWER_MAX_PACKS; i++) {
        VPack *vp = &g_viewerPacks[i];

        for (r = 0; vp->used && r < 2; r++) {
            DataPack *sp = GETP(vp->pack->refs[r].pack);

            SDL_snprintf(full, sizeof(full), "%.12s/share_%u", vp->pack->refs[r].name, vp->pack->refs[r].id);
            if (sp != NULL && vp->pack->refs[r].name[0] != '\0' && SDL_strcasecmp(full, name) == 0) {
                return sp;
            }
        }
    }
    return NULL;
}

/* The pack shown, or `pack`. */
static DataPack *PackArg(const JsonNode *req, VPack **vp, char *packName, size_t nameSize, char *err, size_t errSize) {
    const char *name = Json_GetString(req, "pack", NULL);
    DataPack *dp;
    int i;

    if (name == NULL) {
        /* the shown one: the viewer's pack last loaded or selected (viewer.c keeps it; the first used is
         * its stand-in when the request names none and several are loaded) */
        *vp = Viewer_ShownPack();
        for (i = 0; *vp == NULL && i < VIEWER_MAX_PACKS; i++) {
            if (g_viewerPacks[i].used) {
                *vp = &g_viewerPacks[i];
            }
        }
        if (*vp == NULL) {
            SDL_snprintf(err, errSize, "no pack is loaded");
            return NULL;
        }
        SDL_snprintf(packName, nameSize, "%s/%s", (*vp)->archiveName, (*vp)->name);
        return (*vp)->pack;
    }
    if ((dp = Viewer_FindDataPack(name, vp)) == NULL) {
        SDL_snprintf(err, errSize, "pack %s is not loaded", name);
        return NULL;
    }
    SDL_strlcpy(packName, SDL_strncmp(name, "base://", 7) == 0 ? name + 7 : name, nameSize);
    return dp;
}

/* `node`: "KIND/N" in the pack (or "base://ARCHIVE/PACK/KIND/N", which names the pack too). */
static DataPack *NodeArg(const JsonNode *req, const char *kind, VPack **vp, char *packName, size_t nameSize,
                         u32 *index, char *err, size_t errSize) {
    const char *node = Json_GetString(req, "node", NULL);
    char buf[256], *slash;
    const char *rest;
    DataPack *dp;
    size_t kl = SDL_strlen(kind);

    if (node == NULL) {
        SDL_snprintf(err, errSize, "which %s? (node: %s/N)", kind, kind);
        return NULL;
    }
    if (SDL_strncmp(node, "base://", 7) == 0) {
        /* base://ARCHIVE/PACK/kind/N: the pack is the part before kind */
        SDL_strlcpy(buf, node + 7, sizeof(buf));
        slash = SDL_strchr(buf, '/');
        slash = slash != NULL ? SDL_strchr(slash + 1, '/') : NULL;
        if (slash == NULL) {
            SDL_snprintf(err, errSize, "%s: base://ARCHIVE/PACK/%s/N", node, kind);
            return NULL;
        }
        *slash = '\0';
        if ((dp = Viewer_FindDataPack(buf, vp)) == NULL) {
            SDL_snprintf(err, errSize, "pack %s is not loaded", buf);
            return NULL;
        }
        SDL_strlcpy(packName, buf, nameSize);
        rest = node + 7 + (slash - buf) + 1;
    } else {
        if ((dp = PackArg(req, vp, packName, nameSize, err, errSize)) == NULL) {
            return NULL;
        }
        rest = node;
    }
    if (SDL_strncmp(rest, kind, kl) != 0 || rest[kl] != '/' || rest[kl + 1] < '0' || rest[kl + 1] > '9') {
        SDL_snprintf(err, errSize, "%s is not a %s (%s/N)", node, kind, kind);
        return NULL;
    }
    *index = (u32)SDL_strtoul(rest + kl + 1, NULL, 10);
    return dp;
}

/* ---- textures ------------------------------------------------------------------ */

/* The texture object the game draws texture `i` of dp with; NULL: none. */
static const GXTexObj *TexObj(DataPack *dp, u32 i, u16 *texId) {
    u16 id;

    if (i >= dp->numTextures || GETP(dp->textureIDs) == NULL) {
        return NULL;
    }
    id = GETP(dp->textureIDs)[i];
    *texId = id;
    return id < 511 ? &m_gxTexObjArray[id] : NULL;
}

/* The release's own stored format, and the PS2's palette: the pack's texture
 * header (0x20 bytes on the GameCube, 0x10 on the others: DataPack_RefreshTextures). */
static void WriteSource(JsonWriter *w, DataPack *dp, u32 i, const GXTexObj *obj) {
    u32 stride = dp->portFormat == PORT_DATA_GC ? 0x20 : 0x10;
    const u8 *hdr = (const u8 *)PTR(dp->textureData) + stride * i;
    const u8 *base = GETP(dp->base), *image = PTR(obj->dummy[0]);
    u16 mips;

    memcpy(&mips, hdr + 6, 2);
    mips = bswap16(mips);   /* as LoadBTF_RAM reads it (__sthbrx) */
    Jw_Int(w, "storedLevels", mips == 0 ? 1 : mips);
    if (dp->portFormat == PORT_DATA_PS2) {
        u32 clut;

        memcpy(&clut, hdr + 12, 4);
        /* PortPS2_Textures expands a palettised (PSMT8) texture into a block of its own */
        if (image != NULL && (image < base || image >= base + dp->dataSize)) {
            Jw_String(w, "source", "PSMT8");
            Jw_BeginObject(w, "palette", 1);
            Jw_Int(w, "clut", clut);
            Jw_Int(w, "colours", 256);
            Jw_EndObject(w);
        } else {
            Jw_String(w, "source", "PSMCT32");
            Viewer_WriteNull(w, "palette");
        }
    } else {
        Jw_String(w, "source", FormatName((int)obj->dummy[2]));
        Viewer_WriteNull(w, "palette");
    }
}

static void WriteTexInfo(JsonWriter *w, DataPack *dp, u32 i) {
    u16 id = 0;
    const GXTexObj *obj = TexObj(dp, i, &id);
    GXPortTexInfo t;

    Jw_Int(w, "index", i);
    Jw_Int(w, "textureId", id);
    if (obj == NULL || !GX_PortTexObjInfo(obj, &t)) {
        Jw_Bool(w, "drawn", 0);    /* no texture object: past 512 (Render_CreateTextureSlot) or none */
        return;
    }
    Jw_Bool(w, "drawn", 1);
    Jw_String(w, "format", FormatName(t.fmt));
    Jw_Int(w, "gxFormat", t.fmt);
    Jw_Int(w, "width", t.w);
    Jw_Int(w, "height", t.h);
    Jw_Int(w, "levels", t.levels);
    Jw_Int(w, "bytes", t.bytes);
    WriteSource(w, dp, i, obj);
    Jw_BeginObject(w, "flags", 1);
    Jw_String(w, "wrapS", s_wrapNames[t.wrapS < 3 ? t.wrapS : 0]);
    Jw_String(w, "wrapT", s_wrapNames[t.wrapT < 3 ? t.wrapT : 0]);
    Jw_Bool(w, "linear", t.magLinear);
    Jw_Bool(w, "mipmaps", t.levels > 1);
    Jw_Bool(w, "squared", t.squared);
    Jw_Bool(w, "mod", t.mod);
    Jw_EndObject(w);
}

/* The nodes whose texture slots name texture `index` of `share` (NULL: the pack's own). */
static void WriteUsers(JsonWriter *w, VPack *vp, const s32 *users, DataPack *share, u32 index) {
    DataPack *dp = vp->pack;
    u32 i, n = 0;
    s32 seen[16];
    u32 numSeen = 0, k;

    Jw_BeginArray(w, "usedBy", 1);
    for (i = 0; i < dp->numRelocs && n < 16; i++) {
        u32 slot = (u32)GETP(dp->relocs)[i * 2] & ~PORT_TEXRELOC_U16;
        u32 id = (u32)GETP(dp->relocs)[i * 2 + 1];
        DataPack *target = dp;
        s32 owner;

        if (id >> 16) {
            target = GETP(dp->refs[dp->refs[0].id != (id >> 16)].pack);
            id &= 0xFFFF;
        }
        if (target != (share != NULL ? share : dp) || id != index || slot / 4 >= vp->size / 4 || users == NULL) {
            continue;
        }
        owner = users[slot / 4];
        for (k = 0; k < numSeen && seen[k] != owner; k++) {
        }
        if (k < numSeen || owner < 0 || (u32)owner >= vp->numNodes) {
            continue;
        }
        seen[numSeen++] = owner;
        Jw_String(w, NULL, vp->nodes[owner].id);
        n++;
    }
    Jw_EndArray(w);
}

static int CmdTextures(JsonWriter *w, const JsonNode *req, char *err, size_t errSize) {
    VPack *vp = NULL;
    char packName[200];
    DataPack *dp = PackArg(req, &vp, packName, sizeof(packName), err, errSize), *used[2] = { NULL, NULL };
    u32 i, r;
    s32 *users;

    if (dp == NULL) {
        return 0;
    }
    users = vp != NULL ? Viewer_WordUsers(vp) : NULL;
    Jw_String(w, "pack", packName);
    Jw_BeginArray(w, "textures", 0);
    for (i = 0; i < dp->numTextures; i++) {
        char id[40];

        Jw_BeginObject(w, NULL, 1);
        SDL_snprintf(id, sizeof(id), "texture/%u", i);
        Jw_String(w, "id", id);
        Jw_String(w, "pack", packName);
        WriteTexInfo(w, dp, i);
        if (vp != NULL) {
            WriteUsers(w, vp, users, NULL, i);
        }
        Jw_EndObject(w);
    }
    /* the share packs' textures it refers to (its texture relocations with a pack id) */
    if (vp != NULL && Json_GetBool(req, "shared", 1)) {
        for (r = 0; r < 2; r++) {
            DataPack *sp = GETP(dp->refs[r].pack);
            char share[64];

            if (sp == NULL || dp->refs[r].name[0] == '\0' || sp == used[0]) {
                continue;
            }
            used[r] = sp;
            SDL_snprintf(share, sizeof(share), "%.12s/share_%u", dp->refs[r].name, dp->refs[r].id);
            for (i = 0; i < sp->numTextures; i++) {
                u32 k, refd = 0;
                char id[120];

                for (k = 0; k < dp->numRelocs && !refd; k++) {
                    u32 rid = (u32)GETP(dp->relocs)[k * 2 + 1];

                    refd = (rid >> 16) == dp->refs[r].id && (rid & 0xFFFF) == i;
                }
                if (!refd) {
                    continue;
                }
                Jw_BeginObject(w, NULL, 1);
                SDL_snprintf(id, sizeof(id), "base://%s/texture/%u", share, i);
                Jw_String(w, "id", id);
                Jw_String(w, "pack", share);
                Jw_Bool(w, "shared", 1);
                WriteTexInfo(w, sp, i);
                WriteUsers(w, vp, users, sp, i);
                Jw_EndObject(w);
            }
        }
    }
    Jw_EndArray(w);
    SDL_free(users);
    return 1;
}

static int CmdTexture(JsonWriter *w, const JsonNode *req, char *err, size_t errSize) {
    VPack *vp = NULL;
    char packName[200], id[240];
    u32 index = 0, total = 0, at = 0;
    DataPack *dp = NodeArg(req, "texture", &vp, packName, sizeof(packName), &index, err, errSize);
    const GXTexObj *obj;
    GXPortTexInfo t;
    long long only = Json_GetInt(req, "level", -1);
    int l, first, last, translucent = 0, cutout = 0;
    u16 texId = 0;
    u8 *out;

    if (dp == NULL) {
        return 0;
    }
    if (index >= dp->numTextures) {
        SDL_snprintf(err, errSize, "%s has no texture/%u", packName, index);
        return 0;
    }
    if ((obj = TexObj(dp, index, &texId)) == NULL || !GX_PortTexObjInfo(obj, &t)) {
        SDL_snprintf(err, errSize, "texture/%u of %s isn't drawn (no texture object)", index, packName);
        return 0;
    }
    if (only >= t.levels) {
        SDL_snprintf(err, errSize, "texture/%u has %d levels", index, t.levels);
        return 0;
    }
    first = only >= 0 ? (int)only : 0;
    last = only >= 0 ? (int)only : t.levels - 1;
    for (l = first; l <= last; l++) {
        int lw = (t.w >> l) > 0 ? t.w >> l : 1, lh = (t.h >> l) > 0 ? t.h >> l : 1;

        total += (u32)lw * lh * 4;
    }
    if ((out = BlobBegin(total, err, errSize)) == NULL) {
        return 0;
    }
    SDL_snprintf(id, sizeof(id), "base://%s/texture/%u", packName, index);
    Jw_String(w, "id", id);
    Jw_String(w, "pack", packName);
    WriteTexInfo(w, dp, index);
    Jw_String(w, "pixels", "rgba8");
    Jw_BeginArray(w, "levelData", 0);
    for (l = first; l <= last; l++) {
        int lw = (t.w >> l) > 0 ? t.w >> l : 1, lh = (t.h >> l) > 0 ? t.h >> l : 1, p;

        GX_PortDecodeTexObj(obj, l, out + at);
        if (l == first) {
            for (p = 0; p < lw * lh; p++) {
                u8 a = out[at + p * 4 + 3];

                translucent |= a != 0 && a != 255;
                cutout |= a == 0;
            }
        }
        Jw_BeginObject(w, NULL, 1);
        Jw_Int(w, "level", l);
        Jw_Int(w, "width", lw);
        Jw_Int(w, "height", lh);
        Jw_Int(w, "offset", at);
        Jw_Int(w, "bytes", (long long)lw * lh * 4);
        Jw_EndObject(w);
        at += (u32)lw * lh * 4;
    }
    Jw_EndArray(w);
    Jw_String(w, "alpha", translucent ? "blended" : cutout ? "cutout" : "opaque");
    BlobEnd(w, total);
    return 1;
}

/* ---- sounds ------------------------------------------------------------------ */

static const char *ReleaseOf(DataPack *dp) {
    return dp->portFormat == PORT_DATA_PC ? "pc" : dp->portFormat == PORT_DATA_PS2 ? "ps2" : "gc";
}

/* What the release stores; what the mixer plays is in ARAM (bits). */
static const char *StoredFormat(DataPack *dp, u8 bits) {
    if (dp->portFormat == PORT_DATA_PS2) {
        return "spu-adpcm";
    }
    if (bits == 8) {
        return "pcm8";
    }
    if (bits == 16) {
        return dp->portFormat == PORT_DATA_GC ? "pcm16be" : "pcm16le";
    }
    return "adpcm";
}

static const SFX *SoundOf(DataPack *dp, u32 i) {
    u16 id;

    if (i >= dp->numSounds || GETP(dp->soundIDs) == NULL) {
        return NULL;
    }
    id = GETP(dp->soundIDs)[i];
    return id != 0 && id <= 0xFE && g_sound[id - 1].data != 0 ? &g_sound[id - 1] : NULL;
}

/* A sample in ARAM: its first byte and bytes a sample, or NULL. */
static const u8 *Samples(u32 addr, u32 length, u8 bits, int *bytesPer) {
    const u8 *aram = Port_ARAMBase();
    u64 start, end;

    if (aram == NULL || (bits != 8 && bits != 16)) {
        return NULL;
    }
    *bytesPer = bits / 8;
    start = (u64)addr * (u64)*bytesPer;
    end = start + (u64)length * (u64)*bytesPer;
    return end <= (16u << 20) ? aram + start : NULL;
}

static void WriteSample(JsonWriter *w, u32 length, u32 loop, u32 rate, u8 bits) {
    Jw_Int(w, "rate", rate);
    Jw_Int(w, "length", length);
    Jw_Float(w, "seconds", rate ? (f32)length / (f32)rate : 0.0f);
    if (loop == 0xFFFFFFFFu || loop >= length) {
        Viewer_WriteNull(w, "loop");
    } else {
        Jw_Int(w, "loop", loop);
    }
    Jw_Int(w, "bits", bits);
    Jw_String(w, "plays", bits == 16 ? "pcm16" : bits == 8 ? "pcm8" : "adpcm (silent in the port's mixer)");
}

static const MidiInst *Instruments(u32 *count) {
    *count = 0;
    if (g_pHdr == NULL || g_pSnd == NULL || SDL_memcmp(g_pHdr->form, "MIDI", 4) != 0) {
        return NULL;
    }
    *count = *(u32 *)(g_pSnd + 0x10);
    if (*count > 1024) {
        *count = 0;
    }
    return (const MidiInst *)(g_pSnd + 0x20);
}

/* The song's events: tempo, length, channels and notes (one SMF track, as ProcessMIDI reads it). */
static void WriteSongInfo(JsonWriter *w) {
    const MidiHeader *h = g_pHdr;
    const u8 *p = h->track, *end = h->track + h->trackSize;
    u32 tick = 0, notes = 0, channels = 0, firstTempo = 0, tempos = 0, markers = 0, events = 0;
    u8 status = 0;

    if (h->trackSize > h->size) {
        end = p;
    }
    while (p < end) {
        u32 delta = 0, c, len;

        do {
            c = *p++;
            delta = (delta << 7) | (c & 0x7F);
        } while ((c & 0x80) && p < end);
        tick += delta;
        if (p >= end) {
            break;
        }
        if (*p & 0x80) {
            status = *p++;
        }
        events++;
        if (status == 0xFF) {
            u8 type = p < end ? *p++ : 0;

            len = 0;
            do {
                c = p < end ? *p++ : 0;
                len = (len << 7) | (c & 0x7F);
            } while (c & 0x80);
            if (type == 0x51 && len == 3 && p + 3 <= end) {
                if (tempos++ == 0) {
                    firstTempo = (u32)p[0] << 16 | (u32)p[1] << 8 | p[2];
                }
            }
            p += len;
            if (type == 0x2F) {
                break;
            }
            continue;
        }
        if (status == 0xF0 || status == 0xF7) {
            len = 0;
            do {
                c = p < end ? *p++ : 0;
                len = (len << 7) | (c & 0x7F);
            } while (c & 0x80);
            p += len;
            continue;
        }
        switch (status & 0xF0) {
        case 0x90:
            if (p + 1 < end && p[1] != 0) {
                notes++;
                channels |= 1u << (status & 0xF);
            }
            p += 2;
            break;
        case 0xB0:
            markers += p < end && *p == 0x6E;   /* a loop marker (FindMarkers) */
            p += 2;
            break;
        case 0xC0:
        case 0xD0:
            p += 1;
            break;
        default:
            p += 2;
            break;
        }
    }
    Jw_String(w, "kind", "midi");
    Jw_Int(w, "division", h->division);
    Jw_Int(w, "tempo", firstTempo ? firstTempo : 500000);   /* us a quarter note (ProcessMIDI's default) */
    Jw_Float(w, "bpm", 60000000.0f / (f32)(firstTempo ? firstTempo : 500000));
    Jw_Int(w, "tempoChanges", tempos);
    Jw_Int(w, "ticks", tick);
    Jw_Int(w, "events", events);
    Jw_Int(w, "notes", notes);
    Jw_Int(w, "markers", markers);
    {
        int ch, n = 0;

        Jw_BeginArray(w, "channels", 1);
        for (ch = 0; ch < 16; ch++) {
            if (channels & (1u << ch)) {
                Jw_Int(w, NULL, ch);
                n++;
            }
        }
        Jw_EndArray(w);
        Jw_Int(w, "tracks", n);   /* channels used: the song is one SMF track */
    }
}

static void WriteSongState(JsonWriter *w) {
    Jw_Bool(w, "playing", g_pHdr != NULL && g_musicState == 1);
    Jw_Int(w, "tick", g_pHdr != NULL ? g_song.tick : 0);
}

static int CmdSounds(JsonWriter *w, const JsonNode *req, char *err, size_t errSize) {
    VPack *vp = NULL;
    char packName[200];
    DataPack *dp = PackArg(req, &vp, packName, sizeof(packName), err, errSize);
    u32 i, count;
    const MidiInst *inst;

    if (dp == NULL) {
        return 0;
    }
    Jw_String(w, "pack", packName);
    Jw_String(w, "release", ReleaseOf(dp));
    Jw_Bool(w, "audio", m.audio);
    Jw_BeginArray(w, "sounds", 0);
    for (i = 0; i < dp->numSounds; i++) {
        const SFX *sfx = SoundOf(dp, i);
        char id[40];

        Jw_BeginObject(w, NULL, 1);
        SDL_snprintf(id, sizeof(id), "sound/%u", i);
        Jw_String(w, "id", id);
        Jw_Int(w, "index", i);
        if (sfx == NULL) {
            Jw_Bool(w, "loaded", 0);   /* sound loading off, or out of sound memory */
        } else {
            Jw_Bool(w, "loaded", 1);
            Jw_Int(w, "soundId", GETP(dp->soundIDs)[i]);
            Jw_String(w, "format", StoredFormat(dp, sfx->bits));
            WriteSample(w, sfx->length, sfx->loop, sfx->rate, sfx->bits);
            Jw_Int(w, "volume", sfx->vol);
        }
        Jw_EndObject(w);
    }
    Jw_EndArray(w);
    inst = dp->songLoaded ? Instruments(&count) : NULL;
    if (dp->songLoaded && g_pHdr != NULL) {
        Jw_BeginObject(w, "song", 0);
        Jw_String(w, "id", "song");
        if (SDL_memcmp(g_pHdr->form, "MIDI", 4) == 0) {
            WriteSongInfo(w);
        } else {
            Jw_String(w, "kind", "it");
        }
        WriteSongState(w);
        Jw_Int(w, "instruments", inst != NULL ? count : 0);
        Jw_EndObject(w);
    } else {
        Viewer_WriteNull(w, "song");
    }
    Jw_BeginArray(w, "instruments", 0);
    for (i = 0; inst != NULL && i < count; i++) {
        char id[40];

        Jw_BeginObject(w, NULL, 1);
        SDL_snprintf(id, sizeof(id), "instrument/%u", i);
        Jw_String(w, "id", id);
        Jw_Int(w, "index", i);
        Jw_Bool(w, "drum", inst[i].prog == 0x80);
        Jw_Int(w, "program", inst[i].prog);
        Jw_Int(w, "lo", inst[i].lo);
        Jw_Int(w, "hi", inst[i].hi);
        Jw_String(w, "format", StoredFormat(dp, inst[i].bits));
        WriteSample(w, inst[i].length, inst[i].loop, inst[i].rate, inst[i].bits);
        Jw_BeginObject(w, "envelope", 1);
        Jw_Int(w, "attack", inst[i].attack);
        Jw_Int(w, "decay", inst[i].decay);
        Jw_Int(w, "sustain", inst[i].sustain);
        Jw_Int(w, "release", inst[i].release);
        Jw_EndObject(w);
        Jw_EndObject(w);
    }
    Jw_EndArray(w);
    return 1;
}

/* "sound/N" or "instrument/N" of the pack: its sample. 0 with a message. */
static int SampleArg(const JsonNode *req, DataPack **dpOut, char *packName, size_t nameSize, char *what,
                     size_t whatSize, u32 *addr, u32 *length, u32 *loop, u32 *rate, u8 *bits, u32 *soundId,
                     const MidiInst **instOut, char *err, size_t errSize) {
    const char *node = Json_GetString(req, "node", "");
    const char *tail = SDL_strrchr(node, '/');
    VPack *vp;
    u32 index, count;
    DataPack *dp;

    *soundId = 0;
    *instOut = NULL;
    if (tail != NULL && tail - node >= 10 && SDL_strncmp(tail - 10, "instrument", 10) == 0) {
        const MidiInst *inst;

        if ((dp = NodeArg(req, "instrument", &vp, packName, nameSize, &index, err, errSize)) == NULL) {
            return 0;
        }
        inst = dp->songLoaded ? Instruments(&count) : NULL;
        if (inst == NULL || index >= count) {
            SDL_snprintf(err, errSize, "%s has no instrument/%u (no song loaded with it)", packName, index);
            return 0;
        }
        *addr = inst[index].addr;
        *length = inst[index].length;
        *loop = inst[index].loop;
        *rate = inst[index].rate;
        *bits = inst[index].bits;
        *instOut = &inst[index];
        SDL_snprintf(what, whatSize, "instrument/%u", index);
    } else {
        const SFX *sfx;

        if ((dp = NodeArg(req, "sound", &vp, packName, nameSize, &index, err, errSize)) == NULL) {
            return 0;
        }
        if ((sfx = SoundOf(dp, index)) == NULL) {
            if (index >= dp->numSounds) {
                SDL_snprintf(err, errSize, "%s has no sound/%u", packName, index);
            } else {
                SDL_snprintf(err, errSize, "sound/%u of %s isn't loaded%s", index, packName,
                             m.audio ? " (out of sound memory)" : " (HWVX_VIEWER_AUDIO=0)");
            }
            return 0;
        }
        *addr = sfx->data;
        *length = sfx->length;
        *loop = sfx->loop;
        *rate = sfx->rate;
        *bits = sfx->bits;
        *soundId = GETP(dp->soundIDs)[index];
        SDL_snprintf(what, whatSize, "sound/%u", index);
    }
    *dpOut = dp;
    return 1;
}

static int CmdSound(JsonWriter *w, const JsonNode *req, char *err, size_t errSize) {
    DataPack *dp;
    char packName[200], what[40], id[260];
    u32 addr, length, loop, rate, soundId, i;
    u8 bits;
    const MidiInst *inst;
    const u8 *src;
    int per = 0;
    s16 *out;

    if (!SampleArg(req, &dp, packName, sizeof(packName), what, sizeof(what), &addr, &length, &loop, &rate, &bits,
                   &soundId, &inst, err, errSize)) {
        return 0;
    }
    if ((src = Samples(addr, length, bits, &per)) == NULL) {
        SDL_snprintf(err, errSize, "%s: %u-bit samples the port's mixer doesn't play", what, bits);
        return 0;
    }
    if ((out = (s16 *)BlobBegin(length * 2, err, errSize)) == NULL) {
        return 0;
    }
    for (i = 0; i < length; i++) {
        if (per == 2) {
            memcpy(&out[i], src + i * 2, 2);    /* host order, as FetchSample reads it */
        } else {
            out[i] = (s16)((s8)src[i] * 256);
        }
    }
    SDL_snprintf(id, sizeof(id), "base://%s/%s", packName, what);
    Jw_String(w, "id", id);
    Jw_String(w, "pack", packName);
    Jw_String(w, "format", StoredFormat(dp, bits));
    WriteSample(w, length, loop, rate, bits);
    Jw_String(w, "pcm", "s16");     /* mono, host order (little-endian) */
    BlobEnd(w, length * 2);
    return 1;
}

/* ---- playing -------------------------------------------------------------------- */

static void Remember(u32 voice) {
    int i;

    for (i = 0; i < MAX_PLAYING; i++) {
        if (m.voices[i] == 0 || VOICE_ID(&g_voice[(u8)m.voices[i]]) != m.voices[i] ||
            g_voice[(u8)m.voices[i]].state == VOICE_STATE_FREE) {
            m.voices[i] = voice;
            return;
        }
    }
    ReleaseVoice(m.voices[0]);
    memmove(m.voices, m.voices + 1, sizeof(m.voices) - sizeof(m.voices[0]));
    m.voices[MAX_PLAYING - 1] = voice;
}

/* Voices the viewer started that still play. */
static int Playing(void) {
    int i, n = 0;

    for (i = 0; i < MAX_PLAYING; i++) {
        u32 v = m.voices[i];

        if (v != 0 && VOICE_ID(&g_voice[(u8)v]) == v && g_voice[(u8)v].state == VOICE_STATE_PLAYING) {
            n++;
        }
    }
    return n;
}

static int StopVoices(void) {
    int i, n = 0;

    for (i = 0; i < MAX_PLAYING; i++) {
        if (m.voices[i] != 0 && VOICE_ID(&g_voice[(u8)m.voices[i]]) == m.voices[i] &&
            g_voice[(u8)m.voices[i]].state != VOICE_STATE_FREE) {
            ReleaseVoice(m.voices[i]);
            n++;
        }
        m.voices[i] = 0;
    }
    return n;
}

static int StopSong(void) {
    if (g_pHdr != NULL && g_musicState == 1) {
        SetMusicState(3);
        return 1;
    }
    return 0;
}

static int CmdPlay(JsonWriter *w, const JsonNode *req, char *err, size_t errSize) {
    const char *node = Json_GetString(req, "node", NULL);
    DataPack *dp;
    char packName[200], what[40];
    u32 addr, length, loop, rate, soundId, voice = 0;
    u8 bits;
    const MidiInst *inst;

    if (!m.audio) {
        SDL_snprintf(err, errSize, "the viewer runs without sounds (HWVX_VIEWER_AUDIO=0)");
        return 0;
    }
    if (node != NULL && (SDL_strcmp(node, "song") == 0 || (SDL_strlen(node) > 5 && SDL_strcmp(node + SDL_strlen(node) - 5, "/song") == 0))) {
        VPack *vp;

        if (SDL_strcmp(node, "song") == 0) {
            if ((dp = PackArg(req, &vp, packName, sizeof(packName), err, errSize)) == NULL) {
                return 0;
            }
        } else {
            /* base://ARCHIVE/PACK/song */
            SDL_strlcpy(packName, SDL_strncmp(node, "base://", 7) == 0 ? node + 7 : node, sizeof(packName));
            packName[SDL_strlen(packName) >= 5 ? SDL_strlen(packName) - 5 : 0] = '\0';
            if ((dp = Viewer_FindDataPack(packName, &vp)) == NULL) {
                SDL_snprintf(err, errSize, "pack %s is not loaded", packName);
                return 0;
            }
        }
        if (!dp->songLoaded || g_pHdr == NULL) {
            SDL_snprintf(err, errSize, "%s has no song loaded (only the last pack loaded with a song keeps one)", packName);
            return 0;
        }
        StopVoices();
        SetMusicState(3);   /* from the start */
        SetMusicState(1);
        SetMusicVol(0x7FFF);
        m.songPlaying = 1;
        SDL_snprintf(m.playing, sizeof(m.playing), "base://%s/song", packName);
        Jw_String(w, "playing", m.playing);
        Jw_Bool(w, "device", Port_AudioDevice());
        WriteSongState(w);
        return 1;
    }
    if (!SampleArg(req, &dp, packName, sizeof(packName), what, sizeof(what), &addr, &length, &loop, &rate, &bits,
                   &soundId, &inst, err, errSize)) {
        return 0;
    }
    if (!Json_GetBool(req, "mix", 0)) {
        StopVoices();
        StopSong();
    }
    if (soundId != 0) {
        /* as the game plays a sample (SoundAudioSample_Play) */
        voice = GetVoice(soundId, 10);
    } else {
        /* an instrument: a voice as ProcessMIDI's note on sets one up, at the
         * sample's own rate (or at `note`: GetNoteRate_ of the bank's rate) */
        Voice *v = GetVoice_(VOICE_KIND_SFX, 20);
        long long note = Json_GetInt(req, "note", -1);

        if (v != NULL) {
            v->addr = inst->addr;
            v->length = inst->length;
            v->loop = inst->loop;
            v->rate = note >= 0 && note < 128 ? GetNoteRate_(inst->rate, (u8)note) : inst->rate;
            v->pitch = 0x10000;
            v->bits = inst->bits;
            v->vol = 0x7FFF;
            v->mixVol = 0x7FFF;
            v->auxVol = 0x7FFF;
            v->pan = 0;
            v->span = -0x7FFF;
            v->owner = 0xFC;
            v->serial++;
            voice = VOICE_ID(v);
        }
    }
    if (voice == 0) {
        SDL_snprintf(err, errSize, "no voice free to play %s", what);
        return 0;
    }
    SetVoiceState(voice, 1);
    Remember(voice);
    SDL_snprintf(m.playing, sizeof(m.playing), "base://%s/%s", packName, what);
    Jw_String(w, "playing", m.playing);
    Jw_Int(w, "voice", voice);
    Jw_Bool(w, "looping", loop != 0xFFFFFFFFu && loop < length);
    Jw_Bool(w, "device", Port_AudioDevice());
    return 1;
}

void ViewerMedia_WriteStatus(JsonWriter *w) {
    Jw_BeginObject(w, "audio", 1);
    Jw_Bool(w, "on", m.audio);
    Jw_Bool(w, "device", Port_AudioDevice());
    Jw_Int(w, "voices", Playing());
    Jw_Bool(w, "song", g_pHdr != NULL && g_musicState == 1);
    if (g_pHdr != NULL) {
        Jw_Int(w, "tick", g_song.tick);
    }
    if (m.playing[0]) {
        Jw_String(w, "last", m.playing);
    }
    Jw_EndObject(w);
}

void ViewerMedia_PackGoing(DataPack *pack) {
    if (pack != NULL && pack->songLoaded) {
        StopVoices();   /* (an instrument voice reads the song's samples) */
    }
}

void ViewerMedia_BeforeLoad(int hasSong) {
    int i;

    if (!hasSong || !m.audio || g_pHdr == NULL) {
        return;
    }
    /* the MIDI player holds one song: the one held goes (CreateMIDI would drop it unfreed) */
    StopVoices();
    DestroyMusic();
    for (i = 0; i < VIEWER_MAX_PACKS; i++) {
        if (g_viewerPacks[i].used) {
            g_viewerPacks[i].pack->songLoaded = 0;
        }
    }
}

int ViewerMedia_Dispatch(const char *cmd, const JsonNode *req, JsonWriter *w, int *ok, char *err, size_t errSize) {
    if (SDL_strcmp(cmd, "textures") == 0) {
        *ok = CmdTextures(w, req, err, errSize);
    } else if (SDL_strcmp(cmd, "texture") == 0) {
        *ok = CmdTexture(w, req, err, errSize);
    } else if (SDL_strcmp(cmd, "sounds") == 0) {
        *ok = CmdSounds(w, req, err, errSize);
    } else if (SDL_strcmp(cmd, "sound") == 0) {
        *ok = CmdSound(w, req, err, errSize);
    } else if (SDL_strcmp(cmd, "play") == 0) {
        *ok = CmdPlay(w, req, err, errSize);
    } else if (SDL_strcmp(cmd, "stop") == 0) {
        int voices = StopVoices(), song = StopSong();

        Jw_Int(w, "voices", voices);
        Jw_Bool(w, "song", song);
        *ok = 1;
    } else {
        return 0;
    }
    return 1;
}

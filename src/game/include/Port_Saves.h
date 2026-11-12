/* The port's own save format (HWVX_SAVES=json): JSONC files keyed by id,
 * written atomically with rotating backups, one folder per profile.
 * docs/xdl-format/README.md §11.
 *
 *   Port_SaveIds.c     index <-> id, the one table the save format maps through
 *   Port_SaveFormat.c  the game's SaveFile <-> JSON; the releases' binary saves
 *   Port_Saves.c       the store: files, hashes, backups, profiles, migration
 *   Port_SaveImport.c  the releases' save containers, merged into the store
 *
 * The game keeps its own flow: FileManager_NGC.c's memory card calls
 * (MemCard_Open/Read/Write, File_Delete) come here instead of card.c when the
 * switch is on, a whole SaveFile at a time. */
#ifndef PORT_SAVES_H
#define PORT_SAVES_H

#include "port.h"

struct JsonNode;
struct JsonWriter;

/* The save files the game writes: one per game type (MCP.gameType). */
typedef struct PortSaveKind {
    const char *fileName;   /* as File_Open names it (lower case) */
    const char *game;       /* "game" in the JSON */
    u32 dataSize;           /* g_gameInfo[].gameDataSize: one slot's game data */
} PortSaveKind;

const PortSaveKind *Port_SaveKindByName(const char *fileName);
const PortSaveKind *Port_SaveKindByGame(const char *game);
/* The whole SaveFile's size, SAVE_FILE_SIZE(dataSize). */
u32 Port_SaveFileSize(const PortSaveKind *kind);

/* ---- Port_SaveIds.c ---------------------------------------------------- */

/* The game's unlock arrays that the save keys by id. */
enum {
    PORT_SAVE_IDS_CARS,     /* HotWheelsGameData.cars, code variable 0x64 */
    PORT_SAVE_IDS_TRACKS,   /* .tracks, 0x65 */
    PORT_SAVE_IDS_ITEMS,    /* .items, 0x66 */
    PORT_SAVE_IDS_COUNT
};
const char *Port_SaveIdSection(int table);   /* its name in the JSON: "cars" */
u32 Port_SaveIdCount(int table);             /* the game array's length (the retail entries) */
/* The id of index `index` in `table`: 1 if it has one. */
int Port_SaveIdOf(int table, u32 index, char *buf, size_t size);
/* The index of `id` in `table`: 1 if the game has it. */
int Port_SaveIndexOf(int table, const char *id, u32 *index);

/* ---- Port_SaveFormat.c ------------------------------------------------- */

#define PORT_SAVE_FORMAT_VERSION 1

/* Byte order of a binary save: the port's own .sav files are the host's;
 * the GameCube's big-endian; the PC's and PS2's little-endian. */
enum { PORT_SAVE_ORDER_HOST, PORT_SAVE_ORDER_BIG, PORT_SAVE_ORDER_LITTLE };

/* What a save holds besides the game's SaveFile: per slot, the entries the
 * game has no index for (mod content not installed, a later release's), kept
 * so a save written back still has them. An object {section: {id: value}}
 * or NULL. */
typedef struct PortSaveExtras {
    struct JsonNode *kept[8];
    char importedFrom[256];   /* where the save first came from, "" if made here */
} PortSaveExtras;

void Port_SaveExtrasFree(PortSaveExtras *x);

/* SaveFile (host order, Port_SaveFileSize bytes) -> JSON text, without the
 * hash line Port_Saves.c adds. 1 on success. */
int Port_SaveToJson(const void *saveFile, const PortSaveKind *kind, const char *profile,
                    const PortSaveExtras *extras, struct JsonWriter *w);
/* JSON -> SaveFile (magic, version and checksum filled in, so the game's own
 * check passes) and the extras. 1 on success, else a reason in err. */
int Port_SaveFromJson(const struct JsonNode *root, const PortSaveKind *kind, void *saveFile,
                      PortSaveExtras *extras, char *err, size_t errSize);
/* A release's binary SaveFile at `bytes` (magic, version, checksum, settings,
 * 8 slots) -> host order, checked: magic, version (the size that follows) and
 * checksum. 1 on success. The GameCube's layout only: the PC's longer and
 * the PS2's packed settings are moved into it first (Port_SaveImport.c). */
int Port_SaveFromBinary(const void *bytes, size_t len, int order, const PortSaveKind *kind,
                        void *saveFile, char *err, size_t errSize);
/* The game's checksum of a host-order SaveFile (FileManager_GetChecksum). */
u32 Port_SaveChecksum(const void *saveFile, const PortSaveKind *kind);

/* ---- Port_Saves.c ------------------------------------------------------ */

/* HWVX_SAVES=json: the game's saves go to the JSON store. Off by default. */
int Port_SavesJson(void);
/* The profile in use ("default") and its folder, <pref>/saves/<profile>/. */
const char *Port_SavesProfile(void);
char *Port_SavesDir(void);   /* SDL_free it */

/* The game's file calls, by the name File_Open was given. */
int Port_SavesExists(const char *name);
/* Fills `saveFile` (size bytes) from the newest good file: the current one,
 * else a backup (logged). 0, and zeroes, if there is none: the game then sees
 * a damaged file, as it would a bad card, and nothing is reset or replaced. */
int Port_SavesRead(const char *name, void *saveFile, u32 size);
int Port_SavesWrite(const char *name, const void *saveFile, u32 size);
/* The entries save `slot` keeps by id (PortSaveExtras.kept) as the save
 * last read has them: those past the game's arrays (Port_Content.h) and
 * those it has no number for. NULL if none. */
const struct JsonNode *Port_SavesKept(const char *name, int slot);
/* The game's Delete: the save and its backups move to deleted/. */
int Port_SavesDelete(const char *name);

/* For an import: the save as it stands, read without changing anything (a
 * memory card file the store would import first is read in its place). 1 a
 * save (what: where from), 0 none yet (zeroes), -1 one that can't be read
 * (damaged with no good backup): an import must not replace it. */
int Port_SavesPeek(const char *name, void *saveFile, u32 size, char *what, size_t whatSize);
/* Port_SavesWrite, and the save says where it came from if it doesn't yet;
 * the save before it is kept as <name>.before-import.json (until the next
 * import). */
int Port_SavesWriteImported(const char *name, const void *saveFile, u32 size, const char *importedFrom);

/* Tests (tools/savetest.c): stop a write after its temporary file is
 * written, as a crash there would; drop what the store remembers. */
void Port_SavesTestFailAfterTemp(int on);
void Port_SavesForget(void);
#define PORT_SAVES_BACKUPS 5

/* ---- Port_SaveImport.c ------------------------------------------------- */

/* The releases, by whose rules a save's numbers are read. */
enum { PORT_RELEASE_GC, PORT_RELEASE_PC, PORT_RELEASE_PS2, PORT_RELEASE_COUNT };

/* The files a save comes in. */
enum {
    PORT_CONTAINER_PORT_SAV,    /* the port's own memcardA/hwvx.sav (GameCube card file, host order) */
    PORT_CONTAINER_GCI,         /* Dolphin's .gci export of one GameCube card file */
    PORT_CONTAINER_GC_CARD,     /* a raw GameCube memory card image (Dolphin's MemoryCardA.*.raw) */
    PORT_CONTAINER_PC_FILE,     /* the PC's My Documents\VelocityX Saved Games\velocityX */
    PORT_CONTAINER_PS2_FILE,    /* the PS2's save file on its own (BASLUS-20412/BASLUS-20412) */
    PORT_CONTAINER_PS2_CARD,    /* a PS2 memory card image (PCSX2's .ps2, with or without ECC) */
    PORT_CONTAINER_PSU,         /* a PS2 save folder exported as .psu (uLaunchELF) */
    PORT_CONTAINER_COUNT
};

/* The PC's 0x38 bytes of settings more than the GameCube's: each player's
 * controller settings are 0x20 bytes, not 0x12 (docs/pc-version/README.md
 * "Save files"): the first 0x12 are the GameCube's ControllerSettings, then
 * these. Settings, not progress: read, not imported. */
typedef struct PortPCControls {
    u8 buttons[13];     /* +0x12: the gamepad's button map (10 11 9 7 8 6 4 5 3 2 12 13 1 by default) */
    u8 pad;             /* +0x1F */
} PortPCControls;

/* One file to import: a release's save, read into the game's SaveFile. */
typedef struct PortImportSource {
    char path[1024];
    int container, release;
    char where[96];         /* inside the container: "GHWE78 hwvx", "/BASLUS-20412/BASLUS-20412" */
    u8 *saveFile;           /* Hot Wheels SaveFile, host order, GameCube layout (checksum checked) */
    int ps2Settings0;       /* the PS2's settings byte 0 (GameCube: difficulty, s32; PS2 u8, 1 by default) */
    PortPCControls pc[4];
} PortImportSource;

const char *Port_SaveReleaseName(int release);       /* "GameCube" */
const char *Port_SaveContainerName(int container);   /* "Dolphin .gci export" */
/* Which container `bytes` is and the save in it. 1, or 0 and why in err
 * (a damaged card, a file that isn't one, a save of another game). */
int Port_SaveImportRead(const void *bytes, size_t len, const char *path, PortImportSource *src,
                        char *err, size_t errSize);
int Port_SaveImportOpen(const char *path, PortImportSource *src, char *err, size_t errSize);
void Port_SaveImportClose(PortImportSource *src);
/* The name of save `slot` (0-7) of a file read: 1 if the slot holds a save. */
int Port_SaveImportSlotName(const PortImportSource *src, int slot, char *name, size_t size);

/* How the saves are merged. */
typedef struct PortImportOptions {
    int onlySource, onlySlot;   /* -1, -1: every save of every file; else just that one */
    int defaultCars;            /* 1: also take cars 0-2 from a PC or PS2 save, which has them from
                                 * the start (a GameCube player gets them from the GBA link) */
} PortImportOptions;

#define PORT_IMPORT_MAX_SOURCES 8
/* What one imported save does. Bits are indexes. */
typedef struct PortImportSlot {
    int source, from;           /* the file (index) and its slot */
    int into;                   /* the slot of the save it goes to; -1: no room (8 saves already) */
    int isNew;                  /* 1: a new save; 0: merged with the save of the same name */
    char name[12], intoName[12];
    u64 added[PORT_SAVE_IDS_COUNT];   /* cars, tracks, items unlocked by it */
    u64 defaultCars;            /* PC/PS2 default cars (0-2) the save hasn't: added with defaultCars */
    u32 extras, cheats;         /* extras and cheats it adds */
    int raised;                 /* values made higher that were already unlocked */
    int scriptValues;           /* 1: the script values are this save's (it is further on) */
    int changes;                /* everything above, counted */
} PortImportSlot;

typedef struct PortImportPlan {
    char target[160];           /* what is merged into ("saves/default/hwvx.json", "no save yet") */
    int numSlots;
    PortImportSlot slots[PORT_IMPORT_MAX_SOURCES * 8];
    int changes;                /* in all */
    u64 defaultCarsOffered;     /* the choice to show: PC/PS2 default cars that would be new */
} PortImportPlan;

/* The merge itself, on a SaveFile (host order) in memory: `target` (zeroes
 * for none) gains the sources' progress. Never fails; the plan says what
 * changed. */
void Port_SaveImportMerge(u8 *target, const PortImportSource *srcs, int n, const PortImportOptions *opt,
                          PortImportPlan *plan);
/* What an import would do to the store's save, changing nothing. 1, or 0
 * (err: the save can't be read, so it can't be merged into). */
int Port_SaveImportPreview(const PortImportSource *srcs, int n, const PortImportOptions *opt, PortImportPlan *plan,
                           char *err, size_t errSize);
/* The import: the merge written to the store (atomically, the save before
 * it kept as hwvx.before-import.json). The source files are only read. */
int Port_SaveImportApply(const PortImportSource *srcs, int n, const PortImportOptions *opt, PortImportPlan *plan,
                         char *err, size_t errSize);
/* One line for a slot of the plan: "HW VEL X 1" -> new save: 3 cars (3, 4, 5), ..." */
void Port_SaveImportDescribe(const PortImportPlan *plan, int i, const PortImportOptions *opt, char *buf, size_t size);

#endif

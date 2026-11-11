/* The port's own save format (HWVX_SAVES=json): JSONC files keyed by id,
 * written atomically with rotating backups, one folder per profile.
 * docs/xdl-format/README.md §11.
 *
 *   Port_SaveIds.c     index <-> id, the one table the save format maps through
 *   Port_SaveFormat.c  the game's SaveFile <-> JSON; the releases' binary saves
 *   Port_Saves.c       the store: files, hashes, backups, profiles, migration
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
u32 Port_SaveIdCount(int table);             /* the game array's length */
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
 * checksum. 1 on success. The PC's longer settings block is not read yet
 * (task 4d): it fails the version check. */
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
/* The game's Delete: the save and its backups move to deleted/. */
int Port_SavesDelete(const char *name);

/* Tests (tools/savetest.c): stop a write after its temporary file is
 * written, as a crash there would; drop what the store remembers. */
void Port_SavesTestFailAfterTemp(int on);
void Port_SavesForget(void);
#define PORT_SAVES_BACKUPS 5

#endif

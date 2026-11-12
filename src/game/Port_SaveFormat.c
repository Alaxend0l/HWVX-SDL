/* The port's save format: the game's SaveFile <-> JSONC, and the releases'
 * binary SaveFiles read into the game's structs. Port-only file.
 * docs/xdl-format/README.md §11; the binary layout §11.3.
 *
 * A save, format_version 1:
 *
 *   {
 *     "format_version": 1,
 *     "game": "hwvx",                      (or "ddrescue")
 *     "profile": "default",
 *     "imported_from": "memcardA/hwvx.sav", (only for an imported save)
 *     "settings": { "difficulty", "sfx_volume", "music_volume", "speaker_mode",
 *                   "handling" (only when not 0: 2 is the PC's third handling mode),
 *                   "controllers": [4 x hex of ControllerSettings] },
 *     "saves": [ {                          (the file's 8 slots, empty ones left out)
 *       "slot": 0, "valid": true, "name": "HW VEL X 1", "date": [y, m, d, h, min],
 *       "cars":   { "base://HW_Cars/car_0": {"unlocked": true}, ... },
 *       "tracks": { "base://mcp/track/0": {"unlocked": true}, ... },
 *       "items":  { "base://mcp/item/0": {"unlocked": true}, ... },
 *       "cheats": [11], "extras": [5], "script_values": [30],
 *       "vehicle": "base://HW_Cars/car_3", "item": "base://mcp/item/0"
 *     } ]
 *   }
 *
 * Ids come from Port_SaveIds.c. An entry past the game's arrays (a 34th
 * car: the content registry's, Port_Content.h) is kept by id per slot
 * (PortSaveExtras), as is an id the game has no index for at all, which is
 * written back marked "inactive": true. DD Rescue's
 * slots hold its world state as named records (ddr_* below). Bytes no code
 * reads (struct padding) are written only when they aren't zero, so a save
 * goes through JSON and back unchanged. */
#include <SDL3/SDL.h>
#include "common.h"
#include "Port_Saves.h"
#include "jsonc.h"

static const PortSaveKind s_kinds[] = {
    { "hwvx", "hwvx", sizeof(HotWheelsGameData) },
    { "doomsdayrescue", "ddrescue", sizeof(DDRescueGameData) },
};

const PortSaveKind *Port_SaveKindByName(const char *fileName) {
    size_t i;
    for (i = 0; i < SDL_arraysize(s_kinds); i++) {
        if (SDL_strcasecmp(s_kinds[i].fileName, fileName) == 0) {
            return &s_kinds[i];
        }
    }
    return NULL;
}

const PortSaveKind *Port_SaveKindByGame(const char *game) {
    size_t i;
    for (i = 0; game != NULL && i < SDL_arraysize(s_kinds); i++) {
        if (SDL_strcmp(s_kinds[i].game, game) == 0) {
            return &s_kinds[i];
        }
    }
    return NULL;
}

u32 Port_SaveFileSize(const PortSaveKind *kind) {
    return (u32)SAVE_FILE_SIZE(kind->dataSize);
}

static int IsHotWheels(const PortSaveKind *kind) {
    return kind->dataSize == sizeof(HotWheelsGameData);
}

/* SaveFile.version: the size of what follows the header. */
static u32 SaveVersion(const PortSaveKind *kind) {
    return (u32)(sizeof(GeneralGameSettings) + kind->dataSize * SAVE_FILE_SLOTS);
}

u32 Port_SaveChecksum(const void *saveFile, const PortSaveKind *kind) {
    const u8 *p = (const u8 *)saveFile + offsetof(SaveFile, settings);
    u32 i, n = SaveVersion(kind), sum = 0;

    for (i = 0; i < n; i++) {
        sum += p[i] + 1u;
    }
    return sum;
}

void Port_SaveExtrasFree(PortSaveExtras *x) {
    int i;
    for (i = 0; i < SAVE_FILE_SLOTS; i++) {
        Json_Free(x->kept[i]);
        x->kept[i] = NULL;
    }
    x->importedFrom[0] = 0;
}

static int AllZero(const void *p, size_t n) {
    const u8 *b = p;
    while (n-- > 0) {
        if (*b++ != 0) {
            return 0;
        }
    }
    return 1;
}

/* ---- writing ----------------------------------------------------------- */

static void WriteIntArray8(JsonWriter *w, const char *key, const s8 *a, int n) {
    int i;
    Jw_BeginArray(w, key, 1);
    for (i = 0; i < n; i++) {
        Jw_Int(w, NULL, a[i]);
    }
    Jw_EndArray(w);
}

static void WriteU16Array(JsonWriter *w, const char *key, const u16 *a, int n) {
    int i;
    Jw_BeginArray(w, key, 1);
    for (i = 0; i < n; i++) {
        Jw_Int(w, NULL, a[i]);
    }
    Jw_EndArray(w);
}

static void WriteUnlocks(JsonWriter *w, int table, const s8 *a, const JsonNode *kept) {
    const char *section = Port_SaveIdSection(table);
    const JsonNode *more = Json_Get(kept, section), *m, *c;
    u32 i, n = Port_SaveIdCount(table);
    char id[128];

    Jw_BeginObject(w, section, 0);
    for (i = 0; i < n; i++) {
        if (!Port_SaveIdOf(table, i, id, sizeof(id))) {
            continue;
        }
        Jw_BeginObject(w, id, 1);
        Jw_Bool(w, "unlocked", a[i] != 0);
        if (a[i] != 0 && a[i] != 1) {
            Jw_Int(w, "value", a[i]);
        }
        Jw_EndObject(w);
    }
    for (m = more != NULL ? more->child : NULL; m != NULL; m = m->next) {
        u32 index;
        int known = Port_SaveIndexOf(table, m->key, &index);   /* past the array: the registry's */

        Jw_BeginObject(w, m->key, 1);
        if (m->type == JSON_OBJECT) {
            for (c = m->child; c != NULL; c = c->next) {
                if (SDL_strcmp(c->key, "inactive") != 0) {
                    Jw_Node(w, c->key, c);
                }
            }
        } else {
            Jw_Node(w, "unlocked", m);
        }
        if (!known) {
            Jw_Bool(w, "inactive", 1);
        }
        Jw_EndObject(w);
    }
    Jw_EndObject(w);
}

/* A number the game keeps for one of the id tables (vehicle, item): the id,
 * or the number itself when it has none (-1: none chosen). */
static void WriteIdRef(JsonWriter *w, const char *key, int table, s32 v, const JsonNode *kept) {
    char id[128];
    const JsonNode *k = Json_Get(Json_Get(kept, "refs"), key);

    if (v == -1 && k != NULL && k->type == JSON_STRING) {
        Jw_String(w, key, k->string);   /* an id the game hadn't, still not chosen again */
    } else if (v >= 0 && Port_SaveIdOf(table, (u32)v, id, sizeof(id))) {
        Jw_String(w, key, id);
    } else {
        Jw_Int(w, key, v);
    }
}

static void WriteSlotInfo(JsonWriter *w, int slot, const SaveSlotInfo *info) {
    char name[sizeof(info->name) + 1];

    SDL_memcpy(name, info->name, sizeof(info->name));
    name[sizeof(info->name)] = 0;
    Jw_Int(w, "slot", slot);
    Jw_Bool(w, "valid", info->valid != 0);
    if (info->valid > 1) {
        Jw_Int(w, "valid_value", info->valid);
    }
    Jw_String(w, "name", name);
    if (SDL_strlen(name) + 1 < sizeof(info->name) && !AllZero(info->name + SDL_strlen(name) + 1, sizeof(info->name) - SDL_strlen(name) - 1)) {
        Jw_Hex(w, "name_bytes", info->name, sizeof(info->name));
    }
    Jw_BeginArray(w, "date", 1);
    Jw_Int(w, NULL, info->year);
    Jw_Int(w, NULL, info->month);
    Jw_Int(w, NULL, info->day);
    Jw_Int(w, NULL, info->hour);
    Jw_Int(w, NULL, info->minute);
    Jw_EndArray(w);
}

static void WriteHotWheels(JsonWriter *w, const HotWheelsGameData *g, const JsonNode *kept) {
    WriteUnlocks(w, PORT_SAVE_IDS_CARS, g->cars, kept);
    WriteUnlocks(w, PORT_SAVE_IDS_TRACKS, g->tracks, kept);
    WriteUnlocks(w, PORT_SAVE_IDS_ITEMS, g->items, kept);
    WriteIntArray8(w, "cheats", g->cheats, sizeof(g->cheats));
    WriteIntArray8(w, "extras", g->extras, sizeof(g->extras));
    WriteU16Array(w, "script_values", g->miscValues, SDL_arraysize(g->miscValues));
    WriteIdRef(w, "vehicle", PORT_SAVE_IDS_CARS, g->vehicle, kept);
    WriteIdRef(w, "item", PORT_SAVE_IDS_ITEMS, g->item, kept);
    if (!AllZero(g->pad5A, sizeof(g->pad5A))) {
        Jw_Hex(w, "unused_5a", g->pad5A, sizeof(g->pad5A));
    }
    if (g->padAF != 0) {
        Jw_Int(w, "unused_af", g->padAF);
    }
}

static void WriteFloatArray(JsonWriter *w, const char *key, const f32 *a, int n) {
    int i;
    Jw_BeginArray(w, key, 1);
    for (i = 0; i < n; i++) {
        Jw_Float(w, NULL, a[i]);
    }
    Jw_EndArray(w);
}

/* DD Rescue's bytes no code reads: pad6A, pad75, the hole the compiler
 * leaves after carMode (0x197E), pad1CD2. To `pads`, or back from it. */
#define DDR_PADS 9
static void DDRescuePads(DDRescueGameData *g, u8 *pads, int back) {
    static const struct { u32 offset, size; } s_pads[] = {
        { offsetof(DDRescueGameData, pad6A), 2 },
        { offsetof(DDRescueGameData, pad75), 3 },
        { offsetof(DDRescueGameData, carMode) + DDR_MAX_CARS, 2 },
        { offsetof(DDRescueGameData, pad1CD2), 2 },
    };
    size_t i, n = 0;

    for (i = 0; i < SDL_arraysize(s_pads); i++) {
        u8 *p = (u8 *)g + s_pads[i].offset;
        if (back) {
            SDL_memcpy(p, pads + n, s_pads[i].size);
        } else {
            SDL_memcpy(pads + n, p, s_pads[i].size);
        }
        n += s_pads[i].size;
    }
}
_Static_assert(offsetof(DDRescueGameData, carPos) == offsetof(DDRescueGameData, carMode) + DDR_MAX_CARS + 2,
               "DDRescueGameData: the hole after carMode");

/* DD Rescue: its unlocks, then the world it was in as named records
 * (GameSettings_GatherGameData). */
static void WriteDDRescue(JsonWriter *w, const DDRescueGameData *g) {
    int i;

    WriteIntArray8(w, "cars", g->cars, sizeof(g->cars));
    WriteIntArray8(w, "missions", g->missions, sizeof(g->missions));
    WriteIntArray8(w, "cheats", g->cheats, sizeof(g->cheats));
    WriteIntArray8(w, "extras", g->extras, sizeof(g->extras));
    WriteU16Array(w, "script_values", g->miscValues, SDL_arraysize(g->miscValues));
    Jw_Int(w, "progression_id", g->progressionID);
    Jw_Int(w, "progression", g->progression);
    Jw_Int(w, "vehicle", g->vehicle);
    Jw_Int(w, "item", g->item);
    {
        u8 pads[DDR_PADS];
        DDRescuePads((DDRescueGameData *)g, pads, 0);
        if (!AllZero(pads, sizeof(pads))) {
            Jw_Hex(w, "unused", pads, sizeof(pads));
        }
    }
    Jw_BeginObject(w, "world", 0);
    Jw_Bool(w, "valid", g->bValid != 0);
    if ((u8)g->bValid > 1) {
        Jw_Int(w, "valid_value", (u8)g->bValid);
    }
    Jw_Hex(w, "player_stats", g->playerStats, sizeof(g->playerStats));
    {
        const s32 *a = (const s32 *)(const void *)g->actions;
        Jw_BeginArray(w, "action_counts", 1);
        for (i = 0; i < 500; i++) {
            Jw_Int(w, NULL, a[i]);
        }
        Jw_EndArray(w);
    }
    Jw_BeginArray(w, "timers", 0);
    for (i = 0; i < DDR_MAX_TIMERS; i++) {
        Jw_BeginObject(w, NULL, 1);
        Jw_Float(w, "time", g->timerTime[i]);
        Jw_BeginArray(w, "mode", 1);
        Jw_Int(w, NULL, g->timerMode[i][0]);
        Jw_Int(w, NULL, g->timerMode[i][1]);
        Jw_EndArray(w);
        Jw_EndObject(w);
    }
    Jw_EndArray(w);
    Jw_BeginArray(w, "variables", 0);
    for (i = 0; i < DDR_MAX_VARIABLES; i++) {
        Jw_BeginObject(w, NULL, 1);
        Jw_Float(w, "value", g->variableValue[i]);
        Jw_BeginArray(w, "mode", 1);
        Jw_Int(w, NULL, g->variableMode[i][0]);
        Jw_Int(w, NULL, g->variableMode[i][1]);
        Jw_EndArray(w);
        Jw_EndObject(w);
    }
    Jw_EndArray(w);
    Jw_Hex(w, "creature_modes", g->creatureMode, sizeof(g->creatureMode));
    Jw_Hex(w, "item_modes", g->itemMode, sizeof(g->itemMode));
    Jw_Hex(w, "instance_modes", g->instanceMode, sizeof(g->instanceMode));
    Jw_Hex(w, "path_enabled", g->pathEnabled, sizeof(g->pathEnabled));
    Jw_BeginArray(w, "cars", 0);
    for (i = 0; i < DDR_MAX_CARS; i++) {
        Jw_BeginObject(w, NULL, 1);
        Jw_Int(w, "mode", g->carMode[i]);
        WriteFloatArray(w, "pos", g->carPos[i], 3);
        Jw_Float(w, "health", g->carHealth[i]);
        Jw_Int(w, "spawned", g->carSpawned[i]);
        Jw_EndObject(w);
    }
    Jw_EndArray(w);
    Jw_EndObject(w);
}

int Port_SaveToJson(const void *saveFile, const PortSaveKind *kind, const char *profile,
                    const PortSaveExtras *extras, JsonWriter *w) {
    const SaveFile *file = saveFile;
    const GeneralGameSettings *s = &file->settings;
    int i;

    Jw_Comment(w, "Hot Wheels: Velocity X save (HWVX-SDL). Yours to read and edit.");
    Jw_Comment(w, "After editing, delete the sha256 line at the end: with it, a changed");
    Jw_Comment(w, "file reads as damaged and the newest backup is loaded instead.");
    Jw_BeginObject(w, NULL, 0);
    Jw_Int(w, "format_version", PORT_SAVE_FORMAT_VERSION);
    Jw_String(w, "game", kind->game);
    Jw_String(w, "profile", profile);
    if (extras != NULL && extras->importedFrom[0] != 0) {
        Jw_String(w, "imported_from", extras->importedFrom);
    }

    Jw_BeginObject(w, "settings", 0);
    Jw_Int(w, "difficulty", s->difficulty);
    Jw_Int(w, "sfx_volume", s->audio.sfxVolume);
    Jw_Int(w, "music_volume", s->audio.musicVolume);
    Jw_Int(w, "speaker_mode", s->audio.speakerMode);
    if (s->audio.handling != 0) {
        Jw_Int(w, "handling", s->audio.handling);
    }
    Jw_BeginArray(w, "controllers", 0);
    for (i = 0; i < 4; i++) {
        Jw_Hex(w, NULL, s->controllers[i].data, sizeof(s->controllers[i].data));
    }
    Jw_EndArray(w);
    Jw_EndObject(w);

    Jw_BeginArray(w, "saves", 0);
    for (i = 0; i < SAVE_FILE_SLOTS; i++) {
        const SaveSlotInfo *info = &s->slots[i];
        const u8 *data = SAVE_FILE_GAME_DATA(file, kind->dataSize, i);
        const JsonNode *kept = extras != NULL ? extras->kept[i] : NULL;

        if (!info->valid && AllZero(info, sizeof(*info)) && AllZero(data, kind->dataSize) && kept == NULL) {
            continue;
        }
        Jw_BeginObject(w, NULL, 0);
        WriteSlotInfo(w, i, info);
        if (IsHotWheels(kind)) {
            WriteHotWheels(w, (const HotWheelsGameData *)data, kept);
        } else {
            WriteDDRescue(w, (const DDRescueGameData *)data);
        }
        Jw_EndObject(w);
    }
    Jw_EndArray(w);
    Jw_EndObject(w);
    Jw_Raw(w, "\n");
    return !w->failed;
}

/* ---- reading ----------------------------------------------------------- */

typedef struct Reader {
    char *err;
    size_t errSize;
    int failed;
} Reader;

static void Bad(Reader *r, const char *fmt, ...) PORT_PRINTF(2, 3);
static void Bad(Reader *r, const char *fmt, ...) {
    va_list ap;
    if (r->failed) {
        return;
    }
    r->failed = 1;
    va_start(ap, fmt);
    SDL_vsnprintf(r->err, r->errSize, fmt, ap);
    va_end(ap);
}

static long long Int(Reader *r, const JsonNode *n, const char *what, long long lo, long long hi) {
    if (n == NULL || n->type != JSON_NUMBER || !n->isInteger) {
        Bad(r, "%s: not an integer", what);
        return 0;
    }
    if (n->integer < lo || n->integer > hi) {
        Bad(r, "%s: %lld out of range", what, n->integer);
        return 0;
    }
    return n->integer;
}

static long long IntMember(Reader *r, const JsonNode *obj, const char *key, long long lo, long long hi, long long fallback) {
    const JsonNode *n = Json_Get(obj, key);
    return n == NULL ? fallback : Int(r, n, key, lo, hi);
}

static f32 Float(Reader *r, const JsonNode *n, const char *what) {
    if (n != NULL && n->type == JSON_NUMBER) {
        return (f32)n->number;
    }
    if (n != NULL && n->type == JSON_STRING && SDL_strncmp(n->string, "0x", 2) == 0) {
        u32 bits = (u32)SDL_strtoul(n->string + 2, NULL, 16);
        f32 f;
        SDL_memcpy(&f, &bits, 4);
        return f;
    }
    Bad(r, "%s: not a number", what);
    return 0;
}

static void HexMember(Reader *r, const JsonNode *obj, const char *key, void *out, size_t size, int required) {
    const JsonNode *n = Json_Get(obj, key);
    if (n == NULL && !required) {
        return;
    }
    if (n == NULL || n->type != JSON_STRING || !Json_HexDecode(n->string, out, size)) {
        Bad(r, "%s: not %u bytes of hex", key, (unsigned)size);
    }
}

/* An array of up to n small integers (missing elements stay 0). */
static void ReadIntArray(Reader *r, const JsonNode *obj, const char *key, void *out, int n, int elemSize, long long lo, long long hi) {
    const JsonNode *a = Json_Get(obj, key), *e;
    int i = 0;

    if (a == NULL) {
        return;
    }
    if (a->type != JSON_ARRAY || a->count > n) {
        Bad(r, "%s: not an array of at most %d", key, n);
        return;
    }
    for (e = a->child; e != NULL; e = e->next, i++) {
        long long v = Int(r, e, key, lo, hi);
        switch (elemSize) {
        case 1: ((u8 *)out)[i] = (u8)v; break;
        case 2: ((u16 *)out)[i] = (u16)v; break;
        default: ((s32 *)out)[i] = (s32)v; break;
        }
    }
}

static void ReadFloatArray(Reader *r, const JsonNode *a, const char *what, f32 *out, int n) {
    const JsonNode *e;
    int i = 0;

    if (a == NULL || a->type != JSON_ARRAY || a->count != n) {
        Bad(r, "%s: not an array of %d", what, n);
        return;
    }
    for (e = a->child; e != NULL; e = e->next) {
        out[i++] = Float(r, e, what);
    }
}

static void Keep(JsonNode **kept, const char *section, const JsonNode *entry) {
    if (*kept == NULL) {
        *kept = Json_NewObject();
    }
    Json_Append(Json_ObjectMember(*kept, section), Json_Clone(entry), NULL);
}

static void ReadUnlocks(Reader *r, const JsonNode *slot, int table, s8 *a, JsonNode **kept) {
    const char *section = Port_SaveIdSection(table);
    const JsonNode *obj = Json_Get(slot, section), *m;
    u32 index;

    if (obj == NULL) {
        return;
    }
    if (obj->type != JSON_OBJECT) {
        Bad(r, "%s: not an object", section);
        return;
    }
    for (m = obj->child; m != NULL; m = m->next) {
        long long v;

        if (m->type == JSON_OBJECT) {
            int unlocked = Json_GetBool(m, "unlocked", 0);
            v = IntMember(r, m, "value", -128, 127, unlocked);
        } else if (m->type == JSON_BOOL) {
            v = m->boolean;
        } else {
            v = Int(r, m, m->key, -128, 127);
        }
        if (Port_SaveIndexOf(table, m->key, &index) && index < Port_SaveIdCount(table)) {
            a[index] = (s8)v;
        } else {
            /* past the game's array (the content registry has it: Port_Content.h),
             * or content the game hasn't: kept for when it comes back */
            Keep(kept, section, m);
        }
    }
}

static s32 ReadIdRef(Reader *r, const JsonNode *slot, const char *key, int table, JsonNode **kept) {
    const JsonNode *n = Json_Get(slot, key);
    u32 index;

    if (n == NULL) {
        return -1;
    }
    if (n->type == JSON_STRING) {
        if (Port_SaveIndexOf(table, n->string, &index)) {
            return (s32)index;
        }
        /* chosen content that isn't installed: none chosen, the id kept */
        if (*kept == NULL) {
            *kept = Json_NewObject();
        }
        Json_Append(Json_ObjectMember(*kept, "refs"), Json_Clone(n), key);
        return -1;
    }
    return (s32)Int(r, n, key, INT32_MIN, INT32_MAX);
}

static void ReadSlotInfo(Reader *r, const JsonNode *slot, SaveSlotInfo *info) {
    const JsonNode *date = Json_Get(slot, "date");
    const char *name = Json_GetString(slot, "name", "");
    size_t n = SDL_strlen(name);

    info->valid = (u8)IntMember(r, slot, "valid_value", 0, 255, Json_GetBool(slot, "valid", 0));
    if (n > sizeof(info->name)) {
        Bad(r, "name: longer than %u", (unsigned)sizeof(info->name));
        return;
    }
    SDL_memcpy(info->name, name, n);
    HexMember(r, slot, "name_bytes", info->name, sizeof(info->name), 0);
    if (date != NULL) {
        if (date->type != JSON_ARRAY || date->count != 5) {
            Bad(r, "date: not [year, month, day, hour, minute]");
            return;
        }
        info->year = (s32)Int(r, Json_At(date, 0), "date", INT32_MIN, INT32_MAX);
        info->month = (s32)Int(r, Json_At(date, 1), "date", INT32_MIN, INT32_MAX);
        info->day = (s32)Int(r, Json_At(date, 2), "date", INT32_MIN, INT32_MAX);
        info->hour = (u32)Int(r, Json_At(date, 3), "date", 0, UINT32_MAX);
        info->minute = (s32)Int(r, Json_At(date, 4), "date", INT32_MIN, INT32_MAX);
    }
}

static void ReadHotWheels(Reader *r, const JsonNode *slot, HotWheelsGameData *g, JsonNode **kept) {
    ReadUnlocks(r, slot, PORT_SAVE_IDS_CARS, g->cars, kept);
    ReadUnlocks(r, slot, PORT_SAVE_IDS_TRACKS, g->tracks, kept);
    ReadUnlocks(r, slot, PORT_SAVE_IDS_ITEMS, g->items, kept);
    ReadIntArray(r, slot, "cheats", g->cheats, sizeof(g->cheats), 1, -128, 255);
    ReadIntArray(r, slot, "extras", g->extras, sizeof(g->extras), 1, -128, 255);
    ReadIntArray(r, slot, "script_values", g->miscValues, SDL_arraysize(g->miscValues), 2, 0, 0xFFFF);
    g->vehicle = ReadIdRef(r, slot, "vehicle", PORT_SAVE_IDS_CARS, kept);
    g->item = ReadIdRef(r, slot, "item", PORT_SAVE_IDS_ITEMS, kept);
    HexMember(r, slot, "unused_5a", g->pad5A, sizeof(g->pad5A), 0);
    g->padAF = (u8)IntMember(r, slot, "unused_af", 0, 255, 0);
}

static void ReadDDRescue(Reader *r, const JsonNode *slot, DDRescueGameData *g) {
    const JsonNode *world = Json_Get(slot, "world"), *a, *e;
    int i;

    ReadIntArray(r, slot, "cars", g->cars, sizeof(g->cars), 1, -128, 255);
    ReadIntArray(r, slot, "missions", g->missions, sizeof(g->missions), 1, -128, 255);
    ReadIntArray(r, slot, "cheats", g->cheats, sizeof(g->cheats), 1, -128, 255);
    ReadIntArray(r, slot, "extras", g->extras, sizeof(g->extras), 1, -128, 255);
    ReadIntArray(r, slot, "script_values", g->miscValues, SDL_arraysize(g->miscValues), 2, 0, 0xFFFF);
    g->progressionID = (s32)IntMember(r, slot, "progression_id", INT32_MIN, INT32_MAX, 0);
    g->progression = (s32)IntMember(r, slot, "progression", INT32_MIN, INT32_MAX, 0);
    g->vehicle = (s32)IntMember(r, slot, "vehicle", INT32_MIN, INT32_MAX, -1);
    g->item = (s32)IntMember(r, slot, "item", INT32_MIN, INT32_MAX, -1);
    if (Json_Get(slot, "unused") != NULL) {
        u8 pads[DDR_PADS];
        HexMember(r, slot, "unused", pads, sizeof(pads), 1);
        DDRescuePads(g, pads, 1);
    }
    if (world == NULL) {
        return;
    }
    g->bValid = (s8)IntMember(r, world, "valid_value", 0, 255, Json_GetBool(world, "valid", 0));
    HexMember(r, world, "player_stats", g->playerStats, sizeof(g->playerStats), 0);
    ReadIntArray(r, world, "action_counts", g->actions, 500, 4, INT32_MIN, INT32_MAX);
    if ((a = Json_Get(world, "timers")) != NULL) {
        if (a->type != JSON_ARRAY || a->count > DDR_MAX_TIMERS) {
            Bad(r, "timers: not an array of at most %d", DDR_MAX_TIMERS);
            return;
        }
        for (i = 0, e = a->child; e != NULL; e = e->next, i++) {
            g->timerTime[i] = Float(r, Json_Get(e, "time"), "timers.time");
            ReadIntArray(r, e, "mode", g->timerMode[i], 2, 1, 0, 255);
        }
    }
    if ((a = Json_Get(world, "variables")) != NULL) {
        if (a->type != JSON_ARRAY || a->count > DDR_MAX_VARIABLES) {
            Bad(r, "variables: not an array of at most %d", DDR_MAX_VARIABLES);
            return;
        }
        for (i = 0, e = a->child; e != NULL; e = e->next, i++) {
            g->variableValue[i] = Float(r, Json_Get(e, "value"), "variables.value");
            ReadIntArray(r, e, "mode", g->variableMode[i], 2, 1, 0, 255);
        }
    }
    HexMember(r, world, "creature_modes", g->creatureMode, sizeof(g->creatureMode), 0);
    HexMember(r, world, "item_modes", g->itemMode, sizeof(g->itemMode), 0);
    HexMember(r, world, "instance_modes", g->instanceMode, sizeof(g->instanceMode), 0);
    HexMember(r, world, "path_enabled", g->pathEnabled, sizeof(g->pathEnabled), 0);
    if ((a = Json_Get(world, "cars")) != NULL) {
        if (a->type != JSON_ARRAY || a->count > DDR_MAX_CARS) {
            Bad(r, "cars: not an array of at most %d", DDR_MAX_CARS);
            return;
        }
        for (i = 0, e = a->child; e != NULL; e = e->next, i++) {
            g->carMode[i] = (u8)IntMember(r, e, "mode", 0, 255, 0);
            ReadFloatArray(r, Json_Get(e, "pos"), "cars.pos", g->carPos[i], 3);
            g->carHealth[i] = Float(r, Json_Get(e, "health"), "cars.health");
            g->carSpawned[i] = (u8)IntMember(r, e, "spawned", 0, 255, 0);
        }
    }
}

int Port_SaveFromJson(const JsonNode *root, const PortSaveKind *kind, void *saveFile,
                      PortSaveExtras *extras, char *err, size_t errSize) {
    SaveFile *file = saveFile;
    GeneralGameSettings *s = &file->settings;
    const JsonNode *settings, *saves, *slot, *c;
    Reader r = { err, errSize, 0 };
    long long version;
    unsigned seen = 0;
    int i;

    SDL_memset(saveFile, 0, Port_SaveFileSize(kind));
    SDL_zerop(extras);
    if (root == NULL || root->type != JSON_OBJECT) {
        Bad(&r, "not an object");
        return 0;
    }
    version = Json_GetInt(root, "format_version", -1);
    if (version < 1) {
        Bad(&r, "no format_version");
        return 0;
    }
    if (version > PORT_SAVE_FORMAT_VERSION) {
        /* from a newer port: never read it as something else */
        Bad(&r, "format_version %lld is newer than this build's %d", version, PORT_SAVE_FORMAT_VERSION);
        return 0;
    }
    if (SDL_strcmp(Json_GetString(root, "game", ""), kind->game) != 0) {
        Bad(&r, "game is \"%s\", not \"%s\"", Json_GetString(root, "game", ""), kind->game);
        return 0;
    }
    SDL_strlcpy(extras->importedFrom, Json_GetString(root, "imported_from", ""), sizeof(extras->importedFrom));

    settings = Json_Get(root, "settings");
    if (settings == NULL || settings->type != JSON_OBJECT) {
        Bad(&r, "no settings");
        return 0;
    }
    s->difficulty = (s32)IntMember(&r, settings, "difficulty", INT32_MIN, INT32_MAX, 0);
    s->audio.sfxVolume = (u8)IntMember(&r, settings, "sfx_volume", 0, 255, 100);
    s->audio.musicVolume = (u8)IntMember(&r, settings, "music_volume", 0, 255, 100);
    s->audio.speakerMode = (u8)IntMember(&r, settings, "speaker_mode", 0, 255, 0);
    /* the byte after the audio settings; saves before the Handling option
     * named it audio_unused */
    s->audio.handling = (u8)IntMember(&r, settings, Json_Get(settings, "handling") != NULL ? "handling" : "audio_unused",
                                      0, 255, 0);
    c = Json_Get(settings, "controllers");
    if (c != NULL) {
        if (c->type != JSON_ARRAY || c->count != 4) {
            Bad(&r, "controllers: not 4");
        }
        for (i = 0; i < 4 && !r.failed; i++) {
            const JsonNode *e = Json_At(c, i);
            if (e == NULL || e->type != JSON_STRING || !Json_HexDecode(e->string, s->controllers[i].data, sizeof(s->controllers[i].data))) {
                Bad(&r, "controllers[%d]: not %u bytes of hex", i, (unsigned)sizeof(s->controllers[i].data));
            }
        }
    }

    saves = Json_Get(root, "saves");
    if (saves == NULL || saves->type != JSON_ARRAY) {
        Bad(&r, "no saves");
    }
    for (slot = saves != NULL ? saves->child : NULL; slot != NULL && !r.failed; slot = slot->next) {
        long long n = IntMember(&r, slot, "slot", 0, SAVE_FILE_SLOTS - 1, -1);
        u8 *data;

        if (n < 0 || r.failed) {
            Bad(&r, "a save without its slot (0-%d)", SAVE_FILE_SLOTS - 1);
            break;
        }
        if (seen & (1u << n)) {
            Bad(&r, "slot %lld twice", n);
            break;
        }
        seen |= 1u << n;
        ReadSlotInfo(&r, slot, &s->slots[n]);
        data = SAVE_FILE_GAME_DATA(file, kind->dataSize, n);
        if (IsHotWheels(kind)) {
            ReadHotWheels(&r, slot, (HotWheelsGameData *)data, &extras->kept[n]);
        } else {
            ReadDDRescue(&r, slot, (DDRescueGameData *)data);
        }
        if (r.failed) {
            SDL_snprintf(err + SDL_strlen(err), errSize - SDL_strlen(err), " (slot %lld)", n);
        }
    }
    if (r.failed) {
        Port_SaveExtrasFree(extras);
        return 0;
    }
    file->magic = SAVE_FILE_MAGIC;
    file->version = SaveVersion(kind);
    file->checksum = Port_SaveChecksum(file, kind);
    return 1;
}

/* ---- the releases' binary saves ---------------------------------------- */

static void Fix32(void *p, int swap) {
    if (swap) {
        u32 v;
        SDL_memcpy(&v, p, 4);
        v = SDL_Swap32(v);
        SDL_memcpy(p, &v, 4);
    }
}

static void Fix16(void *p, int swap) {
    if (swap) {
        u16 v;
        SDL_memcpy(&v, p, 2);
        v = SDL_Swap16(v);
        SDL_memcpy(p, &v, 2);
    }
}

static void FixSaveFile(SaveFile *file, const PortSaveKind *kind, int swap) {
    int i, j;

    Fix32(&file->magic, swap);
    Fix32(&file->version, swap);
    Fix32(&file->checksum, swap);
    Fix32(&file->settings.difficulty, swap);
    for (i = 0; i < SAVE_FILE_SLOTS; i++) {
        SaveSlotInfo *info = &file->settings.slots[i];
        Fix32(&info->year, swap);
        Fix32(&info->month, swap);
        Fix32(&info->day, swap);
        Fix32(&info->hour, swap);
        Fix32(&info->minute, swap);
    }
    for (i = 0; i < SAVE_FILE_SLOTS; i++) {
        u8 *data = SAVE_FILE_GAME_DATA(file, kind->dataSize, i);
        if (IsHotWheels(kind)) {
            HotWheelsGameData *g = (HotWheelsGameData *)data;
            for (j = 0; j < (int)SDL_arraysize(g->miscValues); j++) {
                Fix16(&g->miscValues[j], swap);
            }
            Fix32(&g->vehicle, swap);
            Fix32(&g->item, swap);
        } else {
            /* playerStats (Player 0x70-0xA7) stays as bytes: its fields
             * aren't all named yet (task 4d, for a big-endian DD Rescue save) */
            DDRescueGameData *g = (DDRescueGameData *)data;
            for (j = 0; j < (int)SDL_arraysize(g->miscValues); j++) {
                Fix16(&g->miscValues[j], swap);
            }
            Fix32(&g->progressionID, swap);
            Fix32(&g->progression, swap);
            for (j = 0; j < 500; j++) {
                Fix32(g->actions + j * 4, swap);
            }
            for (j = 0; j < DDR_MAX_TIMERS; j++) {
                Fix32(&g->timerTime[j], swap);
            }
            for (j = 0; j < DDR_MAX_VARIABLES; j++) {
                Fix32(&g->variableValue[j], swap);
            }
            for (j = 0; j < DDR_MAX_CARS; j++) {
                Fix32(&g->carPos[j][0], swap);
                Fix32(&g->carPos[j][1], swap);
                Fix32(&g->carPos[j][2], swap);
                Fix32(&g->carHealth[j], swap);
            }
            Fix32(&g->vehicle, swap);
            Fix32(&g->item, swap);
        }
    }
}

int Port_SaveFromBinary(const void *bytes, size_t len, int order, const PortSaveKind *kind,
                        void *saveFile, char *err, size_t errSize) {
    SaveFile *file = saveFile;
    u32 size = Port_SaveFileSize(kind), sum;
    int swap;

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    swap = order == PORT_SAVE_ORDER_LITTLE;
#else
    swap = order == PORT_SAVE_ORDER_BIG;
#endif
    if (len < size) {
        SDL_snprintf(err, errSize, "%u bytes, a save is %u", (unsigned)len, (unsigned)size);
        return 0;
    }
    SDL_memcpy(file, bytes, size);
    FixSaveFile(file, kind, swap);
    if (file->magic != SAVE_FILE_MAGIC) {
        SDL_snprintf(err, errSize, "magic %u, not %d", (unsigned)file->magic, SAVE_FILE_MAGIC);
        return 0;
    }
    if (file->version != SaveVersion(kind)) {
        SDL_snprintf(err, errSize, "version 0x%X, not 0x%X", (unsigned)file->version, (unsigned)SaveVersion(kind));
        return 0;
    }
    /* a sum of bytes: the same in either byte order */
    sum = Port_SaveChecksum(file, kind);
    if (file->checksum != sum) {
        SDL_snprintf(err, errSize, "checksum 0x%08X, the data's is 0x%08X", (unsigned)file->checksum, (unsigned)sum);
        return 0;
    }
    return 1;
}

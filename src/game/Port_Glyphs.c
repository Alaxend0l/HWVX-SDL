/* Port: input glyph sets. See Port_Glyphs.h.
 *
 * DataPack_RefreshTextures() has just refilled textureIDs[] with the pack's
 * own textures; Port_Glyphs_ApplyToPack() retargets the entries the active
 * set overrides, before the reloc loop hands them out. */
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "common.h"
#include "Port_Glyphs.h"

#define MAX_SETS 12
#define MAX_TXT_OVERRIDES 32
#define PACK_HISTORY 24

/* --- sets ------------------------------------------------------------------
 * Built-in sets carry their override tables here; glyphs/<set>/glyphs.txt in
 * the data tree can extend them (or define a whole set by itself). The
 * texture index is the position of the texture inside the pack entry; run
 * tools/xgc_tex_dump.py over the pack to see the art behind each number. */

static const struct GlyphOverride s_overridesXbox[] = {
    { NULL, 0, 0, NULL },
};

static const struct GlyphOverride s_overridesPlaystation[] = {
    { NULL, 0, 0, NULL },
};

static const struct GlyphOverride s_overridesSwitch[] = {
    { NULL, 0, 0, NULL },
};

static const struct GlyphOverride s_overridesSteam[] = {
    { NULL, 0, 0, NULL },
};

static const struct GlyphOverride s_overridesKeyboard[] = {
    { NULL, 0, 0, NULL },
};

static struct GlyphSet {
    char name[0x40];
    const struct GlyphOverride *overrides; /* NULL for glyphs.txt-only sets */
    char txt[4096];               /* glyphs.txt contents; tokens live here */
    struct GlyphOverride txtOverrides[MAX_TXT_OVERRIDES];
    int numTxtOverrides;
} s_sets[MAX_SETS];
static int s_numSets;
static const struct GlyphSet *s_active;

/* Gfx_TextureHandler.c's texture table, for the dimensions sanity check. */
typedef struct {
    u8 name[0x24];
    PTR32(void) data; /* 0x24 */
    u32 unk28;
    f32 unk2C;
    u16 topHeight;    /* 0x30: height of the top mip */
    u16 width;        /* 0x32 */
    u16 mipLevels;    /* 0x34 */
    s16 refCount;     /* 0x36 */
    u16 format;       /* 0x38: 0 = RGB8, 1 = RGBA8, 4 = CMPR */
    u16 blobOwned;    /* 0x3A: data belongs to pack bytes, must not be freed */
    u16 view;
    u16 wrapMode;
} PortTexture;
extern PortTexture *m_texture;
extern u32 m_totalTextures;

static void register_set(const char *name, const struct GlyphOverride *overrides)
{
    int i;

    for (i = 0; i < s_numSets; i++) {
        if (strcasecmp(s_sets[i].name, name) == 0)
            return; /* first registration wins */
    }
    if (s_numSets >= MAX_SETS)
        return;
    snprintf(s_sets[s_numSets].name, sizeof(s_sets[s_numSets].name), "%s", name);
    s_sets[s_numSets].overrides = overrides;
    s_sets[s_numSets].numTxtOverrides = 0;
    s_numSets++;
}

static void register_builtins(void)
{
    register_set("gamecube", NULL); /* the original art: nothing to replace */
    register_set("xbox", s_overridesXbox);
    register_set("playstation", s_overridesPlaystation);
    register_set("switch", s_overridesSwitch);
    register_set("steam", s_overridesSteam);
    register_set("keyboard", s_overridesKeyboard);
}

/* --- pack identity ---------------------------------------------------------
 * DataPack objects live in the FileStreaming pools and get refreshed long
 * after their DataPack_LoadFile, so remember name#id per pack pointer from
 * each load. Pool slots are reused; RecordPack keeps the table fresh. */

static struct {
    const void *pack;
    char name[0x50]; /* plenty: pack base names cap at 0x4C */
    u32 id;
} s_packIDs[PACK_HISTORY];

void Port_Glyphs_RecordPack(const void *pack, const char *name, u32 id)
{
    int i, victim = 0;

    for (i = 0; i < PACK_HISTORY; i++) {
        if (s_packIDs[i].pack == pack) {
            victim = i;
            break;
        }
        if (s_packIDs[i].pack == NULL)
            victim = i;
    }
    snprintf(s_packIDs[victim].name, sizeof(s_packIDs[victim].name), "%s", name);
    s_packIDs[victim].pack = pack;
    s_packIDs[victim].id = id;
}

/* --- selection ------------------------------------------------------------- */

static int parse_u32(const char *s, u32 *out)
{
    u32 v = 0;

    if (*s < '0' || *s > '9')
        return 0;
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (*s++ - '0');
    if (*s != 0)
        return 0;
    *out = v;
    return 1;
}

/* glyphs/<set>/glyphs.txt: "override <packName> <packId> <textureIndex>
 * <texName>" lines, read through the game's file layer so paths resolve
 * under the data directory (case-insensitively) like textures do. The
 * strings the overrides use point into set->txt. */
static int scan_glyph_txt(struct GlyphSet *set)
{
    char path[0x80];
    char *text, *line, *save = NULL;
    int n = 0, handle, len;

    snprintf(path, sizeof(path), "glyphs/%s/glyphs.txt", set->name);
    /* Main_Init's FileManager_Create has not run yet, so the file slots are
     * still BSS zeros; File_Open's free-slot scan wants -1. */
    if (g_files[0].slot != -1 && g_files[0].slot != 1) {
        FileManager_Create();
    }
    handle = File_Open(0, path, 0, NULL);
    if (handle <= 0) /* -1: open failed; 0: no free slot; handles are 0x1xx */
        return 0;
    text = set->txt;
    len = File_Read(handle, sizeof(set->txt) - 1, text);
    File_Close(handle);
    if (len <= 0)
        return 0;
    text[len] = 0;
    for (line = strtok_r(text, "\r\n", &save); line != NULL;
         line = strtok_r(NULL, "\r\n", &save)) {
        char *fsave = NULL;
        char *cmd = strtok_r(line, " \t", &fsave);
        char *pack = strtok_r(NULL, " \t", &fsave);
        char *id = strtok_r(NULL, " \t", &fsave);
        char *tex = strtok_r(NULL, " \t", &fsave);
        char *name = strtok_r(NULL, " \t", &fsave);
        u32 v1, v2;

        while (*line == ' ' || *line == '\t')
            line++;
        if (*line == 0 || *line == '#')
            continue;
        if (strcasecmp(cmd, "override") != 0) {
            Port_Log("glyphs: %s: unknown directive: %s", path, cmd);
            continue;
        }
        if (pack == NULL || id == NULL || tex == NULL || name == NULL ||
            !parse_u32(id, &v1) || !parse_u32(tex, &v2)) {
            Port_Log("glyphs: %s: bad override line: %s", path, line);
            continue;
        }
        if (n >= MAX_TXT_OVERRIDES) {
            Port_Log("glyphs: %s: too many overrides", path);
            break;
        }
        set->txtOverrides[n].packName = pack;
        set->txtOverrides[n].packId = v1;
        set->txtOverrides[n].textureIndex = v2;
        set->txtOverrides[n].texName = name;
        n++;
    }
    set->numTxtOverrides = n;
    Port_Log("glyphs: set '%s': %d overrides from %s", set->name, n, path);
    return 1;
}

int Port_Glyphs_Select(const char *name)
{
    struct GlyphSet *set = NULL;
    int i;

    if (name == NULL) {
        s_active = NULL;
        return 1;
    }
    register_builtins();
    for (i = 0; i < s_numSets; i++) {
        if (strcasecmp(s_sets[i].name, name) == 0) {
            set = &s_sets[i];
            break;
        }
    }
    if (set == NULL) { /* a data-defined set? */
        if (s_numSets >= MAX_SETS)
            return 0;
        set = &s_sets[s_numSets];
        snprintf(set->name, sizeof(set->name), "%s", name);
        set->overrides = NULL;
        set->numTxtOverrides = 0;
        s_numSets++;
        if (!scan_glyph_txt(set)) {
            s_numSets--;
            return 0;
        }
    } else {
        scan_glyph_txt(set);
    }
    s_active = set;
    Port_Log("input glyphs: %s", set->name);
    return 1;
}

const char *Port_Glyphs_KnownSets(void)
{
    static char buf[256];
    int i, len = 0;

    register_builtins();
    buf[0] = 0;
    for (i = 0; i < s_numSets; i++) {
        int n = snprintf(buf + len, sizeof(buf) - len, "%s%s",
                         i ? ", " : "", s_sets[i].name);
        if (n < 0 || n >= sizeof(buf) - len)
            break;
        len += n;
    }
    return buf;
}

/* --- applying ---------------------------------------------------------------
 * RefreshTextures can run often, so each override file gets at most one
 * missing-file and one size-mismatch warning. */

static struct {
    const char *texName;
    int warnedMissing, warnedSize;
} s_warnings[64];
static int s_numWarnings;

/* 1 the first time (texName, kind) is seen, 0 afterwards */
static int should_warn(const char *texName, int missing)
{
    int i;

    for (i = 0; i < s_numWarnings; i++) {
        if (strcmp(s_warnings[i].texName, texName) == 0)
            break;
    }
    if (i == s_numWarnings) {
        if (s_numWarnings >= (int)(sizeof(s_warnings) / sizeof(s_warnings[0])))
            return 0;
        s_warnings[i].texName = texName;
        s_warnings[i].warnedMissing = 0;
        s_warnings[i].warnedSize = 0;
        s_numWarnings++;
    }
    if (missing ? s_warnings[i].warnedMissing : s_warnings[i].warnedSize)
        return 0;
    if (missing)
        s_warnings[i].warnedMissing = 1;
    else
        s_warnings[i].warnedSize = 1;
    return 1;
}

static void apply_override(const char *packName, u32 packId,
                           const struct GlyphOverride *ov, DataPack *pack)
{
    u16 *ids;
    u16 newID, oldID;

    if (ov->textureIndex >= pack->numTextures)
        return;
    ids = GETP(pack->textureIDs);
    newID = Gfx_LoadTexture((char *)ov->texName);
    if (newID == 0xFFFF) {
        if (should_warn(ov->texName, 1))
            Port_Log("glyphs: %s#%u tex %u: no '%s.btf' or '.tga' in the data directory",
                     packName, packId, ov->textureIndex, ov->texName);
        return;
    }
    oldID = ids[ov->textureIndex];
    if (oldID < m_totalTextures && newID < m_totalTextures &&
        (m_texture[oldID].width != m_texture[newID].width ||
         m_texture[oldID].topHeight != m_texture[newID].topHeight)) {
        if (should_warn(ov->texName, 0))
            Port_Log("glyphs: %s#%u tex %u: '%s' is %ux%u but the original glyph is %ux%u;"
                     " the prompt quads keep the original size, so the glyph may be cropped or stretched",
                     packName, packId, ov->textureIndex, ov->texName,
                     m_texture[newID].width, m_texture[newID].topHeight,
                     m_texture[oldID].width, m_texture[oldID].topHeight);
    }
    ids[ov->textureIndex] = newID;
}

void Port_Glyphs_ApplyToPack(DataPack *pack)
{
    const struct GlyphSet *set = s_active;
    const struct GlyphOverride *ov;
    int i, j;

    if (set == NULL)
        return;
    for (i = 0; i < PACK_HISTORY; i++) {
        if (s_packIDs[i].pack != pack)
            continue;
        for (ov = set->overrides; ov != NULL && ov->packName != NULL; ov++) {
            if (ov->packId == s_packIDs[i].id &&
                strcmp(ov->packName, s_packIDs[i].name) == 0)
                apply_override(s_packIDs[i].name, s_packIDs[i].id, ov, pack);
        }
        for (j = 0; j < set->numTxtOverrides; j++) {
            ov = &set->txtOverrides[j];
            if (ov->packId == s_packIDs[i].id &&
                strcmp(ov->packName, s_packIDs[i].name) == 0)
                apply_override(s_packIDs[i].name, s_packIDs[i].id, ov, pack);
        }
        return;
    }
}

/* The PS2 prototype as a source of content beside the release being played
 * (docs/xdl-format/README.md §9.1, HWVX-X-FILE-EDITOR): where its files are
 * and which import mode the player picked. Port-only.
 *
 * The prototype is not playable on its own (its MCP and worlds are an earlier
 * game); its packs are loaded one by one into the game being played, under
 * names starting with PORT_PROTO_PREFIX ("~HW_Cars"), which dvd.c opens from
 * the prototype's folder (game/Port_Proto.c says which packs and why).
 *
 * The folder: HWVX_PROTO_DIR, else sources.cfg's "ps2beta=" (the launcher's
 * Games page imports it). The mode: HWVX_PROTO, else sources.cfg's "proto="
 * (the same page, on the prototype's row): off, cars, added, replace. */
#include "port_internal.h"

static int s_mode = -1;
static char *s_root;
static int s_rootRead;

static const char *const s_modeKeys[PORT_PROTO_MODES] = { "off", "cars", "added", "replace" };

const char *Port_ProtoModeKey(int mode) {
    return mode >= 0 && mode < PORT_PROTO_MODES ? s_modeKeys[mode] : "off";
}

int Port_ProtoModeNamed(const char *key) {
    int i;

    for (i = 0; key != NULL && i < PORT_PROTO_MODES; i++) {
        if (SDL_strcasecmp(key, s_modeKeys[i]) == 0) {
            return i;
        }
    }
    return -1;
}

/* "key=value" of sources.cfg in the save folder, or NULL (SDL_free it). */
static char *SourcesValue(const char *key) {
    char *pref = Port_PrefPath(), *path = NULL, *text, *line, *save = NULL, *found = NULL;
    size_t kl = SDL_strlen(key);

    if (pref == NULL || SDL_asprintf(&path, "%ssources.cfg", pref) < 0) {
        SDL_free(pref);
        return NULL;
    }
    SDL_free(pref);
    text = SDL_LoadFile(path, NULL);
    SDL_free(path);
    for (line = text != NULL ? SDL_strtok_r(text, "\r\n", &save) : NULL; line != NULL;
         line = SDL_strtok_r(NULL, "\r\n", &save)) {
        if (SDL_strncmp(line, key, kl) == 0 && line[kl] == '=' && line[kl + 1] != 0) {
            found = SDL_strdup(line + kl + 1);
            break;
        }
    }
    SDL_free(text);
    return found;
}

char *Port_ProtoSetting(const char *key) {
    return SourcesValue(key);
}

const char *Port_ProtoRoot(void) {
    if (!s_rootRead) {
        const char *env = SDL_getenv("HWVX_PROTO_DIR");

        s_rootRead = 1;
        s_root = env != NULL && *env ? SDL_strdup(env) : SourcesValue("ps2beta");
    }
    return s_root;
}

int Port_ProtoMode(void) {
    if (s_mode < 0) {
        const char *env = SDL_getenv("HWVX_PROTO");
        char *v = env != NULL && *env ? SDL_strdup(env) : SourcesValue("proto");
        int m = Port_ProtoModeNamed(v);

        if (v != NULL && m < 0) {
            Port_Log("prototype: no import mode \"%s\" (off, cars, added, replace); off", v);
        }
        s_mode = m < 0 ? PORT_PROTO_OFF : m;
        SDL_free(v);
        if (s_mode != PORT_PROTO_OFF && Port_ProtoRoot() == NULL) {
            Port_Log("prototype: mode %s, but no prototype imported (ps2beta= in sources.cfg, HWVX_PROTO_DIR); off",
                     s_modeKeys[s_mode]);
            s_mode = PORT_PROTO_OFF;
        }
    }
    return s_mode;
}

const char *Port_ProtoName(const char *fileName) {
    while (*fileName == '/') {
        fileName++;
    }
    return *fileName == PORT_PROTO_PREFIX ? fileName + 1 : NULL;
}

/* The prototype's identity for online play (game/Port_Online.c's hello): an FNV-1a
 * of its files' paths (case folded) and sizes, sorted, as 16 hex digits; "" with
 * no prototype. Computed once. */
static int CompareNames(const void *a, const void *b) {
    return SDL_strcasecmp(*(const char *const *)a, *(const char *const *)b);
}

const char *Port_ProtoIdentity(void) {
    static char s_id[17];
    static int s_done;
    const char *root;
    char **names;
    int i, n = 0;
    u64 h = 0xCBF29CE484222325ull;

    if (s_done) {
        return s_id;
    }
    s_done = 1;
    if ((root = Port_ProtoRoot()) == NULL || (names = SDL_GlobDirectory(root, NULL, 0, &n)) == NULL) {
        return s_id;
    }
    SDL_qsort(names, (size_t)n, sizeof(*names), CompareNames);
    for (i = 0; i < n; i++) {
        SDL_PathInfo info;
        char *path = NULL;
        const char *c;
        u64 size;
        int k;

        if (SDL_asprintf(&path, "%s/%s", root, names[i]) < 0) {
            continue;
        }
        if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE) {
            for (c = names[i]; *c; c++) {
                char ch = *c == '\\' ? '/' : (char)SDL_tolower((unsigned char)*c);

                h = (h ^ (u8)ch) * 0x100000001B3ull;
            }
            size = info.size;
            for (k = 0; k < 8; k++) {
                h = (h ^ (u8)(size >> (k * 8))) * 0x100000001B3ull;
            }
        }
        SDL_free(path);
    }
    SDL_free(names);
    SDL_snprintf(s_id, sizeof(s_id), "%016llx", (unsigned long long)h);
    return s_id;
}

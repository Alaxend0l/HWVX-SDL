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

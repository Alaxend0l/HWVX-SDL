/* DVD: reads go to the extracted disc files under the data directory. Reads
 * complete synchronously; async callbacks fire before the call returns, which
 * the game tolerates (it polls status afterwards). */
#include "port_internal.h"

static char *JoinPath(const char *name) {
    size_t n;
    char *path;

    while (*name == '/') {
        name++;
    }
    n = strlen(g_port.dataDir) + strlen(name) + 2;
    path = SDL_malloc(n);
    SDL_snprintf(path, n, "%s/%s", g_port.dataDir, name);
    return path;
}

/* Disc paths are case-insensitive on the GameCube; host file systems often
 * aren't. Try the name as given, then resolve it one path component at a time
 * against the directory listing, ignoring case. */
typedef struct CaseMatch {
    const char *want;
    char *found;
} CaseMatch;

static SDL_EnumerationResult SDLCALL MatchEntry(void *userdata, const char *dir, const char *name) {
    CaseMatch *m = (CaseMatch *)userdata;
    (void)dir;
    if (SDL_strcasecmp(name, m->want) == 0) {
        m->found = SDL_strdup(name);
        return SDL_ENUM_SUCCESS;
    }
    return SDL_ENUM_CONTINUE;
}

static char *ResolveCase(const char *name) {
    char *dir = SDL_strdup(g_port.dataDir);
    const char *p = name;

    while (*p == '/') {
        p++;
    }
    while (*p) {
        const char *end = SDL_strchr(p, '/');
        size_t len = end ? (size_t)(end - p) : SDL_strlen(p);
        char *part = SDL_strndup(p, len);
        CaseMatch m = { part, NULL };
        char *next;
        size_t n;

        SDL_EnumerateDirectory(dir, MatchEntry, &m);
        SDL_free(part);
        if (m.found == NULL) {
            SDL_free(dir);
            return NULL;
        }
        n = SDL_strlen(dir) + SDL_strlen(m.found) + 2;
        next = SDL_malloc(n);
        SDL_snprintf(next, n, "%s/%s", dir, m.found);
        SDL_free(m.found);
        SDL_free(dir);
        dir = next;
        p += len;
        while (*p == '/') {
            p++;
        }
    }
    return dir;
}

SDL_IOStream *Port_OpenDiscFile(const char *name);

static SDL_IOStream *OpenDiscFile(const char *name) {
    char *path = JoinPath(name);
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");

    SDL_free(path);
    if (io == NULL && (path = ResolveCase(name)) != NULL) {
        io = SDL_IOFromFile(path, "rb");
        SDL_free(path);
    }
    return io;
}

/* For readers outside the DVD API (thp.c). */
SDL_IOStream *Port_OpenDiscFile(const char *name) {
    return OpenDiscFile(name);
}

void DVDInit(void) {}

BOOL DVDOpen(const char *fileName, DVDFileInfo *fi) {
    SDL_IOStream *io = OpenDiscFile(fileName);
    if (io == NULL) {
        Port_Log("DVDOpen: %s not found", fileName);
        return FALSE;
    }
    memset(fi, 0, sizeof(*fi));
    fi->host = io;
    fi->length = (u32)SDL_GetIOSize(io);
    return TRUE;
}

BOOL DVDClose(DVDFileInfo *fi) {
    if (fi->host != NULL) {
        SDL_CloseIO(fi->host);
        fi->host = NULL;
    }
    return TRUE;
}

/* Bumped on every data read (not the game's 0x20-byte disc-check polls of
 * bsfile); HWVX_AUTOINPUT waits for the disc to go quiet. */
volatile u32 g_portDiscReads;

s32 DVDReadPrio(DVDFileInfo *fi, void *addr, s32 length, s32 offset, s32 prio) {
    size_t got;
    (void)prio;
    if (length > 0x20) {
        g_portDiscReads++;
    }
    if (SDL_SeekIO(fi->host, offset, SDL_IO_SEEK_SET) < 0) {
        return -1;
    }
    got = SDL_ReadIO(fi->host, addr, (size_t)length);
    return (s32)got;
}

BOOL DVDReadAsyncPrio(DVDFileInfo *fi, void *addr, s32 length, s32 offset,
                      void (*callback)(s32, DVDFileInfo *), s32 prio) {
    s32 result = DVDReadPrio(fi, addr, length, offset, prio);
    if (callback != NULL) {
        callback(result, fi);
    }
    return result >= 0;
}

s32 DVDGetCommandBlockStatus(const DVDCommandBlock *block) {
    (void)block;
    return 0; /* DVD_STATE_END */
}

s32 DVDGetDriveStatus(void) { return 0; }
BOOL DVDCancel(DVDCommandBlock *block) { (void)block; return TRUE; }

BOOL DVDInquiryAsync(DVDCommandBlock *block, void *info, void (*callback)(s32, DVDCommandBlock *)) {
    if (info != NULL) {
        memset(info, 0, 0x20);
    }
    if (callback != NULL) {
        callback(0x20, block);
    }
    return TRUE;
}

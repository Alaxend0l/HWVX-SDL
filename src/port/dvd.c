/* DVD: reads go to the disc image (disc.c) when the game data is one, else
 * to the extracted disc files under the data directory. Reads complete
 * synchronously; async callbacks fire before the call returns, which the game
 * tolerates (it polls status afterwards). */
#include "port_internal.h"
#include "disc.h"
#include "xdl.h"
#include "Port_Mix.h"

/* `root`: the data folder, or a release's in a mixed build (mix.c). */
static char *JoinPath(const char *root, const char *name) {
    size_t n;
    char *path;

    while (*name == '/') {
        name++;
    }
    n = strlen(root) + strlen(name) + 2;
    path = SDL_malloc(n);
    SDL_snprintf(path, n, "%s/%s", root, name);
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

static char *ResolveCase(const char *root, const char *name) {
    char *dir = SDL_strdup(root);
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

/* `found`, if not NULL: the host path it was found at (NULL for a disc
 * image's file), for the caller to free. */
static SDL_IOStream *OpenIn(const char *root, const char *name, char **found) {
    char *path;
    SDL_IOStream *io;

    if (found != NULL) {
        *found = NULL;
    }
    if (Disc_IsOpen() && root == g_port.dataDir) {
        return Disc_OpenFile(name);
    }
    path = JoinPath(root, name);
    io = SDL_IOFromFile(path, "rb");

    if (io == NULL) {
        SDL_free(path);
        path = NULL;
    }
    if (io == NULL && (path = ResolveCase(root, name)) != NULL) {
        io = SDL_IOFromFile(path, "rb");
        if (io == NULL) {
            SDL_free(path);
            path = NULL;
        }
    }
    /* a flat folder (the PC disc's cabinet, imported by pcdisc.c, keeps no
     * folders): the file by its name alone; every pack name is unique */
    if (io == NULL) {
        const char *base = name, *p;

        for (p = name; *p; p++) {
            if (*p == '/' || *p == '\\') {
                base = p + 1;
            }
        }
        if (base != name && (path = ResolveCase(root, base)) != NULL) {
            io = SDL_IOFromFile(path, "rb");
            if (io == NULL) {
                SDL_free(path);
                path = NULL;
            }
        }
    }
    if (found != NULL) {
        *found = path;
    } else {
        SDL_free(path);
    }
    return io;
}

SDL_IOStream *Port_OpenReleaseFile(int rel, const char *name, char **found);

static SDL_IOStream *OpenDiscFile(const char *name, char **found) {
    return OpenIn(g_port.dataDir, name, found);
}

/* For readers outside the DVD API (thp.c). In a mixed build, from the
 * release the recipe's "files" rules name (mix.c). */
SDL_IOStream *Port_OpenDiscFile(const char *name) {
    if (Port_MixActive()) {
        SDL_IOStream *io = NULL;
        int k, rel;

        for (k = 0; io == NULL && (rel = Port_MixCandidate(name, k)) >= 0; k++) {
            io = Port_OpenReleaseFile(rel, name, NULL);
        }
        return io;
    }
    return OpenDiscFile(name, NULL);
}

void DVDInit(void) {}

/* Where the releases keep a pack the game asks for by its GameCube name
 * (".xgc"): the extension, and a folder to look in first (the PS2 disc keeps
 * its packs under XPS/ENGLISH/; the data folder may also be that folder
 * itself). Formats past the GameCube's are only found in a data folder, not
 * a disc image. */
static const struct {
    const char *ext;
    const char *dir;
} s_formats[PORT_DATA_COUNT] = {
    [PORT_DATA_GC] = { ".xgc", "" },
    [PORT_DATA_PC] = { ".xpc", "" },
    [PORT_DATA_PS2] = { ".xps", "XPS/ENGLISH/" },
};

/* fileName as format `fmt` would name it in `dir`, or NULL if it isn't a pack. */
static char *FormatName(const char *fileName, int fmt, const char *dir) {
    size_t n = SDL_strlen(fileName);
    size_t len;
    char *name;

    if (s_formats[fmt].ext == NULL || n < 4 || SDL_strcasecmp(fileName + n - 4, ".xgc") != 0) {
        return NULL;
    }
    len = SDL_strlen(dir) + n + 1;
    name = SDL_malloc(len);
    SDL_snprintf(name, len, "%s%.*s%s", dir, (int)(n - 4), fileName, s_formats[fmt].ext);
    return name;
}

/* The file `name` (the GameCube's name) as release `rel` of a mixed build has
 * it, under that release's own name and folder; NULL if it has none. */
SDL_IOStream *Port_OpenReleaseFile(int rel, const char *name, char **found) {
    const char *root = Port_MixRoot(rel);
    const char *dirs[2] = { s_formats[rel].dir, "" };
    SDL_IOStream *io = NULL;
    int d;

    if (found != NULL) {
        *found = NULL;
    }
    if (root == NULL) {
        return NULL;
    }
    if (rel == PORT_DATA_GC) {
        return OpenIn(root, name, found);
    }
    for (d = 0; io == NULL && d < (*dirs[0] ? 2 : 1); d++) {
        char *alt = FormatName(name, rel, dirs[d]);

        if (alt != NULL) {
            io = OpenIn(root, alt, found);
            SDL_free(alt);
        } else {
            /* not a pack: under its own name */
            size_t n = SDL_strlen(dirs[d]) + SDL_strlen(name) + 1;

            alt = SDL_malloc(n);
            SDL_snprintf(alt, n, "%s%s", dirs[d], name);
            io = OpenIn(root, alt, found);
            SDL_free(alt);
        }
    }
    return io;
}

/* The release whose menus the game is running on: the format DVDOpen found
 * the interface pack (HW_IPack) in. In a folder mixing releases file by
 * file, other packs may come from elsewhere; the menus, and the platform
 * value their scripts read (code variable 0x258), follow this one. */
static int s_release = PORT_DATA_GC;

int Port_DataRelease(void) {
    return s_release;
}

BOOL DVDOpen(const char *fileName, DVDFileInfo *fi) {
    char *found = NULL;
    SDL_IOStream *io = NULL;
    u32 format = PORT_DATA_GC;
    int fmt;

    if (Port_MixActive()) {
        /* a mixed build: from the archive's base release (mix.c), each under its own name */
        int k, rel;

        for (k = 0; io == NULL && (rel = Port_MixCandidate(fileName, k)) >= 0; k++) {
            io = Port_OpenReleaseFile(rel, fileName, &found);
            format = (u32)rel;
        }
    } else {
        io = OpenDiscFile(fileName, &found);
    }

    /* A data folder can hold another release's packs instead, file by file
     * (game/Port_Data.c reads the format back through Port_FileFormat). */
    for (fmt = PORT_DATA_GC + 1; io == NULL && !Port_MixActive() && fmt < PORT_DATA_COUNT && !Disc_IsOpen(); fmt++) {
        const char *dirs[2] = { s_formats[fmt].dir, "" };
        int d;

        for (d = 0; io == NULL && d < (*dirs[0] ? 2 : 1); d++) {
            char *alt = FormatName(fileName, fmt, dirs[d]);

            if (alt != NULL) {
                io = OpenDiscFile(alt, &found);
                format = fmt;
                SDL_free(alt);
            }
        }
    }
    if (io == NULL) {
        Port_Log("DVDOpen: %s not found", fileName);
        return FALSE;
    }
    {
        size_t n = SDL_strlen(fileName);

        const char *base = SDL_strrchr(fileName, '/');

        base = base != NULL ? base + 1 : fileName;
        if (n >= 4 && SDL_strcasecmp(fileName + n - 4, ".xgc") == 0 && SDL_strncasecmp(base, "HW_IPack.", 9) == 0) {
            s_release = (int)format;
        }
    }
    memset(fi, 0, sizeof(*fi));
    fi->host = io;
    fi->format = format;
    fi->length = (u32)SDL_GetIOSize(io);
    /* a pack's converted form, cached (HWVX_XDL, HWVX_XDL_WRITE): looked up
     * by DataPack_LoadDataPack, which skips the source's conversions */
    fi->xdl = Xdl_OpenFile(fileName, (int)format, found);
    SDL_free(found);
    return TRUE;
}

BOOL DVDClose(DVDFileInfo *fi) {
    Xdl_CloseFile(fi->xdl);
    fi->xdl = NULL;
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

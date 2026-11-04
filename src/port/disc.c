/* GameCube disc images as the game data (see disc.h): a plain image (.iso,
 * .gcm), the disc byte for byte.
 *
 * On the disc (big-endian): the magic 0xC2339F3D at 0x1C, the file system
 * table's offset and size at 0x424/0x428. The table is 12-byte entries, then
 * their names: entry 0 is the root directory; a directory's entry holds its
 * parent and the index after its last descendant, a file's its offset and
 * length on the disc. */
#include "port_internal.h"
#include "disc.h"

#define GC_MAGIC 0xC2339F3Du

static struct {
    SDL_IOStream *io;
    SDL_Mutex *lock;
    Uint64 size;
    char id[7];

    /* file system table */
    Uint8 *fst;
    Uint32 fstSize, entries;
} d;

static Uint32 GetBE32(const Uint8 *p) {
    return (Uint32)p[0] << 24 | (Uint32)p[1] << 16 | (Uint32)p[2] << 8 | p[3];
}

/* n bytes of the disc at offset; 0 past its end. Callers hold d.lock. */
static int ReadDisc(Uint64 offset, Uint8 *buf, size_t n) {
    if (offset > d.size || n > d.size - offset) {
        return 0;
    }
    return SDL_SeekIO(d.io, (Sint64)offset, SDL_IO_SEEK_SET) >= 0 && SDL_ReadIO(d.io, buf, n) == n;
}

/* ---- opening -------------------------------------------------------------- */

void Disc_Close(void) {
    if (d.io != NULL) {
        SDL_CloseIO(d.io);
    }
    if (d.lock != NULL) {
        SDL_DestroyMutex(d.lock);
    }
    SDL_free(d.fst);
    SDL_zero(d);
}

int Disc_IsOpen(void) {
    return d.io != NULL;
}

const char *Disc_GameID(void) {
    return d.id;
}

int Disc_IsImageName(const char *path) {
    const char *dot = path != NULL ? SDL_strrchr(path, '.') : NULL;

    return dot != NULL && (SDL_strcasecmp(dot, ".iso") == 0 || SDL_strcasecmp(dot, ".gcm") == 0 ||
                           SDL_strcasecmp(dot, ".gcz") == 0);
}

static int Fail(const char *why) {
    SDL_SetError("%s", why);
    Disc_Close();
    return 0;
}

int Disc_Open(const char *path) {
    Uint8 h[0x440];
    Uint32 fstOffset;
    Sint64 fileSize;

    Disc_Close();
    d.io = SDL_IOFromFile(path, "rb");
    if (d.io == NULL) {
        return 0;   /* SDL's error */
    }
    d.lock = SDL_CreateMutex();
    fileSize = SDL_GetIOSize(d.io);
    d.size = fileSize > 0 ? (Uint64)fileSize : 0;
    if (d.lock == NULL || fileSize < (Sint64)sizeof(h) || !ReadDisc(0, h, sizeof(h))) {
        return Fail("too small to be a GameCube disc image");
    }
    if (GetBE32(h + 0x1C) != GC_MAGIC) {
        if (h[0] == 0x01 && h[1] == 0xC0 && h[2] == 0x0B && h[3] == 0xB1) {
            return Fail("that is a compressed .gcz image; convert it to .iso first "
                        "(Dolphin: right-click the game > Convert File..., format ISO)");
        }
        return Fail(GetBE32(h + 0x18) == 0x5D1C9EA3 ? "that is a Wii disc image, not a GameCube one"
                                                  : "not a GameCube disc image");
    }
    SDL_memcpy(d.id, h, 6);
    d.id[6] = 0;
    fstOffset = GetBE32(h + 0x424);
    d.fstSize = GetBE32(h + 0x428);
    if (d.fstSize < 12 || d.fstSize > (16u << 20) || fstOffset >= d.size) {
        return Fail("the disc image has no readable file table (a trimmed or damaged image?)");
    }
    d.fst = SDL_malloc(d.fstSize + 1);
    if (d.fst == NULL || !ReadDisc(fstOffset, d.fst, d.fstSize)) {
        return Fail("the disc image's file table is unreadable (a trimmed or damaged image?)");
    }
    d.fst[d.fstSize] = 0;   /* names end inside it */
    d.entries = GetBE32(d.fst + 8);
    if (d.entries == 0 || (Uint64)d.entries * 12 > d.fstSize || (d.fst[0] & 1) == 0) {
        return Fail("the disc image's file table is damaged");
    }
    Port_Log("disc image %s: %s, %u files", path, d.id, d.entries);
    return 1;
}

/* ---- files ---------------------------------------------------------------- */

static const char *EntryName(Uint32 i) {
    Uint32 off = GetBE32(d.fst + i * 12) & 0xFFFFFF, base = d.entries * 12;

    return base + off < d.fstSize ? (const char *)d.fst + base + off : "";
}

/* The entry for name, or 0 (the root is never a file). */
static Uint32 Lookup(const char *name) {
    Uint32 dir = 0, i, end;

    for (;;) {
        const char *slash;
        size_t len;

        while (*name == '/' || *name == '\\') {
            name++;
        }
        if (*name == 0) {
            return 0;
        }
        slash = name;
        while (*slash && *slash != '/' && *slash != '\\') {
            slash++;
        }
        len = (size_t)(slash - name);
        end = dir == 0 ? d.entries : GetBE32(d.fst + dir * 12 + 8);
        if (end > d.entries) {
            return 0;
        }
        for (i = dir + 1; i < end;) {
            const char *n = EntryName(i);
            int isDir = d.fst[i * 12] != 0;

            if (SDL_strlen(n) == len && SDL_strncasecmp(n, name, len) == 0) {
                break;
            }
            /* past a directory's whole subtree */
            i = isDir ? SDL_max(GetBE32(d.fst + i * 12 + 8), i + 1) : i + 1;
        }
        if (i >= end) {
            return 0;
        }
        if (*slash == 0) {
            return d.fst[i * 12] == 0 ? i : 0;   /* a file, not a directory */
        }
        if (d.fst[i * 12] == 0) {
            return 0;   /* a file used as a directory */
        }
        dir = i;
        name = slash;
    }
}

typedef struct DiscFile {
    Uint64 start;
    Uint32 length, pos;
} DiscFile;

static Sint64 SDLCALL FileSize(void *userdata) {
    return ((DiscFile *)userdata)->length;
}

static Sint64 SDLCALL FileSeek(void *userdata, Sint64 offset, SDL_IOWhence whence) {
    DiscFile *f = userdata;
    Sint64 to = whence == SDL_IO_SEEK_SET ? offset : whence == SDL_IO_SEEK_CUR ? f->pos + offset
                                                                               : f->length + offset;

    if (to < 0 || to > f->length) {
        return SDL_SetError("seek outside the file");
    }
    f->pos = (Uint32)to;
    return to;
}

static size_t SDLCALL FileRead(void *userdata, void *ptr, size_t size, SDL_IOStatus *status) {
    DiscFile *f = userdata;
    size_t n = SDL_min(size, (size_t)(f->length - f->pos));
    int ok;

    if (n == 0) {
        *status = SDL_IO_STATUS_EOF;
        return 0;
    }
    SDL_LockMutex(d.lock);
    ok = ReadDisc(f->start + f->pos, ptr, n);
    SDL_UnlockMutex(d.lock);
    if (!ok) {
        *status = SDL_IO_STATUS_ERROR;
        SDL_SetError("the disc image is damaged");
        return 0;
    }
    f->pos += (Uint32)n;
    return n;
}

static bool SDLCALL FileClose(void *userdata) {
    SDL_free(userdata);
    return true;
}

SDL_IOStream *Disc_OpenFile(const char *name) {
    SDL_IOStreamInterface iface;
    DiscFile *f;
    SDL_IOStream *io;
    Uint32 i;

    if (d.io == NULL || (i = Lookup(name)) == 0) {
        return NULL;
    }
    f = SDL_calloc(1, sizeof(*f));
    if (f == NULL) {
        return NULL;
    }
    f->start = GetBE32(d.fst + i * 12 + 4);
    f->length = GetBE32(d.fst + i * 12 + 8);
    if (f->start > d.size || f->length > d.size - f->start) {
        SDL_free(f);
        SDL_SetError("%s lies outside the disc image (a trimmed image?)", name);
        return NULL;
    }
    SDL_INIT_INTERFACE(&iface);
    iface.size = FileSize;
    iface.seek = FileSeek;
    iface.read = FileRead;
    iface.close = FileClose;
    io = SDL_OpenIO(&iface, f);
    if (io == NULL) {
        SDL_free(f);
    }
    return io;
}

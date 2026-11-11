/* Importing the PC and PS2 releases from their disc images (launcher.c).
 *
 * The PS2 discs (the release's and the prototype's DVD images, .iso) are
 * ISO 9660 too, with the game's files as they are: the import copies
 * XPS/ENGLISH (packs, audio, movies) into <pref>/ps2 or <pref>/ps2beta, by
 * the archive version of its HW_share.xps, under the disc's upper-case names
 * (dvd.c matches names without case). The console's own files (the ELF,
 * IOP modules) are left out.
 *
 * The PC CD (an .iso, or a .cue with its MODE1/2352 .bin) is ISO 9660. The
 * game's packs are not on it as files: the installer takes them out of
 * HWVX.cab, a Microsoft cabinet compressed with MSZIP (deflate blocks, each
 * starting "CK", sharing their history within a cabinet folder). The
 * movies (the .bik files in FMV_) stay on the disc: the game played them
 * from there, so an installed copy may have none.
 *
 * The import copies both into a folder of the player's own (the port's
 * preference folder): the cabinet's files flat, as they are stored (every
 * pack name is unique; dvd.c finds a pack by its name when the folder path
 * the game asks for isn't there), and the movies under FMV_/. Nothing here
 * changes the disc image. */
#include "port_internal.h"

/* ---- the disc: ISO 9660 in 2048- or 2352-byte sectors ------------------- */

typedef struct Disc {
    SDL_IOStream *io;
    u32 sectorSize, dataOffset;    /* 2048/0, or raw MODE1 2352/16 */
    Sint64 sectors;                /* in the image */
} Disc;

/* The disc image is the player's file, not ours: every size and place read
 * from it is checked before use. Directories are small; anything bigger is a
 * damaged or crafted image. */
#define MAX_DIR_SIZE (1u << 20)

/* An extent of `size` bytes at sector `lba` lies inside the image. */
static int InImage(const Disc *d, u32 lba, u32 size) {
    return (Sint64)lba + ((Sint64)size + 2047) / 2048 <= d->sectors;
}

/* A name from the disc or the cabinet, safe to use as one path component. */
static int SafeName(const char *name) {
    return name[0] != '\0' && SDL_strcmp(name, ".") != 0 && SDL_strcmp(name, "..") != 0 &&
           SDL_strpbrk(name, "/\\:") == NULL;
}

static int ReadSectors(Disc *d, u32 lba, u32 count, u8 *out) {
    u32 i;

    for (i = 0; i < count; i++) {
        if (SDL_SeekIO(d->io, (Sint64)(lba + i) * d->sectorSize + d->dataOffset, SDL_IO_SEEK_SET) < 0 ||
            SDL_ReadIO(d->io, out + i * 2048, 2048) != 2048) {
            return 0;
        }
    }
    return 1;
}

/* Read `size` bytes of a file starting at sector `lba`. */
static int ReadExtent(Disc *d, u32 lba, u32 size, u8 *out) {
    u8 sector[2048];
    u32 done = 0;

    if (!InImage(d, lba, size)) {
        return 0;
    }
    while (done < size) {
        u32 n = size - done < 2048 ? size - done : 2048;
        if (!ReadSectors(d, lba++, 1, sector)) {
            return 0;
        }
        SDL_memcpy(out + done, sector, n);
        done += n;
    }
    return 1;
}

static u32 LE32(const u8 *p) {
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static u16 LE16(const u8 *p) {
    return (u16)(p[0] | p[1] << 8);
}

static int IsAbsolute(const char *path) {
    return path[0] == '/' || path[0] == '\\' || (path[0] != '\0' && path[1] == ':');
}

/* The track file a .cue names: FILE "name" BINARY, or FILE name BINARY, in
 * any case; relative to the .cue's folder unless absolute. */
static int CueTrack(const char *cuePath, char *bin, size_t size) {
    char *cue = SDL_LoadFile(cuePath, NULL), *line, *save = NULL;
    const char *slash = SDL_strrchr(cuePath, '/'), *back = SDL_strrchr(cuePath, '\\');
    int found = 0;

    if (back != NULL && (slash == NULL || back > slash)) {
        slash = back;
    }
    for (line = cue != NULL ? SDL_strtok_r(cue, "\r\n", &save) : NULL; line != NULL && !found;
         line = SDL_strtok_r(NULL, "\r\n", &save)) {
        char *name, *end;

        while (*line == ' ' || *line == '\t') {
            line++;
        }
        if (SDL_strncasecmp(line, "FILE", 4) != 0 || (line[4] != ' ' && line[4] != '\t')) {
            continue;
        }
        for (name = line + 4; *name == ' ' || *name == '\t'; name++) {
        }
        if (*name == '"') {
            end = SDL_strchr(++name, '"');
        } else {
            end = SDL_strpbrk(name, " \t");
        }
        if (end != NULL) {
            *end = '\0';
        }
        if (*name == '\0') {
            continue;
        }
        if (IsAbsolute(name) || slash == NULL) {
            SDL_strlcpy(bin, name, size);
        } else {
            SDL_snprintf(bin, size, "%.*s%s", (int)(slash - cuePath + 1), cuePath, name);
        }
        found = 1;
    }
    SDL_free(cue);
    return found;
}

/* The open disc behind `path` (.iso, .bin, or a .cue naming its .bin). */
static int OpenDisc(const char *path, Disc *d) {
    char bin[1200];
    u8 sync[16];
    size_t n = SDL_strlen(path);
    Sint64 bytes;

    SDL_zerop(d);
    SDL_strlcpy(bin, path, sizeof(bin));
    if (n > 4 && SDL_strcasecmp(path + n - 4, ".cue") == 0 && !CueTrack(path, bin, sizeof(bin))) {
        return 0;
    }
    d->io = SDL_IOFromFile(bin, "rb");
    if (d->io == NULL) {
        return 0;
    }
    /* raw sectors start with the 12-byte sync pattern 00 FF x10 00 */
    if (SDL_ReadIO(d->io, sync, 16) == 16 && sync[0] == 0 && sync[1] == 0xFF && sync[10] == 0xFF && sync[11] == 0) {
        d->sectorSize = 2352;
        d->dataOffset = 16;
    } else {
        d->sectorSize = 2048;
        d->dataOffset = 0;
    }
    bytes = SDL_GetIOSize(d->io);
    d->sectors = bytes > 0 ? bytes / d->sectorSize : 0;
    return 1;
}

/* A directory's extent read whole, or NULL. */
static u8 *ReadDir(Disc *d, u32 lba, u32 size) {
    u8 *dir;

    if (size == 0 || size > MAX_DIR_SIZE || (dir = SDL_malloc(size)) == NULL) {
        return NULL;
    }
    if (!ReadExtent(d, lba, size, dir)) {
        SDL_free(dir);
        return NULL;
    }
    return dir;
}

/* The directory record at `p` in dir/size, its length in *len; 0 at the end
 * of the directory (or a damaged record). A zero length byte is padding to
 * the next sector: *len then takes p there and the record is empty (1, but
 * *name is ""). */
static int Record(const u8 *dir, u32 size, const u8 *p, u32 *len, char name[256]) {
    u32 at = (u32)(p - dir), nlen;

    name[0] = '\0';
    if (at >= size) {
        return 0;
    }
    if (p[0] == 0) {
        *len = (at / 2048 + 1) * 2048 - at;
        return 1;
    }
    *len = p[0];
    if (*len < 34 || *len > size - at || (nlen = p[32]) == 0 || 33 + nlen > *len) {
        return 0;
    }
    SDL_memcpy(name, p + 33, nlen);
    name[nlen] = '\0';
    if (nlen == 1 && (name[0] == 0 || name[0] == 1)) {
        name[0] = '\0';     /* "." and ".." */
    }
    if (SDL_strchr(name, ';') != NULL) {
        *SDL_strchr(name, ';') = '\0';
    }
    return 1;
}

/* A directory entry by name (case-insensitive, ";1" ignored) in the directory
 * at lba/size; returns 1 and its extent. */
static int FindEntry(Disc *d, u32 lba, u32 size, const char *name, u32 *outLba, u32 *outSize, int *isDir) {
    u8 *dir = ReadDir(d, lba, size), *p;
    u32 len;
    char entry[256];
    int found = 0;

    if (dir == NULL) {
        return 0;
    }
    for (p = dir; !found && Record(dir, size, p, &len, entry); p += len) {
        if (entry[0] != '\0' && SDL_strcasecmp(entry, name) == 0) {
            *outLba = LE32(p + 2);
            *outSize = LE32(p + 10);
            *isDir = (p[25] & 2) != 0;
            found = 1;
        }
    }
    SDL_free(dir);
    return found;
}

/* ---- inflate (MSZIP's deflate), into one growing output buffer ---------- */

typedef struct Bits {
    const u8 *p, *end;
    u32 buf, n;
    int over;       /* read past the end: the block is damaged */
} Bits;

static u32 Need(Bits *b, u32 n) {
    u32 v;

    while (b->n < n) {
        if (b->p < b->end) {
            b->buf |= (u32)*b->p++ << b->n;
        } else {
            b->over = 1;
        }
        b->n += 8;
    }
    v = b->buf & ((1u << n) - 1);
    b->buf >>= n;
    b->n -= n;
    return v;
}

typedef struct Huff {
    u16 count[16], symbol[320];
} Huff;

static void Build(Huff *h, const u8 *lengths, u32 n) {
    u16 offs[16];
    u32 i;

    SDL_zeroa(h->count);
    for (i = 0; i < n; i++) {
        h->count[lengths[i]]++;
    }
    h->count[0] = 0;
    for (offs[1] = 0, i = 1; i < 15; i++) {
        offs[i + 1] = (u16)(offs[i] + h->count[i]);
    }
    for (i = 0; i < n; i++) {
        if (lengths[i]) {
            h->symbol[offs[lengths[i]]++] = (u16)i;
        }
    }
}

static int Decode(Bits *b, const Huff *h) {
    int code = 0, first = 0, index = 0, len;

    for (len = 1; len < 16; len++) {
        int count = h->count[len];

        code |= (int)Need(b, 1);
        if (b->over) {
            return -1;
        }
        if (code - count < first) {
            return h->symbol[index + (code - first)];
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}

/* The output: a folder's blocks one after another (a block may refer back
 * into the ones before it). `limit` is where the current block must end. */
typedef struct Out {
    u8 *data;
    u32 size, cap, limit;
} Out;

static int Put(Out *o, u8 v) {
    if (o->size >= o->limit) {
        return 0;
    }
    if (o->size == o->cap) {
        u32 cap = o->cap ? o->cap * 2 : 1 << 20;
        u8 *grown;

        if (o->cap >= 0x80000000u) {
            return 0;
        }
        if ((grown = SDL_realloc(o->data, cap)) == NULL) {
            return 0;
        }
        o->data = grown;
        o->cap = cap;
    }
    o->data[o->size++] = v;
    return 1;
}

static const u16 s_lenBase[] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
                                 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const u8 s_lenExtra[] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const u16 s_distBase[] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                  1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const u8 s_distExtra[] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11,
                                  12, 12, 13, 13 };

static int Codes(Bits *b, Out *o, const Huff *lit, const Huff *dist) {
    for (;;) {
        int sym = Decode(b, lit);

        if (sym < 0) {
            return 0;
        }
        if (sym < 256) {
            if (!Put(o, (u8)sym)) {
                return 0;
            }
        } else if (sym == 256) {
            return 1;
        } else {
            u32 len, d, i;

            sym -= 257;
            if (sym >= 29) {
                return 0;
            }
            len = s_lenBase[sym] + Need(b, s_lenExtra[sym]);
            sym = Decode(b, dist);
            if (sym < 0 || sym >= 30) {
                return 0;
            }
            d = s_distBase[sym] + Need(b, s_distExtra[sym]);
            if (b->over || d > o->size) {
                return 0;
            }
            for (i = 0; i < len; i++) {
                if (!Put(o, o->data[o->size - d])) {
                    return 0;
                }
            }
        }
    }
}

/* One deflate stream (to its final block), appended to `o`; earlier output
 * is the history it may refer back to. */
static int Inflate(const u8 *src, u32 n, Out *o) {
    Bits b = { src, src + n, 0, 0, 0 };
    int last;

    do {
        u32 type;

        last = (int)Need(&b, 1);
        type = Need(&b, 2);
        if (b.over) {
            return 0;
        }
        if (type == 0) {
            u32 len, i;

            b.buf = 0;
            b.n = 0;
            if (b.end - b.p < 4) {
                return 0;
            }
            len = LE16(b.p);
            if ((u16)~LE16(b.p + 2) != len) {
                return 0;
            }
            b.p += 4;
            if ((u32)(b.end - b.p) < len) {
                return 0;
            }
            for (i = 0; i < len; i++) {
                if (!Put(o, *b.p++)) {
                    return 0;
                }
            }
        } else if (type == 1 || type == 2) {
            u8 lengths[320];
            Huff lit, dist;
            u32 nlen = 288, ndist = 30, i;

            if (type == 1) {
                for (i = 0; i < 144; i++) lengths[i] = 8;
                for (; i < 256; i++) lengths[i] = 9;
                for (; i < 280; i++) lengths[i] = 7;
                for (; i < 288; i++) lengths[i] = 8;
                Build(&lit, lengths, 288);
                for (i = 0; i < 30; i++) lengths[i] = 5;
                Build(&dist, lengths, 30);
            } else {
                static const u8 order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
                u8 cl[19] = { 0 };
                Huff clh;
                u32 ncode;

                nlen = Need(&b, 5) + 257;
                ndist = Need(&b, 5) + 1;
                ncode = Need(&b, 4) + 4;
                for (i = 0; i < ncode; i++) {
                    cl[order[i]] = (u8)Need(&b, 3);
                }
                Build(&clh, cl, 19);
                for (i = 0; i < nlen + ndist;) {
                    int sym = Decode(&b, &clh);
                    u32 rep = 0;
                    u8 v = 0;

                    if (sym < 0) {
                        return 0;
                    }
                    if (sym < 16) {
                        lengths[i++] = (u8)sym;
                        continue;
                    }
                    if (sym == 16) {
                        if (i == 0) {
                            return 0;
                        }
                        v = lengths[i - 1];
                        rep = 3 + Need(&b, 2);
                    } else if (sym == 17) {
                        rep = 3 + Need(&b, 3);
                    } else {
                        rep = 11 + Need(&b, 7);
                    }
                    while (rep-- && i < nlen + ndist) {
                        lengths[i++] = v;
                    }
                }
                if (b.over) {
                    return 0;
                }
                Build(&lit, lengths, nlen);
                Build(&dist, lengths + nlen, ndist);
            }
            if (!Codes(&b, o, &lit, &dist)) {
                return 0;
            }
        } else {
            return 0;
        }
    } while (!last);
    return 1;
}

/* ---- the destination folder ------------------------------------------------ */

static int Cancelled(SDL_AtomicInt *cancel) {
    return cancel != NULL && SDL_GetAtomicInt(cancel) != 0;
}

static SDL_EnumerationResult SDLCALL RemoveEntry(void *userdata, const char *dir, const char *name) {
    char *path = NULL;

    (void)userdata;
    if (SDL_asprintf(&path, "%s%s", dir, name) > 0) {
        SDL_PathInfo info;

        if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
            SDL_EnumerateDirectory(path, RemoveEntry, NULL);
        }
        SDL_RemovePath(path);
    }
    SDL_free(path);
    return SDL_ENUM_CONTINUE;
}

/* Delete a folder this import made (a .part or .old one), and all in it. */
static void RemoveTree(const char *path) {
    SDL_PathInfo info;

    if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
        SDL_EnumerateDirectory(path, RemoveEntry, NULL);
    }
    SDL_RemovePath(path);
}

/* An import fills <dest>.part; only a finished one takes dest's place, so a
 * failed or cancelled import leaves the last good copy as it was. */
static int Finish(const char *part, const char *dest, int ok, char *err, size_t errLen) {
    char old[1200];
    SDL_PathInfo info;

    if (!ok) {
        RemoveTree(part);
        return 0;
    }
    SDL_snprintf(old, sizeof(old), "%s.old", dest);
    RemoveTree(old);
    if (SDL_GetPathInfo(dest, &info) && !SDL_RenamePath(dest, old)) {
        SDL_snprintf(err, errLen, "could not replace %s: %s", dest, SDL_GetError());
        RemoveTree(part);
        return 0;
    }
    if (!SDL_RenamePath(part, dest)) {
        SDL_snprintf(err, errLen, "could not finish %s: %s", dest, SDL_GetError());
        SDL_RenamePath(old, dest);
        RemoveTree(part);
        return 0;
    }
    RemoveTree(old);
    return 1;
}

/* ---- the cabinet --------------------------------------------------------- */

static int SaveFile(const char *dir, const char *name, const u8 *data, u32 size) {
    char path[1200];

    SDL_snprintf(path, sizeof(path), "%s/%s", dir, name);
    return SDL_SaveFile(path, data, size);
}

/* Extract every file of the cabinet (read whole into memory) to `dest`. A
 * single cabinet compressed with MSZIP, as the PC CD's is. */
static int ExtractCab(const u8 *cab, u32 size, const char *dest, SDL_AtomicInt *progress, SDL_AtomicInt *cancel,
                      char *err, size_t errLen) {
    u32 coffFiles, i, f, first, folderSize = 8, dataReserve = 0;
    u16 nFolders, nFiles, flags;

    if (size < 36 || SDL_memcmp(cab, "MSCF", 4) != 0) {
        SDL_snprintf(err, errLen, "HWVX.cab is not a cabinet");
        return 0;
    }
    coffFiles = LE32(cab + 16);
    nFolders = LE16(cab + 26);
    nFiles = LE16(cab + 28);
    flags = LE16(cab + 30);
    first = 36;
    if (flags & 3) {
        SDL_snprintf(err, errLen, "HWVX.cab is one part of a set: not the disc's");
        return 0;
    }
    if (flags & 4) {    /* reserved space in the header, each folder and each block */
        if (size < 40) {
            goto damaged;
        }
        folderSize += cab[38];
        dataReserve = cab[39];
        first = 40 + LE16(cab + 36);
    }
    for (i = 0; i < nFolders; i++) {
        u32 at = first + i * folderSize, data, nData, type, k, p;
        Out out = { NULL, 0, 0, 0 };

        if (at > size || size - at < 8) {
            goto damaged;
        }
        data = LE32(cab + at);
        nData = LE16(cab + at + 4);
        type = LE16(cab + at + 6) & 0xF;
        if (type != 1) {
            SDL_snprintf(err, errLen, "HWVX.cab: compression type %u (only MSZIP is read)", (unsigned)type);
            return 0;
        }
        /* the blocks: checksum, compressed and uncompressed sizes, the
         * reserve, then "CK" and up to 32 KB of deflate */
        for (k = 0, p = data; k < nData; k++) {
            u32 cb, unc, payload;

            if (p > size || size - p < 8 + dataReserve) {
                SDL_free(out.data);
                goto damaged;
            }
            cb = LE16(cab + p + 4);
            unc = LE16(cab + p + 6);
            payload = p + 8 + dataReserve;
            out.limit = out.size + unc;
            if (cb < 2 || cb > size - payload || unc > 32768 || cab[payload] != 'C' || cab[payload + 1] != 'K' ||
                !Inflate(cab + payload + 2, cb - 2, &out) || out.size != out.limit || Cancelled(cancel)) {
                SDL_free(out.data);
                if (Cancelled(cancel)) {
                    SDL_snprintf(err, errLen, "cancelled");
                    return 0;
                }
                SDL_snprintf(err, errLen, "HWVX.cab: a damaged block in folder %u", (unsigned)i);
                return 0;
            }
            p = payload + cb;
        }
        /* the files of this folder */
        for (f = 0, p = coffFiles; f < nFiles; f++) {
            u32 fsize, off, fold;
            size_t nameLen;
            const char *name;

            if (p > size || size - p < 17) {
                SDL_free(out.data);
                goto damaged;
            }
            fsize = LE32(cab + p);
            off = LE32(cab + p + 4);
            fold = LE16(cab + p + 8);
            name = (const char *)cab + p + 16;
            nameLen = SDL_strnlen(name, size - p - 16);
            if (nameLen == size - p - 16) {
                SDL_free(out.data);
                goto damaged;
            }
            if (fold == i) {
                if (!SafeName(name) || off > out.size || fsize > out.size - off) {
                    SDL_free(out.data);
                    SDL_snprintf(err, errLen, "HWVX.cab: a damaged file entry");
                    return 0;
                }
                if (!SaveFile(dest, name, out.data + off, fsize)) {
                    SDL_free(out.data);
                    SDL_snprintf(err, errLen, "could not write %s: %s", name, SDL_GetError());
                    return 0;
                }
                SDL_AddAtomicInt(progress, 1);
            }
            p += 16 + (u32)nameLen + 1;
        }
        SDL_free(out.data);
    }
    return 1;
damaged:
    SDL_snprintf(err, errLen, "HWVX.cab is damaged");
    return 0;
}

/* ---- the import ----------------------------------------------------------- */

/* Copy a file of the disc to `path`, a few sectors at a time (the movies are
 * hundreds of megabytes). */
static int CopyOut(Disc *d, u32 lba, u32 size, const char *path, SDL_AtomicInt *cancel) {
    static u8 chunk[64 * 2048];
    SDL_IOStream *out;
    u32 done = 0;

    if (!InImage(d, lba, size) || (out = SDL_IOFromFile(path, "wb")) == NULL) {
        return 0;
    }
    while (done < size) {
        u32 n = size - done < sizeof(chunk) ? size - done : (u32)sizeof(chunk);
        u32 sectors = (n + 2047) / 2048;

        if (Cancelled(cancel) || !ReadSectors(d, lba, sectors, chunk) || SDL_WriteIO(out, chunk, n) != n) {
            SDL_CloseIO(out);
            return 0;
        }
        lba += sectors;
        done += n;
    }
    return SDL_CloseIO(out);
}

int Port_ImportPCDisc(const char *image, const char *dest, SDL_AtomicInt *progress, SDL_AtomicInt *cancel, char *err,
                      size_t errLen) {
    Disc d;
    u8 pvd[2048];
    u32 root, rootSize, lba, fsize, fmv, fmvSize;
    int isDir, ok = 0;
    u8 *buf = NULL;
    char part[1200];

    if (!OpenDisc(image, &d)) {
        SDL_snprintf(err, errLen, "could not open the disc image");
        return 0;
    }
    SDL_snprintf(part, sizeof(part), "%s.part", dest);
    RemoveTree(part);
    if (!ReadSectors(&d, 16, 1, pvd) || pvd[0] != 1 || SDL_memcmp(pvd + 1, "CD001", 5) != 0) {
        SDL_snprintf(err, errLen, "not an ISO 9660 disc image");
        goto done;
    }
    root = LE32(pvd + 156 + 2);
    rootSize = LE32(pvd + 156 + 10);
    if (!FindEntry(&d, root, rootSize, "HWVX.cab", &lba, &fsize, &isDir) || isDir) {
        SDL_snprintf(err, errLen, "no HWVX.cab: not the Hot Wheels Velocity X PC disc");
        goto done;
    }
    if (!InImage(&d, lba, fsize) || (buf = SDL_malloc(fsize)) == NULL || !ReadExtent(&d, lba, fsize, buf)) {
        SDL_snprintf(err, errLen, "could not read HWVX.cab");
        goto done;
    }
    SDL_CreateDirectory(part);
    if (!ExtractCab(buf, fsize, part, progress, cancel, err, errLen)) {
        goto done;
    }
    SDL_free(buf);
    buf = NULL;
    /* the movies, which only the disc has */
    if (FindEntry(&d, root, rootSize, "FMV_", &fmv, &fmvSize, &isDir) && isDir) {
        static const char *const movies[] = { "thqLogo.bik", "BGlogo.bik", "hwboot.bik", "hw_intro.bik",
                                              "hw_end.bik", "hw_gal.bik", "hw_mark.bik" };
        char fmvDir[1200], path[1300];
        size_t m;

        SDL_snprintf(fmvDir, sizeof(fmvDir), "%s/FMV_", part);
        SDL_CreateDirectory(fmvDir);
        for (m = 0; m < SDL_arraysize(movies); m++) {
            if (FindEntry(&d, fmv, fmvSize, movies[m], &lba, &fsize, &isDir) && !isDir) {
                SDL_snprintf(path, sizeof(path), "%s/%s", fmvDir, movies[m]);
                if (!CopyOut(&d, lba, fsize, path, cancel)) {
                    SDL_snprintf(err, errLen, Cancelled(cancel) ? "cancelled" : "could not copy %s", movies[m]);
                    goto done;
                }
                SDL_AddAtomicInt(progress, 1);
            }
        }
    }
    ok = 1;
done:
    SDL_free(buf);
    SDL_CloseIO(d.io);
    return Finish(part, dest, ok, err, errLen);
}

/* ---- the PS2 discs --------------------------------------------------------- */

/* The directories already copied (by first sector), so a disc whose folders
 * point back at themselves or each other can't make the copy go round. */
typedef struct Visited {
    u32 lba[256];
    u32 count;
} Visited;

/* Copy the directory at lba/size, and everything under it, into `dest`. */
static int CopyTree(Disc *d, u32 lba, u32 size, const char *dest, SDL_AtomicInt *progress, SDL_AtomicInt *cancel,
                    Visited *seen, char *err, size_t errLen) {
    u8 *dir, *p;
    u32 len, i;
    char name[256];
    int ok = 1;

    for (i = 0; i < seen->count; i++) {
        if (seen->lba[i] == lba) {
            return 1;
        }
    }
    if (seen->count == SDL_arraysize(seen->lba) || (dir = ReadDir(d, lba, size)) == NULL) {
        SDL_snprintf(err, errLen, "could not read a folder of the disc");
        return 0;
    }
    seen->lba[seen->count++] = lba;
    SDL_CreateDirectory(dest);
    for (p = dir; ok && Record(dir, size, p, &len, name); p += len) {
        char path[1200];

        if (name[0] == '\0') {
            continue;
        }
        if (!SafeName(name)) {
            SDL_snprintf(err, errLen, "a damaged name on the disc");
            ok = 0;
            break;
        }
        SDL_snprintf(path, sizeof(path), "%s/%s", dest, name);
        if (p[25] & 2) {
            ok = CopyTree(d, LE32(p + 2), LE32(p + 10), path, progress, cancel, seen, err, errLen);
        } else if (CopyOut(d, LE32(p + 2), LE32(p + 10), path, cancel)) {
            SDL_AddAtomicInt(progress, 1);
        } else {
            if (Cancelled(cancel)) {
                SDL_snprintf(err, errLen, "cancelled");
            } else {
                SDL_snprintf(err, errLen, "could not copy %s: %s", path, SDL_GetError());
            }
            ok = 0;
        }
    }
    SDL_free(dir);
    return ok;
}

int Port_ImportPS2Disc(const char *image, const char *prefDir, char *dest, size_t destLen, SDL_AtomicInt *progress,
                       SDL_AtomicInt *cancel, char *err, size_t errLen) {
    Disc d;
    u8 pvd[2048], head[2048];
    u32 root, rootSize, xps, xpsSize, eng, engSize, lba, fsize, version;
    int isDir, ok = 0;
    char part[1200];
    Visited *seen = NULL;

    if (!OpenDisc(image, &d)) {
        SDL_snprintf(err, errLen, "could not open the disc image");
        return 0;
    }
    if (!ReadSectors(&d, 16, 1, pvd) || pvd[0] != 1 || SDL_memcmp(pvd + 1, "CD001", 5) != 0) {
        SDL_snprintf(err, errLen, "not an ISO 9660 disc image");
        SDL_CloseIO(d.io);
        return 0;
    }
    root = LE32(pvd + 156 + 2);
    rootSize = LE32(pvd + 156 + 10);
    if (!FindEntry(&d, root, rootSize, "XPS", &xps, &xpsSize, &isDir) || !isDir ||
        !FindEntry(&d, xps, xpsSize, "ENGLISH", &eng, &engSize, &isDir) || !isDir ||
        !FindEntry(&d, eng, engSize, "HW_share.xps", &lba, &fsize, &isDir) || isDir || fsize < 12 ||
        !ReadSectors(&d, lba, 1, head)) {
        SDL_snprintf(err, errLen, "no XPS/ENGLISH/HW_share.xps: not a Hot Wheels Velocity X PS2 disc");
        SDL_CloseIO(d.io);
        return 0;
    }
    /* the archive version tells the release from the prototype (launcher.c) */
    version = LE32(head + 8);
    if (version != 183 && version != 177 && version != 179) {
        SDL_snprintf(err, errLen, "a PS2 disc of an unknown version (%u)", (unsigned)version);
        SDL_CloseIO(d.io);
        return 0;
    }
    SDL_snprintf(dest, destLen, "%s%s", prefDir, version == 183 ? "ps2" : "ps2beta");
    SDL_snprintf(part, sizeof(part), "%s.part", dest);
    RemoveTree(part);
    SDL_CreateDirectory(part);
    {
        char path[1200];

        SDL_snprintf(path, sizeof(path), "%s/XPS", part);
        SDL_CreateDirectory(path);
        SDL_strlcat(path, "/ENGLISH", sizeof(path));
        seen = SDL_calloc(1, sizeof(*seen));
        ok = seen != NULL && CopyTree(&d, eng, engSize, path, progress, cancel, seen, err, errLen);
    }
    SDL_free(seen);
    SDL_CloseIO(d.io);
    return Finish(part, dest, ok, err, errLen);
}

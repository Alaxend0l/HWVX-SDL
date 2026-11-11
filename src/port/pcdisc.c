/* Importing the PC release from its disc image (launcher.c).
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
} Disc;

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

/* The open disc behind `path` (.iso, .bin, or a .cue naming its .bin). */
static int OpenDisc(const char *path, Disc *d) {
    char bin[1200];
    u8 sync[16];
    size_t n = SDL_strlen(path);

    SDL_zerop(d);
    SDL_strlcpy(bin, path, sizeof(bin));
    if (n > 4 && SDL_strcasecmp(path + n - 4, ".cue") == 0) {
        char *cue = SDL_LoadFile(path, NULL), *q, *e;
        const char *slash = SDL_strrchr(path, '/');

        if (cue == NULL || (q = SDL_strstr(cue, "FILE \"")) == NULL || (e = SDL_strchr(q + 6, '"')) == NULL) {
            SDL_free(cue);
            return 0;
        }
        *e = '\0';
        SDL_snprintf(bin, sizeof(bin), "%.*s%s", slash ? (int)(slash - path + 1) : 0, path, q + 6);
        SDL_free(cue);
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
    return 1;
}

/* A directory entry by name (case-insensitive, ";1" ignored) in the directory
 * at lba/size; returns 1 and its extent. */
static int FindEntry(Disc *d, u32 lba, u32 size, const char *name, u32 *outLba, u32 *outSize, int *isDir) {
    u8 *dir = SDL_malloc(size), *p;
    int found = 0;

    if (dir == NULL || !ReadExtent(d, lba, size, dir)) {
        SDL_free(dir);
        return 0;
    }
    for (p = dir; p < dir + size && !found;) {
        u32 len = p[0], nlen;
        char entry[256];

        if (len == 0) {     /* the rest of this sector is padding */
            p = dir + ((p - dir) / 2048 + 1) * 2048;
            continue;
        }
        nlen = p[32];
        SDL_memcpy(entry, p + 33, nlen);
        entry[nlen] = '\0';
        if (SDL_strchr(entry, ';') != NULL) {
            *SDL_strchr(entry, ';') = '\0';
        }
        if (SDL_strcasecmp(entry, name) == 0) {
            *outLba = LE32(p + 2);
            *outSize = LE32(p + 10);
            *isDir = (p[25] & 2) != 0;
            found = 1;
        }
        p += len;
    }
    SDL_free(dir);
    return found;
}

/* ---- inflate (MSZIP's deflate), into one growing output buffer ---------- */

typedef struct Bits {
    const u8 *p, *end;
    u32 buf, n;
} Bits;

static u32 Need(Bits *b, u32 n) {
    u32 v;

    while (b->n < n) {
        b->buf |= (u32)(b->p < b->end ? *b->p++ : 0) << b->n;
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
        if (code - count < first) {
            return h->symbol[index + (code - first)];
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}

typedef struct Out {
    u8 *data;
    u32 size, cap;
} Out;

static int Put(Out *o, u8 v) {
    if (o->size == o->cap) {
        u32 cap = o->cap ? o->cap * 2 : 1 << 20;
        u8 *grown = SDL_realloc(o->data, cap);
        if (grown == NULL) {
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
            if (d > o->size) {
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
    Bits b = { src, src + n, 0, 0 };
    int last;

    do {
        u32 type;

        last = (int)Need(&b, 1);
        type = Need(&b, 2);
        if (type == 0) {
            u32 len, i;

            b.buf = 0;
            b.n = 0;
            if (b.p + 4 > b.end) {
                return 0;
            }
            len = LE16(b.p);
            b.p += 4;
            for (i = 0; i < len && b.p < b.end; i++) {
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

/* ---- the cabinet --------------------------------------------------------- */

static int SaveFile(const char *dir, const char *name, const u8 *data, u32 size) {
    char path[1200];

    SDL_snprintf(path, sizeof(path), "%s/%s", dir, name);
    return SDL_SaveFile(path, data, size);
}

/* Extract every file of the cabinet (read whole into memory) to `dest`. */
static int ExtractCab(const u8 *cab, u32 size, const char *dest, SDL_AtomicInt *progress, char *err, size_t errLen) {
    u32 coffFiles, i, f;
    u16 nFolders, nFiles, flags;
    u32 headerExtra = 0;

    if (size < 36 || SDL_memcmp(cab, "MSCF", 4) != 0) {
        SDL_snprintf(err, errLen, "HWVX.cab is not a cabinet");
        return 0;
    }
    coffFiles = LE32(cab + 16);
    nFolders = LE16(cab + 26);
    nFiles = LE16(cab + 28);
    flags = LE16(cab + 30);
    if (flags & 4) {
        headerExtra = LE16(cab + 36) + 4;   /* reserved fields; not used by this disc */
    }
    for (i = 0; i < nFolders; i++) {
        const u8 *folder = cab + 36 + headerExtra + i * 8;
        u32 data = LE32(folder), nData = LE16(folder + 4), type = LE16(folder + 6) & 0xF, k;
        Out out = { NULL, 0, 0 };
        const u8 *p;

        if (type != 1) {
            SDL_snprintf(err, errLen, "HWVX.cab: compression type %u (only MSZIP is read)", (unsigned)type);
            return 0;
        }
        for (k = 0, p = cab + data; k < nData; k++) {
            u32 cb = LE16(p + 4);

            if (p + 8 + cb > cab + size || p[8] != 'C' || p[9] != 'K' || !Inflate(p + 10, cb - 2, &out)) {
                SDL_free(out.data);
                SDL_snprintf(err, errLen, "HWVX.cab: a damaged block in folder %u", (unsigned)i);
                return 0;
            }
            p += 8 + cb;
        }
        /* the files of this folder */
        for (f = 0, p = cab + coffFiles; f < nFiles; f++) {
            u32 fsize = LE32(p), off = LE32(p + 4), fold = LE16(p + 8);
            const char *name = (const char *)p + 16;

            if (fold == i) {
                if (off + fsize > out.size || !SaveFile(dest, name, out.data + off, fsize)) {
                    SDL_free(out.data);
                    SDL_snprintf(err, errLen, "could not write %s: %s", name, SDL_GetError());
                    return 0;
                }
                SDL_AddAtomicInt(progress, 1);
            }
            p += 16 + SDL_strlen(name) + 1;
        }
        SDL_free(out.data);
    }
    return 1;
}

/* ---- the import ----------------------------------------------------------- */

int Port_ImportPCDisc(const char *image, const char *dest, SDL_AtomicInt *progress, char *err, size_t errLen) {
    Disc d;
    u8 pvd[2048];
    u32 root, rootSize, lba, fsize, fmv, fmvSize;
    int isDir, ok = 0;
    u8 *buf = NULL;

    if (!OpenDisc(image, &d)) {
        SDL_snprintf(err, errLen, "could not open the disc image");
        return 0;
    }
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
    SDL_CreateDirectory(dest);
    buf = SDL_malloc(fsize);
    if (buf == NULL || !ReadExtent(&d, lba, fsize, buf)) {
        SDL_snprintf(err, errLen, "could not read HWVX.cab");
        goto done;
    }
    if (!ExtractCab(buf, fsize, dest, progress, err, errLen)) {
        goto done;
    }
    SDL_free(buf);
    buf = NULL;
    /* the movies, which only the disc has */
    if (FindEntry(&d, root, rootSize, "FMV_", &fmv, &fmvSize, &isDir) && isDir) {
        static const char *const movies[] = { "thqLogo.bik", "BGlogo.bik", "hwboot.bik", "hw_intro.bik",
                                              "hw_end.bik", "hw_gal.bik", "hw_mark.bik" };
        char fmvDir[1200];
        size_t m;

        SDL_snprintf(fmvDir, sizeof(fmvDir), "%s/FMV_", dest);
        SDL_CreateDirectory(fmvDir);
        for (m = 0; m < SDL_arraysize(movies); m++) {
            if (FindEntry(&d, fmv, fmvSize, movies[m], &lba, &fsize, &isDir) && !isDir &&
                (buf = SDL_malloc(fsize)) != NULL) {
                if (ReadExtent(&d, lba, fsize, buf)) {
                    SaveFile(fmvDir, movies[m], buf, fsize);
                }
                SDL_free(buf);
                buf = NULL;
                SDL_AddAtomicInt(progress, 1);
            }
        }
    }
    ok = 1;
done:
    SDL_free(buf);
    SDL_CloseIO(d.io);
    return ok;
}

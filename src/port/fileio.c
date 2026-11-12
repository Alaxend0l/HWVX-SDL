/* Writing files that must survive a crash or power loss (the save store,
 * game/Port_Saves.c): the data reaches the disk before the call returns, and
 * a file is replaced by renaming over it, so it is wholly old or wholly new. */
#include "port_internal.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static WCHAR *Wide(const char *utf8) {
    return (WCHAR *)SDL_iconv_string("UTF-16LE", "UTF-8", utf8, SDL_strlen(utf8) + 1);
}

int Port_WriteFileSynced(const char *path, const void *data, size_t size) {
    WCHAR *w = Wide(path);
    HANDLE h;
    DWORD done = 0;
    int ok;

    if (w == NULL) {
        return 0;
    }
    h = CreateFileW(w, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    SDL_free(w);
    if (h == INVALID_HANDLE_VALUE) {
        return SDL_SetError("can't create %s (%lu)", path, (unsigned long)GetLastError());
    }
    ok = WriteFile(h, data, (DWORD)size, &done, NULL) && done == (DWORD)size && FlushFileBuffers(h);
    if (!ok) {
        SDL_SetError("can't write %s (%lu)", path, (unsigned long)GetLastError());
    }
    if (!CloseHandle(h)) {
        ok = 0;
    }
    return ok;
}

int Port_ReplaceFile(const char *from, const char *to) {
    WCHAR *wf = Wide(from), *wt = Wide(to);
    int ok = wf != NULL && wt != NULL &&
             MoveFileExW(wf, wt, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);

    if (!ok) {
        SDL_SetError("can't rename %s to %s (%lu)", from, to, (unsigned long)GetLastError());
    }
    SDL_free(wf);
    SDL_free(wt);
    return ok;
}

static int IsRealFolder(const char *path) {
    WCHAR *w = Wide(path);
    DWORD attr = w != NULL ? GetFileAttributesW(w) : INVALID_FILE_ATTRIBUTES;

    SDL_free(w);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) &&
           !(attr & FILE_ATTRIBUTE_REPARSE_POINT);
}

#else
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

int Port_WriteFileSynced(const char *path, const void *data, size_t size) {
    const char *p = data;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int ok = 1;

    if (fd < 0) {
        return SDL_SetError("can't create %s: %s", path, strerror(errno));
    }
    while (size > 0) {
        ssize_t n = write(fd, p, size);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            ok = SDL_SetError("can't write %s: %s", path, strerror(errno));
            break;
        }
        p += n;
        size -= (size_t)n;
    }
    if (ok && fsync(fd) != 0) {
        ok = SDL_SetError("can't sync %s: %s", path, strerror(errno));
    }
    if (close(fd) != 0 && ok) {
        ok = SDL_SetError("can't close %s: %s", path, strerror(errno));
    }
    return ok;
}

/* rename(2) replaces `to` atomically; syncing the folder makes the new name
 * itself survive a power loss. */
int Port_ReplaceFile(const char *from, const char *to) {
    const char *slash = SDL_strrchr(to, '/');
    char *dir;
    int fd;

    if (rename(from, to) != 0) {
        return SDL_SetError("can't rename %s to %s: %s", from, to, strerror(errno));
    }
    dir = slash != NULL ? SDL_strndup(to, (size_t)(slash - to) + 1) : SDL_strdup(".");
    if (dir != NULL && (fd = open(dir, O_RDONLY)) >= 0) {
        fsync(fd);
        close(fd);
    }
    SDL_free(dir);
    return 1;
}

static int IsRealFolder(const char *path) {
    struct stat st;

    return lstat(path, &st) == 0 && S_ISDIR(st.st_mode);
}
#endif

/* SDL_GetPathInfo follows links; IsRealFolder doesn't, so a symlink or
 * junction inside the tree (or the tree itself) is removed as a link and
 * its target left alone. */
static SDL_EnumerationResult SDLCALL RemoveEntry(void *userdata, const char *dir, const char *name) {
    char *path = NULL;

    (void)userdata;
    if (SDL_asprintf(&path, "%s%s", dir, name) >= 0) {
        Port_RemoveTree(path);
        SDL_free(path);
    }
    return SDL_ENUM_CONTINUE;
}

void Port_RemoveTree(const char *path) {
    if (IsRealFolder(path)) {
        SDL_EnumerateDirectory(path, RemoveEntry, NULL);
    }
    SDL_RemovePath(path);
}

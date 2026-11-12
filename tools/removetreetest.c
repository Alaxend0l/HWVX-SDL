/* Port_RemoveTree (src/port/fileio.c) never follows a link: a tree holding a
 * symlink to a folder, and a symlink to a file, is deleted while the links'
 * targets stay; a link given as the tree itself is removed, its target kept.
 * Works in `removetreetest-out` under the current folder. POSIX only (77:
 * skipped elsewhere). */
#include <stdio.h>
#include "port_internal.h"

#ifdef _WIN32
int main(void) {
    return 77;
}
#else
#include <unistd.h>

static int s_fails;

static void Check(int ok, const char *what) {
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    s_fails += !ok;
}

static void Put(const char *path) {
    SDL_IOStream *io = SDL_IOFromFile(path, "w");

    SDL_WriteIO(io, "keep\n", 5);
    SDL_CloseIO(io);
}

int main(void) {
    const char *root = "removetreetest-out";

    Port_RemoveTree(root);
    SDL_CreateDirectory("removetreetest-out/victim/sub");
    SDL_CreateDirectory("removetreetest-out/del/inner");
    Put("removetreetest-out/victim/a.txt");
    Put("removetreetest-out/victim/sub/b.txt");
    Put("removetreetest-out/del/inner/f");
    Check(symlink("../../victim", "removetreetest-out/del/inner/dirlink") == 0, "made a folder link");
    Check(symlink("../victim/a.txt", "removetreetest-out/del/filelink") == 0, "made a file link");
    Check(symlink("victim", "removetreetest-out/toplink") == 0, "made a link to remove itself");

    Port_RemoveTree("removetreetest-out/del");
    Check(!SDL_GetPathInfo("removetreetest-out/del", NULL), "the tree is gone");
    Port_RemoveTree("removetreetest-out/toplink");
    Check(!SDL_GetPathInfo("removetreetest-out/toplink", NULL), "the link given is gone");
    Check(SDL_GetPathInfo("removetreetest-out/victim/a.txt", NULL), "the file link's target stays");
    Check(SDL_GetPathInfo("removetreetest-out/victim/sub/b.txt", NULL), "the folder link's target stays");

    Port_RemoveTree(root);
    Check(!SDL_GetPathInfo(root, NULL), "a plain tree is removed");
    return s_fails ? 1 : 0;
}
#endif

/* Viewer mode: the game as a preview and editing server for the data
 * browser (viewer.h; the protocol is docs/viewer-protocol.md).
 *
 * HWVX_VIEWER=<port> replaces the game's first state: Main_Init ends in
 * Port_ViewerStart, Main_Loop runs Port_ViewerFrame. No menus, no saves, no
 * world: the engine is up (file layer, pack loader, renderer), and each
 * frame the commands that arrived on the socket run on the game's own
 * thread, between frames, then the preview is drawn with the game's
 * renderer: a car as CarA_DrawScript assembles it (skinned body, rigid
 * parts, the breakable parts still on), any other pack's geometries side by
 * side, or the selected geometry alone.
 *
 * One client at a time, on 127.0.0.1 only. Requests and replies are one JSON
 * object a line. */
#include "port_internal.h"
#include "gx_render.h"
#include "viewer_internal.h"
#include "CarA.h"
#include "Motion.h"
#include "Port_Content.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET Sock;
#define SOCK_NONE INVALID_SOCKET
#define CloseSock closesocket
#define SockWouldBlock() (WSAGetLastError() == WSAEWOULDBLOCK)
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
typedef int Sock;
#define SOCK_NONE (-1)
#define CloseSock close
#define SockWouldBlock() (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
#endif

#define PROTOCOL_VERSION 1
#define MAX_DRAWN 512           /* geometries side by side */
#define MAX_BONES 64

VPack g_viewerPacks[VIEWER_MAX_PACKS];

const char *const g_viewerTypeNames[11] = {
    "car", "interface", "item", "link", "world", "collision", "world_texture", "geometry", "share", "audio", "music",
};

static struct {
    int on;                     /* -1: not asked yet */
    int port;
    Sock listener, client;
    char *in;
    size_t inLen, inCap;
    int quit;

    Camera *camera;
    f32 yaw, pitch, distance, fov;
    Vector4 target;
    int spin;                   /* turntable: yaw moves by itself */
    GXColor background;

    VPack *shown;               /* the pack drawn */
    char selected[256];         /* its selected node's id, or "" */
    Matrix4 *matrices;          /* scratch for a frame's draws (game heap) */
    u32 numMatrices;
    u32 drawn;                  /* draws queued in the last frame */

    char *shotPath;             /* a frame to save after the next one drawn */
    long long shotId;
    int shotHasId;
    int shotWait;               /* frames to draw before it */
    u32 frame;
} s = { -1 };

#define MATRIX_POOL (MAX_DRAWN + 2 * MAX_BONES + 8)

int Port_ViewerOn(void) {
    if (s.on < 0) {
        const char *e = SDL_getenv("HWVX_VIEWER");

        s.on = e != NULL && *e != '\0';
        s.port = s.on ? SDL_atoi(e) : 0;
    }
    return s.on;
}

/* ---- the socket ------------------------------------------------------------ */

static int SetNonBlocking(Sock sock) {
#ifdef _WIN32
    u_long on = 1;

    return ioctlsocket(sock, FIONBIO, &on) == 0;
#else
    int flags = fcntl(sock, F_GETFL, 0);

    return flags != -1 && fcntl(sock, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static int Listen(void) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int one = 1;

#ifdef _WIN32
    WSADATA data;

    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return 0;
    }
#endif
    s.client = SOCK_NONE;
    s.listener = socket(AF_INET, SOCK_STREAM, 0);
    if (s.listener == SOCK_NONE) {
        return 0;
    }
    setsockopt(s.listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);     /* this machine only */
    addr.sin_port = htons((u16)s.port);
    if (bind(s.listener, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(s.listener, 1) != 0 ||
        !SetNonBlocking(s.listener) || getsockname(s.listener, (struct sockaddr *)&addr, &len) != 0) {
        CloseSock(s.listener);
        s.listener = SOCK_NONE;
        return 0;
    }
    s.port = ntohs(addr.sin_port);
    return 1;
}

static void DropClient(void) {
    if (s.client != SOCK_NONE) {
        CloseSock(s.client);
        s.client = SOCK_NONE;
    }
    s.inLen = 0;
}

static void SendAll(const char *data, size_t len) {
    u64 start = SDL_GetTicks();

    while (len > 0 && s.client != SOCK_NONE) {
        long n = (long)send(s.client, data, len, 0);

        if (n > 0) {
            data += n;
            len -= (size_t)n;
        } else if (SockWouldBlock() && SDL_GetTicks() - start < 5000) {
            SDL_Delay(1);       /* the client is slow to read: wait for it */
        } else {
            DropClient();
        }
    }
}

/* A reply or event: the writer's object, on one line. */
static void SendJson(JsonWriter *w) {
    size_t i, n = 0;
    int inString = 0;

    if (w->failed || w->buf == NULL) {
        static const char oom[] = "{\"ok\":false,\"error\":\"out of memory\"}\n";

        SendAll(oom, sizeof(oom) - 1);
        return;
    }
    /* the writer indents; a line's breaks and indents go (strings keep theirs escaped) */
    for (i = 0; i < w->len; i++) {
        char c = w->buf[i];

        if (inString) {
            if (c == '\\' && i + 1 < w->len) {
                w->buf[n++] = c;
                c = w->buf[++i];
            } else if (c == '"') {
                inString = 0;
            }
        } else if (c == '"') {
            inString = 1;
        } else if (c == '\n') {
            while (i + 1 < w->len && w->buf[i + 1] == ' ') {
                i++;
            }
            continue;
        }
        w->buf[n++] = c;
    }
    w->buf[n++] = '\n';     /* over the writer's own last byte or its spare one */
    SendAll(w->buf, n);
}

/* ---- packs ----------------------------------------------------------------- */

static VPack *FindPack(const char *name) {
    int i;
    char full[200];

    if (name == NULL) {
        return s.shown;
    }
    if (SDL_strncmp(name, "base://", 7) == 0) {
        name += 7;
    }
    for (i = 0; i < VIEWER_MAX_PACKS; i++) {
        VPack *vp = &g_viewerPacks[i];

        SDL_snprintf(full, sizeof(full), "%s/%s", vp->archiveName, vp->name);
        if (vp->used && SDL_strcasecmp(full, name) == 0) {
            return vp;
        }
    }
    return NULL;
}

static void PackTypeName(int type, char *out, size_t size) {
    if (type >= 0 && type < 11) {
        SDL_strlcpy(out, g_viewerTypeNames[type], size);
    } else {
        SDL_snprintf(out, size, "type%d", type);
    }
}

/* A pack file's packs, from its table of contents, read as the loader does. */
typedef struct ListScan {
    JsonWriter *w;
    size_t rootLen;
    int count;
} ListScan;

static const char *const s_packExts[] = { ".xgc", ".xpc", ".xps" };

/* A pack file's table of contents, read as the loader does (DataPack_LoadFile):
 * its entries (SDL_free them), the file left open in *fd. NULL if it isn't one. */
static FileTOCEntry *ReadToc(const char *archive, s32 *fd, int *fmt, u32 *count) {
    u32 head[4], *whole;
    char file[200];

    SDL_snprintf(file, sizeof(file), "%s.xgc", archive);
    *fd = File_Open(0, file, 0, 0);
    if (*fd == -1) {
        return NULL;
    }
    *fmt = Port_FileFormat(*fd);
    if (File_Read(*fd, sizeof(head), head) > 0) {
        PortData_TOC(*fmt, head, 4);
        *count = head[3];
        if (head[0] == 0x020100A0 && head[1] == 0x040100AE && *count <= 0x10000 &&
            (whole = SDL_malloc(16 + *count * sizeof(FileTOCEntry))) != NULL) {
            File_Seek(*fd, 0, 0);
            if (File_Read(*fd, (s32)(16 + *count * sizeof(FileTOCEntry)), whole) > 0) {
                PortData_TOC(*fmt, whole, (16 + *count * (u32)sizeof(FileTOCEntry)) / 4);
                SDL_memmove(whole, whole + 4, *count * sizeof(FileTOCEntry));
                return (FileTOCEntry *)whole;
            }
            SDL_free(whole);
        }
    }
    File_Close(*fd);
    *fd = -1;
    return NULL;
}

static void ListArchive(ListScan *scan, const char *archive) {
    u32 count, i;
    FileTOCEntry *toc;
    char type[24], name[64];
    s32 fd;
    int fmt;

    toc = ReadToc(archive, &fd, &fmt, &count);
    if (toc == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        FileTOCEntry *e = &toc[i];
        VPack *vp;
        const char *last = SDL_strrchr(archive, '/');
        char full[200];

        PackTypeName((int)e->type, type, sizeof(type));
        SDL_snprintf(name, sizeof(name), "%s_%u", type, e->id);
        SDL_snprintf(full, sizeof(full), "%s/%s", last != NULL ? last + 1 : archive, name);
        vp = FindPack(full);
        Jw_BeginObject(scan->w, NULL, 1);
        Jw_String(scan->w, "archive", archive);
        Jw_String(scan->w, "name", name);
        Jw_String(scan->w, "type", type);
        Jw_Int(scan->w, "typeId", e->type);
        Jw_Int(scan->w, "id", e->id);
        Jw_String(scan->w, "release", fmt == PORT_DATA_PC ? "pc" : fmt == PORT_DATA_PS2 ? "ps2" : "gc");
        Jw_String(scan->w, "pack", full);
        Jw_Bool(scan->w, "loaded", vp != NULL);
        Jw_EndObject(scan->w);
        scan->count++;
    }
    SDL_free(toc);
    File_Close(fd);
}

/* The two share packs a pack's header names (its refs), read from the file
 * without loading it. 1 if the pack is there. */
static int PackRefs(const char *archive, int type, u32 id, DataPackRef refs[2]) {
    struct {
        u32 words[20];
        DataPackRef refs[2];
    } header;
    u32 count, i, tocId = Port_ContentPackId(archive, (u32)type, id);
    FileTOCEntry *toc;
    s32 fd;
    int fmt, found = 0;

    toc = ReadToc(archive, &fd, &fmt, &count);
    for (i = 0; toc != NULL && i < count && !found; i++) {
        if (toc[i].type == (u32)type && toc[i].id == tocId) {
            File_Seek(fd, (s32)(toc[i].offset + 0x10 + count * sizeof(FileTOCEntry)), 0);
            if (File_Read(fd, sizeof(header), &header) > 0) {
                PortData_PackHeader(fmt, header.words, 20, 2, sizeof(DataPackRef));
                SDL_memcpy(refs, header.refs, sizeof(header.refs));
                found = 1;
            }
        }
    }
    if (toc != NULL) {
        SDL_free(toc);
        File_Close(fd);
    }
    return found;
}

/* The share packs a pack uses, loaded before it, as the game has them
 * loaded before it asks for a car or a world (GameSpecificInterfaces.c): the
 * loader can't load one in the middle of another (its header is static, its
 * file shared), which it would do for a share pack not loaded yet. They stay
 * loaded (the game's slots keep the names' pointers: s_shareNames). */
static void LoadShares(const char *archive, int type, u32 id, int depth) {
    static char s_shareNames[24][sizeof(((DataPackRef *)0)->name) + 1];
    static u32 s_shareIds[24];
    static int s_numShares;
    DataPackRef refs[2];
    int r, k;

    if (depth > 3 || !PackRefs(archive, type, id, refs)) {
        return;
    }
    for (r = 0; r < 2; r++) {
        char name[sizeof(refs[r].name) + 1];

        SDL_memcpy(name, refs[r].name, sizeof(refs[r].name));
        name[sizeof(refs[r].name)] = '\0';
        if (name[0] == '\0' || (type == 8 && refs[r].id == id && SDL_strcmp(name, archive) == 0)) {
            continue;
        }
        for (k = 0; k < s_numShares; k++) {
            if (s_shareIds[k] == refs[r].id && SDL_strcmp(s_shareNames[k], name) == 0) {
                break;
            }
        }
        if (k < s_numShares || s_numShares >= (int)SDL_arraysize(s_shareNames)) {
            continue;
        }
        LoadShares(name, 8, refs[r].id, depth + 1);
        SDL_strlcpy(s_shareNames[s_numShares], name, sizeof(s_shareNames[0]));
        s_shareIds[s_numShares] = refs[r].id;
        if (FileStreaming_LoadDataPack(s_shareNames[s_numShares], 8, refs[r].id, 0) >= 0) {
            Port_Log("viewer: share pack %s %u loaded first", name, refs[r].id);
            s_numShares++;
        }
    }
}

static SDL_EnumerationResult SDLCALL ListEntry(void *userdata, const char *dir, const char *name) {
    ListScan *scan = userdata;
    size_t n = SDL_strlen(name), i;
    SDL_PathInfo info;
    char *path = NULL;

    if (name[0] == '.' || SDL_asprintf(&path, "%s%s", dir, name) < 0) {
        return SDL_ENUM_CONTINUE;
    }
    if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
        SDL_EnumerateDirectory(path, ListEntry, scan);
    } else {
        for (i = 0; i < SDL_arraysize(s_packExts); i++) {
            if (n > 4 && SDL_strcasecmp(name + n - 4, s_packExts[i]) == 0 && SDL_strlen(path) > scan->rootLen + 4) {
                /* the name the game asks for: from the data folder, no extension;
                 * the PS2 disc's XPS/ENGLISH/ is where dvd.c looks by itself */
                char *archive = path + scan->rootLen;

                while (*archive == '/' || *archive == '\\') {
                    archive++;
                }
                archive[SDL_strlen(archive) - 4] = '\0';
                if (SDL_strncasecmp(archive, "XPS/ENGLISH/", 12) == 0) {
                    archive += 12;
                }
                ListArchive(scan, archive);
            }
        }
    }
    SDL_free(path);
    return SDL_ENUM_CONTINUE;
}

static int CmdPacks(JsonWriter *w, char *err, size_t errSize) {
    ListScan scan = { w, SDL_strlen(g_port.dataDir), 0 };
    SDL_PathInfo info;

    if (!SDL_GetPathInfo(g_port.dataDir, &info) || info.type != SDL_PATHTYPE_DIRECTORY) {
        SDL_snprintf(err, errSize, "the packs of a disc image can't be listed: extract its files and pass the folder");
        return 0;
    }
    Jw_BeginArray(w, "packs", 0);
    SDL_EnumerateDirectory(g_port.dataDir, ListEntry, &scan);
    Jw_EndArray(w);
    return 1;
}

/* ---- the preview ----------------------------------------------------------- */

static Geometry *GeometryOf(VPack *vp, VNode *nd) {
    return (Geometry *)(GETP(vp->pack->base) + nd->off);
}

/* p, if [p, p + size) is inside the pack's blob; else NULL. The model's handles
 * are followed through this: a set can point a link anywhere (force). */
static void *InPack(VPack *vp, const void *p, size_t size) {
    uintptr_t base = (uintptr_t)GETP(vp->pack->base), q = (uintptr_t)p;

    if (p == NULL || q < base || q - base > vp->size || size > vp->size - (q - base)) {
        return NULL;
    }
    return (void *)p;
}

/* The car's model: CarAInfo/0's modelRef, as CarA.c reads it. */
static MotionAnim *CarModel(VPack *vp) {
    VNode *nd = vp->type == 0 ? Viewer_FindNode(vp, "CarAInfo/0") : NULL;
    CarAInfo *info;
    CarAModelRef *ref;

    if (nd == NULL || nd->kind != VN_TYPED || InPack(vp, GETP(vp->pack->base) + nd->off, sizeof(CarAInfo)) == NULL) {
        return NULL;
    }
    info = (CarAInfo *)(GETP(vp->pack->base) + nd->off);
    ref = InPack(vp, GETP(info->modelRef), sizeof(CarAModelRef));
    return ref != NULL ? InPack(vp, GETP(ref->data), sizeof(MotionAnim)) : NULL;
}

static Matrix4 *NextMatrix(void) {
    extern Matrix4 g_identity;
    Matrix4 *m = &s.matrices[s.numMatrices < MATRIX_POOL - 1 ? s.numMatrices++ : MATRIX_POOL - 1];

    memcpy(m, &g_identity, sizeof(Matrix4));
    return m;
}

/* The model's rest pose as bone matrices (the first key frame over the
 * skeleton's base pose), or NULL. */
static Matrix4 *ModelPose(VPack *vp, MotionAnim *model, u32 *numBones) {
    static u8 frames[MAX_BONES * 0x20] __attribute__((aligned(16)));
    CreatureSkeleton *skel = InPack(vp, GETP(model->skeleton), sizeof(CreatureSkeleton));
    Matrix4 *matrices;

    if (skel == NULL || skel->numBones == 0 || skel->numBones > MAX_BONES ||
        InPack(vp, GETP(skel->nodes), skel->numBones * 0x20) == NULL || s.numMatrices + skel->numBones >= MATRIX_POOL) {
        return NULL;
    }
    memcpy(frames, GETP(skel->nodes), skel->numBones * 0x20);
    if (InPack(vp, GETP(model->keys), 4) != NULL) {
        Gfx_AnimExtractKeyFrame(GETP(model->keys), 0.0f, frames, (s32)skel->numBones);
    }
    matrices = &s.matrices[s.numMatrices];
    s.numMatrices += skel->numBones;
    Gfx_AnimTraverseHeirarchyToMatrices(frames, skel->numBones, (f32 *)matrices);
    *numBones = skel->numBones;
    return matrices;
}

static void Grow(f32 *min, f32 *max, const Matrix4 *m, const Geometry *g) {
    int i;

    for (i = 0; i < 3; i++) {
        f32 at = (&m->v[3].x)[i];
        f32 lo = at + g->bboxMin[i], hi = at + g->bboxMax[i];

        if (lo < min[i]) {
            min[i] = lo;
        }
        if (hi > max[i]) {
            max[i] = hi;
        }
    }
}

static void DrawOne(Geometry *g, Matrix4 *m, f32 *min, f32 *max) {
    if (s.drawn >= MAX_DRAWN) {
        return;
    }
    if (g->skipDraw != 0) {
        /* skinned: every bone where the geometry stands */
        u32 i;
        Matrix4 *bones;

        if (s.numMatrices + 8 >= MATRIX_POOL) {
            return;
        }
        bones = &s.matrices[s.numMatrices];
        for (i = 0; i < 8; i++) {
            *NextMatrix() = *m;
        }
        Gfx_DrawAnimGeometryAux(g, (f32 *)bones, 8, NULL);
    } else {
        Gfx_DrawGeometry_MatrixNotCached((s32)HND(g), (s32)HND(m));
    }
    Grow(min, max, m, g);
    s.drawn++;
}

static void DrawModel(VPack *vp, MotionAnim *model, f32 *min, f32 *max) {
    u32 numBones = 0, i;
    Matrix4 *bones = ModelPose(vp, model, &numBones);
    CarPartList *parts = InPack(vp, GETP(model->parts), sizeof(CarPartList));
    void *geoms = model->numGeoms < 0x10000 ? InPack(vp, GETP(model->geoms), model->numGeoms * 4) : NULL;
    MotionPart *rigid = model->numRigid < 0x10000 ? InPack(vp, GETP(model->rigid), model->numRigid * sizeof(MotionPart))
                                                   : NULL;
    CarPart *partList = parts != NULL && parts->count < 0x10000
                            ? InPack(vp, GETP(parts->parts), parts->count * sizeof(CarPart)) : NULL;

    if (bones == NULL) {
        return;
    }
    for (i = 0; geoms != NULL && i < model->numGeoms; i++) {
        Geometry *g = InPack(vp, GETP(GETP(model->geoms)[i]), sizeof(Geometry));

        if (g != NULL) {
            Gfx_DrawAnimGeometryAux(g, (f32 *)bones, numBones, NULL);
            Grow(min, max, &bones[0], g);
            s.drawn++;
        }
    }
    for (i = 0; rigid != NULL && i < model->numRigid; i++) {
        MotionPart *e = &rigid[i];

        if (InPack(vp, GETP(e->geom), sizeof(Geometry)) != NULL && e->mtxIndex < numBones) {
            Gfx_DrawGeometry_MatrixNotCached((s32)HND(GETP(e->geom)), (s32)HND(&bones[e->mtxIndex]));
            Grow(min, max, &bones[e->mtxIndex], GETP(e->geom));
            s.drawn++;
        }
    }
    /* the breakable parts as on an undamaged car (CarA.partVisMask 0) */
    for (i = 0; partList != NULL && i < parts->count; i++) {
        CarPart *part = &partList[i];

        if ((part->flags & 1) || InPack(vp, GETP(part->geom), sizeof(Geometry)) == NULL || part->mtxIndex >= numBones) {
            continue;
        }
        if (part->xform && InPack(vp, GETP(part->offset), sizeof(Vector4)) != NULL) {
            Matrix4 *m = NextMatrix();

            *m = bones[part->mtxIndex];
            XformVec(m, GETP(part->offset), &m->v[3]);
            Gfx_DrawGeometry_MatrixNotCached((s32)HND(GETP(part->geom)), (s32)HND(m));
        } else {
            Gfx_DrawGeometry_MatrixNotCached((s32)HND(GETP(part->geom)), (s32)HND(&bones[part->mtxIndex]));
        }
        Grow(min, max, &bones[part->mtxIndex], GETP(part->geom));
        s.drawn++;
    }
}

/* Queues the frame's draws; the bounds of what was drawn. 0: nothing. */
static int DrawScene(f32 *min, f32 *max) {
    VPack *vp = s.shown;
    VNode *sel;
    MotionAnim *model;
    u32 i;

    min[0] = min[1] = min[2] = 1e30f;
    max[0] = max[1] = max[2] = -1e30f;
    s.numMatrices = 0;
    s.drawn = 0;
    if (vp == NULL || vp->pack == NULL || GETP(vp->pack->base) == NULL) {
        return 0;
    }
    sel = s.selected[0] ? Viewer_FindNode(vp, s.selected) : NULL;
    if (sel != NULL && sel->kind == VN_GEOMETRY) {
        DrawOne(GeometryOf(vp, sel), NextMatrix(), min, max);
    } else if ((model = CarModel(vp)) != NULL) {
        DrawModel(vp, model, min, max);
    } else {
        /* every geometry, in rows, each on a square of the largest one's size */
        f32 cell = 0.0f, x = 0.0f, z = 0.0f;
        u32 n = 0, perRow;

        for (i = 0; i < vp->numNodes; i++) {
            if (vp->nodes[i].kind == VN_GEOMETRY) {
                f32 r = GeometryOf(vp, &vp->nodes[i])->bboxMin[3];

                if (r > cell && r < 1e6f) {
                    cell = r;
                }
                n++;
            }
        }
        if (n > MAX_DRAWN) {
            n = MAX_DRAWN;
        }
        perRow = (u32)ceil(sqrt((f64)n));
        cell = cell * 2.2f + 1.0f;
        for (i = 0, n = 0; i < vp->numNodes && n < MAX_DRAWN; i++) {
            if (vp->nodes[i].kind == VN_GEOMETRY) {
                Geometry *g = GeometryOf(vp, &vp->nodes[i]);
                Matrix4 *m = NextMatrix();

                m->v[3].x = x - g->center[0];
                m->v[3].y = -g->center[1];
                m->v[3].z = z - g->center[2];
                DrawOne(g, m, min, max);
                x += cell;
                if (++n % perRow == 0) {
                    x = 0.0f;
                    z += cell;
                }
            }
        }
    }
    return s.drawn != 0 && min[0] <= max[0];
}

/* The camera on what the last scene drew. */
static void Frame(const f32 *min, const f32 *max) {
    f32 dx = max[0] - min[0], dy = max[1] - min[1], dz = max[2] - min[2];
    f32 r = 0.5f * (f32)sqrt(dx * dx + dy * dy + dz * dz);

    s.target.x = 0.5f * (min[0] + max[0]);
    s.target.y = 0.5f * (min[1] + max[1]);
    s.target.z = 0.5f * (min[2] + max[2]);
    s.distance = r > 0.01f ? r / (f32)sin(0.5f * s.fov * 0.017453292f) : 10.0f;
}

static int s_frameNext;     /* frame the camera on the next scene drawn */

static void PlaceCamera(void) {
    extern Vector4 g_jBasis;
    Camera *c = s.camera;
    f32 cp = (f32)cos(s.pitch);

    if (s.pitch > 1.55f) {
        s.pitch = 1.55f;
    }
    if (s.pitch < -1.55f) {
        s.pitch = -1.55f;
    }
    if (s.distance < 0.05f) {
        s.distance = 0.05f;
    }
    c->pos.x = s.target.x + s.distance * cp * (f32)sin(s.yaw);
    c->pos.y = s.target.y + s.distance * (f32)sin(s.pitch);
    c->pos.z = s.target.z + s.distance * cp * (f32)cos(s.yaw);
    PSVECSubtract(&s.target, &c->pos, &c->dir);
    if (PSVECSquareMag(&c->dir)) {
        PSVECNormalize(&c->dir, &c->dir);
    }
    AccumulateVector(&c->dir, &g_jBasis, &c->up, -c->dir.y);
    if (PSVECSquareMag(&c->up)) {
        PSVECNormalize(&c->up, &c->up);
    }
    PSVECCrossProduct(&c->up, &c->dir, &c->right);
}

/* The mouse in the game's window: drag to orbit, right drag or shift to
 * pan, the wheel to zoom. Watched, so the event pump (vi.c) is as it was. */
static bool SDLCALL MouseWatch(void *userdata, SDL_Event *ev) {
    (void)userdata;
    if (ev->type == SDL_EVENT_MOUSE_MOTION && (ev->motion.state & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK))) {
        if ((ev->motion.state & SDL_BUTTON_RMASK) || (SDL_GetModState() & SDL_KMOD_SHIFT)) {
            f32 k = s.distance * 0.0015f;
            f32 sy = (f32)sin(s.yaw), cy = (f32)cos(s.yaw);

            s.target.x -= ev->motion.xrel * k * cy;
            s.target.z += ev->motion.xrel * k * sy;
            s.target.y += ev->motion.yrel * k;
        } else {
            s.yaw -= ev->motion.xrel * 0.008f;
            s.pitch += ev->motion.yrel * 0.008f;
            s.spin = 0;
        }
    } else if (ev->type == SDL_EVENT_MOUSE_WHEEL) {
        s.distance *= (f32)pow(0.9, ev->wheel.y);
    }
    return true;
}

/* ---- commands -------------------------------------------------------------- */

static void WriteCamera(JsonWriter *w) {
    Jw_BeginObject(w, "camera", 1);
    Jw_Float(w, "yaw", s.yaw * 57.29578f);
    Jw_Float(w, "pitch", s.pitch * 57.29578f);
    Jw_Float(w, "distance", s.distance);
    Jw_Float(w, "fov", s.fov);
    Jw_BeginArray(w, "target", 1);
    Jw_Float(w, NULL, s.target.x);
    Jw_Float(w, NULL, s.target.y);
    Jw_Float(w, NULL, s.target.z);
    Jw_EndArray(w);
    Jw_Bool(w, "spin", s.spin);
    Jw_EndObject(w);
}

static void WritePack(JsonWriter *w, VPack *vp) {
    char full[200], type[24];
    u32 i, counts[6] = { 0 };

    SDL_snprintf(full, sizeof(full), "%s/%s", vp->archiveName, vp->name);
    PackTypeName(vp->type, type, sizeof(type));
    Jw_String(w, "pack", full);
    Jw_String(w, "archive", vp->archive);
    Jw_String(w, "type", type);
    Jw_Int(w, "id", vp->id);
    Jw_String(w, "release", vp->pack->portFormat == PORT_DATA_PC ? "pc" : vp->pack->portFormat == PORT_DATA_PS2 ? "ps2" : "gc");
    Jw_Int(w, "size", vp->size);
    Jw_Int(w, "numNodes", vp->numNodes);
    for (i = 0; i < vp->numNodes; i++) {
        counts[vp->nodes[i].kind]++;
    }
    Jw_Int(w, "typed", counts[VN_TYPED]);
    Jw_Int(w, "geometries", counts[VN_GEOMETRY]);
    Jw_Int(w, "textures", vp->pack->numTextures);
    Jw_Int(w, "sounds", vp->pack->numSounds);
}

static void Unload(VPack *vp) {
    if (s.shown == vp) {
        s.shown = NULL;
        s.selected[0] = '\0';
    }
    Viewer_FreeGraph(vp);
    Viewer_ForgetPack(vp->pack);
    FileStreaming_ReleaseDataPack((u32)vp->slot, vp->type);
    FileStreaming_HeartBeat();      /* frees it now */
    vp->used = 0;
    vp->pack = NULL;
}

static int CmdLoad(JsonWriter *w, const JsonNode *req, char *err, size_t errSize) {
    extern u32 g_MaxStreamingDataPacks[11];
    const char *archive = Json_GetString(req, "archive", NULL), *last;
    const char *typeName = Json_GetString(req, "type", NULL);
    int type = (int)Json_GetInt(req, "type", -1), i;
    long long id = Json_GetInt(req, "id", -1);
    VPack *vp = NULL;
    DataPack *dp;
    char name[64], full[200];

    for (i = 0; typeName != NULL && i < 11; i++) {
        if (SDL_strcmp(typeName, g_viewerTypeNames[i]) == 0) {
            type = i;
        }
    }
    if (archive == NULL || type < 0 || type >= 11 || id < 0) {
        SDL_snprintf(err, errSize, "load takes archive, type (a name or number) and id, as packs lists them");
        return 0;
    }
    if (type == 7 || g_MaxStreamingDataPacks[type] == 0) {
        SDL_snprintf(err, errSize, "a %s pack is a world's quadrant: it loads with its world, not on its own",
                     g_viewerTypeNames[type]);
        return 0;
    }
    last = SDL_strrchr(archive, '/');
    last = last != NULL ? last + 1 : archive;
    SDL_snprintf(name, sizeof(name), "%s_%lld", g_viewerTypeNames[type], id);
    SDL_snprintf(full, sizeof(full), "%s/%s", last, name);
    if ((vp = FindPack(full)) != NULL) {
        Unload(vp);     /* again from the files: what an editor does after a rebuild */
    }
    for (i = 0, vp = NULL; i < VIEWER_MAX_PACKS && vp == NULL; i++) {
        if (!g_viewerPacks[i].used) {
            vp = &g_viewerPacks[i];
        }
    }
    if (vp == NULL) {
        SDL_snprintf(err, errSize, "%d packs are loaded: unload one first", VIEWER_MAX_PACKS);
        return 0;
    }
    memset(vp, 0, sizeof(*vp));
    SDL_strlcpy(vp->archive, archive, sizeof(vp->archive));
    SDL_strlcpy(vp->archiveName, last, sizeof(vp->archiveName));
    SDL_strlcpy(vp->name, name, sizeof(vp->name));
    vp->type = type;
    vp->id = (u32)id;
    /* one of each type's few slots: the packs of that type shown before make room */
    for (i = 0; i < VIEWER_MAX_PACKS; i++) {
        if (g_viewerPacks[i].used && g_viewerPacks[i].type == type && type != 8) {
            Unload(&g_viewerPacks[i]);
        }
    }
    LoadShares(vp->archive, type, (u32)id, 0);
    vp->slot = FileStreaming_LoadDataPack(vp->archive, type, (u32)id, 0);
    dp = vp->slot >= 0 ? FileStreaming_InitializeDataPack((u32)vp->slot, type, 1) : NULL;
    if (dp == NULL || GETP(dp->base) == NULL) {
        if (vp->slot >= 0) {
            FileStreaming_ReleaseDataPack((u32)vp->slot, type);
            FileStreaming_HeartBeat();
        }
        SDL_snprintf(err, errSize, "%s has no %s pack %lld, or it could not be loaded (see the game's log)", archive,
                     g_viewerTypeNames[type], id);
        return 0;
    }
    vp->used = 1;
    vp->pack = dp;
    vp->size = dp->dataSize;
    Viewer_BuildGraph(vp);
    s.shown = vp;
    s.selected[0] = '\0';
    s_frameNext = 1;
    Port_Log("viewer: loaded %s (%u nodes)", full, vp->numNodes);
    WritePack(w, vp);
    return 1;
}

static int NeedPack(const JsonNode *req, VPack **vp, char *err, size_t errSize) {
    const char *name = Json_GetString(req, "pack", NULL);

    *vp = FindPack(name);
    if (*vp == NULL) {
        SDL_snprintf(err, errSize, name != NULL ? "pack %s is not loaded" : "no pack is loaded", name);
        return 0;
    }
    return 1;
}

static int NeedNode(const JsonNode *req, VPack **vp, VNode **nd, char *err, size_t errSize) {
    const char *id = Json_GetString(req, "node", NULL);
    char prefix[200];

    if (!NeedPack(req, vp, err, errSize)) {
        return 0;
    }
    if (id == NULL) {
        SDL_snprintf(err, errSize, "which node?");
        return 0;
    }
    SDL_snprintf(prefix, sizeof(prefix), "base://%s/%s/", (*vp)->archiveName, (*vp)->name);
    if (SDL_strncmp(id, prefix, SDL_strlen(prefix)) == 0) {
        id += SDL_strlen(prefix);
    }
    *nd = Viewer_FindNode(*vp, id);
    if (*nd == NULL) {
        SDL_snprintf(err, errSize, "no node %s in %s/%s", id, (*vp)->archiveName, (*vp)->name);
        return 0;
    }
    return 1;
}

static void SaveShot(void);

/* One request; the reply is sent, unless it waits for a frame (screenshot). */
static void Dispatch(const JsonNode *req) {
    const char *cmd = Json_GetString(req, "cmd", "");
    const JsonNode *idNode = Json_Get(req, "seq");   /* echoed: "id" is a pack's */
    JsonWriter w;
    char err[400] = "";
    int ok = 1;
    VPack *vp;
    VNode *nd;

    Jw_Init(&w);
    Jw_BeginObject(&w, NULL, 0);
    if (idNode != NULL) {
        Jw_Node(&w, "seq", idNode);
    }
    Jw_Bool(&w, "ok", 1);
    if (SDL_strcmp(cmd, "hello") == 0) {
        static const char *const releases[] = { "gc", "pc", "ps2" };

        Jw_Int(&w, "protocol", PROTOCOL_VERSION);
        Jw_String(&w, "app", "HWVX-SDL viewer");
        Jw_String(&w, "data", g_port.dataDir);
        Jw_String(&w, "release", releases[Port_DataRelease() < 3 ? Port_DataRelease() : 0]);
        Jw_String(&w, "renderer", g_gxr->name);
        Viewer_WriteSchemaNames(&w, "schemas");
    } else if (SDL_strcmp(cmd, "packs") == 0) {
        ok = CmdPacks(&w, err, sizeof(err));
    } else if (SDL_strcmp(cmd, "load") == 0) {
        ok = CmdLoad(&w, req, err, sizeof(err));
    } else if (SDL_strcmp(cmd, "unload") == 0) {
        if ((ok = NeedPack(req, &vp, err, sizeof(err)))) {
            Unload(vp);
        }
    } else if (SDL_strcmp(cmd, "loaded") == 0) {
        int i;

        Jw_BeginArray(&w, "packs", 0);
        for (i = 0; i < VIEWER_MAX_PACKS; i++) {
            if (g_viewerPacks[i].used) {
                Jw_BeginObject(&w, NULL, 1);
                WritePack(&w, &g_viewerPacks[i]);
                Jw_Bool(&w, "shown", s.shown == &g_viewerPacks[i]);
                Jw_EndObject(&w);
            }
        }
        Jw_EndArray(&w);
    } else if (SDL_strcmp(cmd, "outline") == 0) {
        if ((ok = NeedPack(req, &vp, err, sizeof(err)))) {
            WritePack(&w, vp);
            Viewer_WriteOutline(&w, vp);
        }
    } else if (SDL_strcmp(cmd, "schema") == 0) {
        const JsonNode *schema = Viewer_Schema(Json_GetString(req, "type", NULL));

        if (schema != NULL) {
            Jw_Node(&w, "schema", schema);
        } else {
            SDL_snprintf(err, sizeof(err), "no schema %s", Json_GetString(req, "type", "(type?)"));
            ok = 0;
        }
    } else if (SDL_strcmp(cmd, "get") == 0) {
        if ((ok = NeedNode(req, &vp, &nd, err, sizeof(err)))) {
            Viewer_WriteNode(&w, vp, nd);
        }
    } else if (SDL_strcmp(cmd, "set") == 0) {
        if ((ok = NeedNode(req, &vp, &nd, err, sizeof(err)))) {
            const char *field = Json_GetString(req, "field", NULL);

            if (field == NULL || Json_Get(req, "value") == NULL) {
                SDL_snprintf(err, sizeof(err), "set takes node, field and value");
                ok = 0;
            } else {
                Jw_String(&w, "node", nd->id);
                Jw_String(&w, "field", field);
                ok = Viewer_SetField(&w, vp, nd, field, Json_Get(req, "value"), Json_GetBool(req, "force", 0), err,
                                     sizeof(err));
            }
        }
    } else if (SDL_strcmp(cmd, "select") == 0 || SDL_strcmp(cmd, "focus") == 0) {
        /* select: the node the preview is about (a geometry is drawn alone;
         * null: the whole pack again). focus: the same, and the camera on it. */
        const JsonNode *node = Json_Get(req, "node");

        if (node == NULL || node->type == JSON_NULL) {
            if ((ok = NeedPack(req, &vp, err, sizeof(err)))) {
                s.shown = vp;
                s.selected[0] = '\0';
            }
        } else if ((ok = NeedNode(req, &vp, &nd, err, sizeof(err)))) {
            s.shown = vp;
            SDL_strlcpy(s.selected, nd->id, sizeof(s.selected));
        }
        if (ok) {
            if (cmd[0] == 'f') {
                s_frameNext = 1;
            }
            if (s.selected[0]) {
                Jw_String(&w, "selected", s.selected);
            } else {
                Viewer_WriteNull(&w, "selected");
            }
        }
    } else if (SDL_strcmp(cmd, "camera") == 0 || SDL_strcmp(cmd, "orbit") == 0) {
        /* camera: absolute (any of yaw, pitch in degrees, distance, fov, target,
         * spin); orbit: steps (dyaw, dpitch in degrees, zoom a factor, pan in
         * screen directions, in units of the distance) */
        const JsonNode *target = Json_Get(req, "target"), *pan = Json_Get(req, "pan");

        if (cmd[0] == 'c') {
            s.yaw = (f32)Json_GetNumber(req, "yaw", s.yaw * 57.29578f) * 0.017453292f;
            s.pitch = (f32)Json_GetNumber(req, "pitch", s.pitch * 57.29578f) * 0.017453292f;
            s.distance = (f32)Json_GetNumber(req, "distance", s.distance);
            s.fov = (f32)Json_GetNumber(req, "fov", s.fov);
            if (s.fov < 5.0f || s.fov > 150.0f) {
                s.fov = 60.0f;
            }
            if (target != NULL && target->type == JSON_ARRAY && target->count == 3) {
                s.target.x = (f32)Json_At(target, 0)->number;
                s.target.y = (f32)Json_At(target, 1)->number;
                s.target.z = (f32)Json_At(target, 2)->number;
            }
            s_frameNext = 0;
        } else {
            s.yaw += (f32)Json_GetNumber(req, "dyaw", 0.0) * 0.017453292f;
            s.pitch += (f32)Json_GetNumber(req, "dpitch", 0.0) * 0.017453292f;
            s.distance *= (f32)Json_GetNumber(req, "zoom", 1.0);
            if (pan != NULL && pan->type == JSON_ARRAY && pan->count == 2) {
                f32 px = (f32)Json_At(pan, 0)->number * s.distance, py = (f32)Json_At(pan, 1)->number * s.distance;

                s.target.x += px * (f32)cos(s.yaw);
                s.target.z -= px * (f32)sin(s.yaw);
                s.target.y += py;
            }
        }
        s.spin = Json_GetBool(req, "spin", cmd[0] == 'c' ? s.spin : 0);
        WriteCamera(&w);
    } else if (SDL_strcmp(cmd, "background") == 0) {
        const JsonNode *c = Json_Get(req, "color");

        if (c != NULL && c->type == JSON_ARRAY && c->count == 3) {
            s.background.r = (u8)Json_At(c, 0)->number;
            s.background.g = (u8)Json_At(c, 1)->number;
            s.background.b = (u8)Json_At(c, 2)->number;
        } else {
            SDL_snprintf(err, sizeof(err), "background takes color: [r, g, b], 0 .. 255");
            ok = 0;
        }
    } else if (SDL_strcmp(cmd, "screenshot") == 0) {
        const char *path = Json_GetString(req, "path", NULL);

        if (path == NULL || s.shotPath != NULL) {
            SDL_snprintf(err, sizeof(err), path == NULL ? "screenshot takes path (.bmp or .png)" : "a screenshot is pending");
            ok = 0;
        } else {
            /* answered once the next frame is drawn, with what it shows */
            s.shotPath = SDL_strdup(path);
            s.shotHasId = idNode != NULL && idNode->type == JSON_NUMBER;
            s.shotId = s.shotHasId ? idNode->integer : 0;
            s.shotWait = 2;
            Jw_Free(&w);
            return;
        }
    } else if (SDL_strcmp(cmd, "status") == 0) {
        Jw_Int(&w, "frame", s.frame);
        Jw_Int(&w, "drawn", s.drawn);
        if (s.shown != NULL) {
            char full[200];

            SDL_snprintf(full, sizeof(full), "%s/%s", s.shown->archiveName, s.shown->name);
            Jw_String(&w, "shown", full);
        }
        if (s.selected[0]) {
            Jw_String(&w, "selected", s.selected);
        }
        WriteCamera(&w);
    } else if (SDL_strcmp(cmd, "quit") == 0) {
        s.quit = 1;
    } else {
        SDL_snprintf(err, sizeof(err), "unknown command %.40s", cmd);
        ok = 0;
    }
    if (!ok) {
        Jw_Free(&w);
        Jw_Init(&w);
        Jw_BeginObject(&w, NULL, 0);
        if (idNode != NULL) {
            Jw_Node(&w, "seq", idNode);
        }
        Jw_Bool(&w, "ok", 0);
        Jw_String(&w, "error", err);
    }
    Jw_EndObject(&w);
    SendJson(&w);
    Jw_Free(&w);
}

static void Poll(void) {
    char buf[4096];
    Sock sock;

    if (s.listener == SOCK_NONE) {
        return;
    }
    sock = accept(s.listener, NULL, NULL);
    if (sock != SOCK_NONE) {
        int one = 1;

        DropClient();   /* the newest connection is the one served */
        s.client = sock;
        SetNonBlocking(sock);
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
#ifdef SO_NOSIGPIPE
        setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&one, sizeof(one));
#endif
    }
    while (s.client != SOCK_NONE) {
        long n = (long)recv(s.client, buf, sizeof(buf), 0);
        char *nl;

        if (n == 0 || (n < 0 && !SockWouldBlock())) {
            DropClient();
            break;
        }
        if (n < 0) {
            break;
        }
        if (s.inLen + (size_t)n + 1 > s.inCap) {
            size_t cap = (s.inLen + (size_t)n + 1) * 2;
            char *p = cap <= (16u << 20) ? SDL_realloc(s.in, cap) : NULL;

            if (p == NULL) {
                DropClient();
                break;
            }
            s.in = p;
            s.inCap = cap;
        }
        memcpy(s.in + s.inLen, buf, (size_t)n);
        s.inLen += (size_t)n;
        while (s.client != SOCK_NONE && (nl = memchr(s.in, '\n', s.inLen)) != NULL) {
            size_t len = (size_t)(nl - s.in);
            char err[160];
            JsonNode *req = len > 0 ? Json_Parse(s.in, len, err, sizeof(err)) : NULL;

            if (req != NULL && req->type == JSON_OBJECT) {
                Dispatch(req);
            } else if (len > 1) {
                JsonWriter w;

                Jw_Init(&w);
                Jw_BeginObject(&w, NULL, 0);
                Jw_Bool(&w, "ok", 0);
                Jw_String(&w, "error", req != NULL ? "a request is a JSON object" : err);
                Jw_EndObject(&w);
                SendJson(&w);
                Jw_Free(&w);
            }
            Json_Free(req);
            if (s.inLen >= len + 1) {   /* (a dropped client emptied it) */
                s.inLen -= len + 1;
                memmove(s.in, nl + 1, s.inLen);
            }
        }
    }
}

/* The frame just drawn, before it is copied to the window (the copy clears it). */
static void SaveShot(void) {
    int w = 0, h = 0, ok = 0;
    SDL_Surface *surf;
    JsonWriter jw;
    char err[300] = "";
    size_t n = SDL_strlen(s.shotPath);

    SDL_GetWindowSizeInPixels(g_port.window, &w, &h);
    surf = w > 0 && h > 0 ? SDL_CreateSurface(w, h, SDL_PIXELFORMAT_XBGR8888) : NULL;
    if (surf == NULL || surf->pitch != w * 4 || !g_gxr->readEFB(surf->pixels)) {
        SDL_snprintf(err, sizeof(err), "the frame could not be read back from the %s renderer", g_gxr->name);
    } else if (n > 4 && SDL_strcasecmp(s.shotPath + n - 4, ".png") == 0) {
#if SDL_VERSION_ATLEAST(3, 4, 0)
        ok = SDL_SavePNG(surf, s.shotPath);
#else
        SDL_SetError("this build's SDL writes .bmp only");
#endif
    } else {
        ok = SDL_SaveBMP(surf, s.shotPath);
    }
    if (!ok && err[0] == '\0') {
        SDL_snprintf(err, sizeof(err), "%s: %s", s.shotPath, SDL_GetError());
    }
    Jw_Init(&jw);
    Jw_BeginObject(&jw, NULL, 0);
    if (s.shotHasId) {
        Jw_Int(&jw, "seq", s.shotId);
    }
    Jw_Bool(&jw, "ok", ok);
    if (ok) {
        Jw_String(&jw, "path", s.shotPath);
        Jw_Int(&jw, "width", w);
        Jw_Int(&jw, "height", h);
        Jw_Int(&jw, "drawn", s.drawn);
    } else {
        Jw_String(&jw, "error", err);
    }
    Jw_EndObject(&jw);
    SendJson(&jw);
    Jw_Free(&jw);
    SDL_DestroySurface(surf);
    SDL_free(s.shotPath);
    s.shotPath = NULL;
}

/* ---- the game's side ------------------------------------------------------- */

static s32 Heartbeat(void) {
    return 1;
}

s32 Port_ViewerStart(void) {
    extern s8 g_cameraManagerPause;
    extern s32 (*Heartbeat_Process)(void);
    const char *dir = SDL_getenv("HWVX_VIEWER_SCHEMAS");
    char *found = NULL;
    int n = 0;

    /* what a state's Init sets up for drawing (State_RenderTest.c is the game's own example) */
    Geometry_Init(1);
    Gfx_SetEnviroMapMode(1);
    ProcessManager_Init();
    ProcessManager_Add("TimeHandler", NULL, TimeHandler_Heartbeat, 0, 0);
    CameraManager_Init();
    s.camera = CameraManager_GetNewCamera();
    ViewManager_SetCamera(s.camera, 0);
    g_cameraManagerPause = 1;
    Gfx_SetProjectionMatrix(60.0f);
    Gfx_SetCameraMatrix(NULL, NULL, NULL, NULL);
    Gfx_SetWorldMatrix(NULL);
    Gfx_StateTextureBlendMode(2);
    DataPack_EnableLoadingSounds(0);    /* the sound manager isn't running: packs load without their samples */
    Heartbeat_Process = Heartbeat;  /* (Main_Loop runs Port_ViewerFrame instead) */

    s.matrices = Allocate(MATRIX_POOL * sizeof(Matrix4), 0, "");
    if (s.camera == NULL || s.matrices == NULL) {
        return 0;
    }
    s.yaw = 0.6f;
    s.pitch = 0.3f;
    s.distance = 12.0f;
    s.fov = 60.0f;
    s.background.r = 0x30;
    s.background.g = 0x34;
    s.background.b = 0x3C;

    /* the schemas: HWVX_VIEWER_SCHEMAS, or schemas/ by the program or the current folder */
    if (dir == NULL) {
        const char *base = SDL_GetBasePath();
        const char *tries[] = { "schemas/", "../schemas/" };
        size_t i;

        for (i = 0; i < SDL_arraysize(tries) && n == 0; i++) {
            SDL_free(found);
            found = NULL;
            if (base != NULL && SDL_asprintf(&found, "%s%s", base, tries[i]) > 0) {
                n = Viewer_LoadSchemas(found);
            }
        }
        if (n == 0) {
            n = Viewer_LoadSchemas("schemas/");
        }
    } else if (SDL_asprintf(&found, "%s/", dir) > 0) {
        n = Viewer_LoadSchemas(found);
    }
    SDL_free(found);
    Port_Log("viewer: %d schemas", n);

    if (!Listen()) {
        Port_Fatal("The viewer could not listen on 127.0.0.1:%d.", s.port);
    }
    SDL_AddEventWatch(MouseWatch, NULL);
    SDL_SetWindowTitle(g_port.window, "HWVX viewer");
    Port_Log("viewer: listening on 127.0.0.1:%d", s.port);
    return 1;
}

s32 Port_ViewerFrame(void) {
    extern GXColor m_clearColor;
    static const GXColor white = { 0xFF, 0xFF, 0xFF, 0xFF };
    static const GXColor grey = { 0x80, 0x80, 0x80, 0xFF };
    Vector4 lightDir = { -0.4f, -1.0f, -0.5f, 0.0f };
    f32 min[3], max[3];
    GXColor c;
    int any;

    Poll();
    if (s.quit) {
        return 0;
    }
    if (s.spin) {
        s.yaw += 0.01f;
    }
    ShowStats_StartFrame();
    ProcessManager_Heartbeat(0);
    FileStreaming_HeartBeat();
    ShaderHolder_Heartbeat();
    m_clearColor = s.background;
    Gfx_SetProjectionMatrix(s.fov);
    GetRenderWindow()->fov = s.fov;
    PlaceCamera();
    Camera_PrepareForRender(s.camera);
    ShowStats_BeginScene();
    c = grey;
    Gfx_SetAmbientColor(&c);
    c = white;
    Gfx_SetParallelLight(&c, &lightDir);
    any = DrawScene(min, max);
    if (any && s_frameNext) {
        /* the camera for the frame after this one: this one's draws are queued */
        Frame(min, max);
        s_frameNext = 0;
    }
    ProcessManager_Render(1);
    if (s.shotPath != NULL && s.shotWait > 0) {
        s.shotWait--;   /* a frame first: the camera framed, the buffers built */
    }
    if (s.shotPath != NULL && s.shotWait == 0) {
        Gfx_EndScene();
        SaveShot();
        Gfx_FlipBuffers();
        Performance_End();
    } else {
        ShowStats_EndScene();
    }
    s.frame++;
    return 1;
}

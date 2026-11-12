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
#define MAX_LINE (1u << 20)     /* a request line's longest */

#ifdef MSG_NOSIGNAL
#define SEND_FLAGS MSG_NOSIGNAL /* a closed client is an error, not SIGPIPE (Linux; SO_NOSIGPIPE elsewhere) */
#else
#define SEND_FLAGS 0
#endif

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
    size_t scan;                /* in[0 .. scan) has no line end */
    int authed;                 /* the client's hello had the token */
    char token[80];             /* HWVX_VIEWER_TOKEN, or one made up and logged */
    int quit;

    Camera *camera;
    f32 yaw, pitch, distance, fov;
    Vector4 target;
    int fly;                    /* free fly: the camera at flyPos, looking along yaw/pitch */
    Vector4 flyPos;
    f32 flySpeed;               /* units a second */
    int spin;                   /* turntable: yaw moves by itself */
    GXColor background;

    VPack *shown;               /* the pack drawn */
    char selected[256];         /* its selected node's id, or "" */
    s32 selectedIndex;          /* that node in shown->nodes, or -1 */
    Matrix4 *matrices;          /* scratch for a frame's draws (game heap) */
    u32 numMatrices;
    u32 drawn;                  /* draws queued in the last frame */

    /* what the last frame drew, for pick: each geometry and where */
    const Geometry *drawnGeom[MAX_DRAWN];
    f32 drawnAt[MAX_DRAWN][3];
    u32 numDrawnGeom;

    char *shotPath;             /* a frame to save after the next one drawn */
    long long shotId;
    int shotHasId;
    int shotWait;               /* frames to draw before it */
    u32 frame;
} s = { -1 };

#define MATRIX_POOL (MAX_DRAWN + 24 * MAX_BONES + 8)   /* a world's tile cars too */

int Port_ViewerHeadless(void) {
    static int headless = -1;

    if (headless < 0) {
        const char *e = SDL_getenv("HWVX_VIEWER_WINDOW");

        headless = Port_ViewerOn() && !(e != NULL && *e == '1');
    }
    return headless;
}

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
    s.scan = 0;
    s.authed = 0;
}

static void SendAll(const char *data, size_t len) {
    u64 start = SDL_GetTicks();

    while (len > 0 && s.client != SOCK_NONE) {
        long n = (long)send(s.client, data, len, SEND_FLAGS);

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

VPack *Viewer_ShownPack(void) {
    return s.shown;
}

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
static int PackRefs(const char *archive, int type, u32 id, DataPackRef refs[2], u32 *songBank) {
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
                if (songBank != NULL) {
                    *songBank = header.words[19];   /* songBankOffset: the pack brings a song */
                }
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
static char s_shareNames[24][sizeof(((DataPackRef *)0)->name) + 1];
static u32 s_shareIds[24];
static int s_numShares;

void Viewer_ForgetShares(void) {
    s_numShares = 0;
}

static void LoadShares(const char *archive, int type, u32 id, int depth) {
    DataPackRef refs[2];
    int r, k;

    if (depth > 3 || !PackRefs(archive, type, id, refs, NULL)) {
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
    uintptr_t base, q = (uintptr_t)p;

    if (vp == NULL) {
        return (void *)p;   /* the game's own (a world's tile car): trusted */
    }
    base = (uintptr_t)GETP(vp->pack->base);
    if (p == NULL || q < base || q - base > vp->size || size > vp->size - (q - base)) {
        return NULL;
    }
    return (void *)p;
}

/* The car's model: CarAInfo/0's modelRef, as CarA.c reads it. */
static MotionAnim *ModelOf(VPack *vp, CarAInfo *info) {
    CarAModelRef *ref = info != NULL ? InPack(vp, GETP(info->modelRef), sizeof(CarAModelRef)) : NULL;

    return ref != NULL ? InPack(vp, GETP(ref->data), sizeof(MotionAnim)) : NULL;
}

static MotionAnim *CarModel(VPack *vp) {
    VNode *nd = vp->type == 0 ? Viewer_FindNode(vp, "CarAInfo/0") : NULL;

    if (nd == NULL || nd->kind != VN_TYPED || InPack(vp, GETP(vp->pack->base) + nd->off, sizeof(CarAInfo)) == NULL) {
        return NULL;
    }
    return ModelOf(vp, (CarAInfo *)(GETP(vp->pack->base) + nd->off));
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

    if (s.numDrawnGeom < MAX_DRAWN) {
        s.drawnGeom[s.numDrawnGeom] = g;
        SDL_memcpy(s.drawnAt[s.numDrawnGeom], &m->v[3].x, sizeof(s.drawnAt[0]));
        s.numDrawnGeom++;
    }
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

static void DrawModel(VPack *vp, MotionAnim *model, f32 *min, f32 *max, const Matrix4 *at) {
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
    for (i = 0; at != NULL && i < numBones; i++) {
        Matrix4 b = bones[i];

        MultiplyMatrix(&b, (Matrix4 *)at, &bones[i]);
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

static int s_frameNext;     /* frame the camera on the next scene drawn */
static void PlaceCamera(void);

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
    s.numDrawnGeom = 0;
    if (vp == NULL || vp->pack == NULL || GETP(vp->pack->base) == NULL) {
        return 0;
    }
    sel = s.selected[0] && s.selectedIndex >= 0 && (u32)s.selectedIndex < vp->numNodes ? &vp->nodes[s.selectedIndex]
                                                                                       : NULL;
    if (sel != NULL && sel->kind == VN_GEOMETRY) {
        DrawOne(GeometryOf(vp, sel), NextMatrix(), min, max);
    } else if ((model = CarModel(vp)) != NULL) {
        DrawModel(vp, model, min, max, NULL);
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
    if (s.fly) {
        s.fly = 0;
        PlaceCamera();
        s.flyPos = s.camera->pos;
        s.fly = 1;
    }
}


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
    if (s.fly) {
        /* the same view as orbiting: looking along -(the orbit's offset) */
        c->pos = s.flyPos;
        c->dir.x = -cp * (f32)sin(s.yaw);
        c->dir.y = -(f32)sin(s.pitch);
        c->dir.z = -cp * (f32)cos(s.yaw);
        c->dir.w = 0.0f;
    } else {
        c->pos.x = s.target.x + s.distance * cp * (f32)sin(s.yaw);
        c->pos.y = s.target.y + s.distance * (f32)sin(s.pitch);
        c->pos.z = s.target.z + s.distance * cp * (f32)cos(s.yaw);
        PSVECSubtract(&s.target, &c->pos, &c->dir);
    }
    if (PSVECSquareMag(&c->dir)) {
        PSVECNormalize(&c->dir, &c->dir);
    }
    AccumulateVector(&c->dir, &g_jBasis, &c->up, -c->dir.y);
    if (PSVECSquareMag(&c->up)) {
        PSVECNormalize(&c->up, &c->up);
    }
    PSVECCrossProduct(&c->up, &c->dir, &c->right);
}

/* Free fly on or off, keeping the view: fly starts where the orbit camera
 * is; orbit goes round the point `distance` ahead. */
static void SetFly(int on) {
    if (on && !s.fly) {
        PlaceCamera();
        s.flyPos = s.camera->pos;
        if (s.flySpeed <= 0.0f) {
            s.flySpeed = s.distance > 1.0f ? s.distance : 20.0f;
        }
    } else if (!on && s.fly) {
        PlaceCamera();
        s.target.x = s.flyPos.x + s.camera->dir.x * s.distance;
        s.target.y = s.flyPos.y + s.camera->dir.y * s.distance;
        s.target.z = s.flyPos.z + s.camera->dir.z * s.distance;
    }
    s.fly = on;
    s.spin = 0;
}

/* A fly step: forward, right, up in units of flySpeed * dt (fast: x4). */
static void FlyMove(f32 fwd, f32 right, f32 up, f32 dt, int fast) {
    extern Vector4 g_jBasis;
    Camera *c = s.camera;
    f32 k = s.flySpeed * dt * (fast ? 4.0f : 1.0f);

    PlaceCamera();
    /* the camera's right is the screen's left (Gfx_SetCameraMatrix) */
    s.flyPos.x += (c->dir.x * fwd - c->right.x * right) * k + g_jBasis.x * up * k;
    s.flyPos.y += (c->dir.y * fwd - c->right.y * right) * k + g_jBasis.y * up * k;
    s.flyPos.z += (c->dir.z * fwd - c->right.z * right) * k + g_jBasis.z * up * k;
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
            f32 k = s.fly ? -0.004f : 0.008f;  /* fly: look where the pointer goes */

            s.yaw -= ev->motion.xrel * k;
            s.pitch += ev->motion.yrel * k;
            s.spin = 0;
        }
    } else if (ev->type == SDL_EVENT_MOUSE_WHEEL) {
        if (s.fly) {
            s.flySpeed *= (f32)pow(1.25, ev->wheel.y);
        } else {
            s.distance *= (f32)pow(0.9, ev->wheel.y);
        }
    }
    return true;
}

/* The view's half extents at distance 1 (tan of the half field of view),
 * across and up, for an EFB of w x h: the game's projection is made for its
 * 4:3 render window (Gfx_SetProjectionMatrix), and gx_draw.c widens it to
 * the EFB's shape ("screen mapping"). */
static void ViewExtents(int w, int h, f32 *tanX, f32 *tanY) {
    GfxWindow *rw = GetRenderWindow();
    f32 rwW = rw->width ? (f32)rw->width : 640.0f, rwH = rw->height ? (f32)rw->height : 480.0f;
    f32 fovY = 1.0714285f * s.fov * rwH / rwW;      /* degrees, at 4:3 */
    f32 t = (f32)tan(0.5f * fovY * 0.017453292f);
    int a[4];

    GX_Letterbox(w, h, 4, 3, a);
    *tanY = t * (f32)h / (f32)a[3];
    *tanX = t * (rwW / rwH) * (f32)w / (f32)a[2];
}

/* A drag of dx, dy image pixels in an image h pixels tall: orbit (left), or
 * pan (right, middle, or pan set) so what is under the pointer follows it. */
static void DragView(f32 dx, f32 dy, int h, int pan) {
    if (pan && s.fly) {
        f32 k = 2.0f / (f32)(h > 0 ? h : 1);   /* the image's height: 2 s of flying */

        FlyMove(0.0f, -dx * k, dy * k, 1.0f, 0);
    } else if (pan) {
        int w, eh;
        f32 tx, ty, k, sy = (f32)sin(s.yaw), cy = (f32)cos(s.yaw);

        GX_DrawEfbSize(&w, &eh);
        ViewExtents(w, eh, &tx, &ty);
        k = 2.0f * ty * s.distance / (f32)(h > 0 ? h : 1);
        s.target.x -= dx * k * cy;
        s.target.z += dx * k * sy;
        s.target.y += dy * k * (f32)cos(s.pitch);
    } else {
        f32 k = 3.0f / (f32)(h > 0 ? h : 1);       /* the image's height: about 170 degrees */

        if (s.fly) {
            k *= -0.5f;     /* mouse look: the view turns where the pointer goes */
        }
        s.yaw -= dx * k;
        s.pitch += dy * k;
        s.spin = 0;
    }
}

/* The drawn geometry under image pixel (x, y), as the last frame drew it:
 * the ray through it against each geometry's bounding sphere, the smallest
 * one hit (the most specific part), nearest first among equals. -1: none. */
/* The ray from the camera through image pixel (x, y): a unit direction. */
static void ScreenRay(f32 x, f32 y, f32 *d) {
    Camera *c = s.camera;
    f32 tanX, tanY, nx, ny, len;
    int w, h;

    GX_DrawEfbSize(&w, &h);
    ViewExtents(w, h, &tanX, &tanY);
    nx = (2.0f * x / (f32)w - 1.0f) * tanX;
    ny = (1.0f - 2.0f * y / (f32)h) * tanY;
    /* the camera's right is the screen's left (Gfx_SetCameraMatrix) */
    d[0] = c->dir.x - c->right.x * nx + c->up.x * ny;
    d[1] = c->dir.y - c->right.y * nx + c->up.y * ny;
    d[2] = c->dir.z - c->right.z * nx + c->up.z * ny;
    len = (f32)sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    d[0] /= len;
    d[1] /= len;
    d[2] /= len;
}

static int Pick(f32 x, f32 y, f32 *dist) {
    Camera *c = s.camera;
    f32 d[3], bestR = 1e30f, bestT = 1e30f;
    int best = -1;
    u32 i;

    ScreenRay(x, y, d);
    for (i = 0; i < s.numDrawnGeom; i++) {
        const Geometry *g = s.drawnGeom[i];
        f32 o[3], r, t, e2 = 0.0f;
        int k;

        for (k = 0; k < 3; k++) {
            f32 lo = g->bboxMin[k], hi = g->bboxMax[k];

            o[k] = s.drawnAt[i][k] + 0.5f * (lo + hi) - (&c->pos.x)[k];
            e2 += 0.25f * (hi - lo) * (hi - lo);
        }
        r = (f32)sqrt(e2);
        if (!(r < 1e6f)) {
            continue;
        }
        t = o[0] * d[0] + o[1] * d[1] + o[2] * d[2];
        o[0] -= t * d[0];
        o[1] -= t * d[1];
        o[2] -= t * d[2];
        if (t + r > 0.0f && o[0] * o[0] + o[1] * o[1] + o[2] * o[2] <= r * r &&
            (r < bestR * 0.999f || (r <= bestR * 1.001f && t < bestT))) {
            best = (int)i;
            bestR = r;
            bestT = t;
        }
    }
    *dist = bestT;
    return best;
}

/* The node of a geometry drawn from the shown pack, or NULL. */
static VNode *GeometryNode(const Geometry *g) {
    VPack *vp = s.shown;
    u32 i;

    for (i = 0; vp != NULL && i < vp->numNodes; i++) {
        if (vp->nodes[i].kind == VN_GEOMETRY && GeometryOf(vp, &vp->nodes[i]) == g) {
            return &vp->nodes[i];
        }
    }
    return NULL;
}

/* A key over the preview: f frames what is drawn, home (or r) resets the
 * view, space spins, arrows orbit, + and - zoom. 0: not one. */
static int ViewKey(const char *key) {
    static const char *const fly[] = { "w", "s", "a", "d", "e", "q" };
    size_t i;

    for (i = 0; i < SDL_arraysize(fly); i++) {
        if (SDL_strcasecmp(key, fly[i]) == 0) {
            /* a tenth of a second of flying (a held key: the move event) */
            f32 v[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };

            SetFly(1);
            FlyMove(v[i][0], v[i][1], v[i][2], 0.1f, 0);
            return 1;
        }
    }
    if (SDL_strcasecmp(key, "g") == 0) {
        Viewer_WorldLoaded();   /* the start */
        return 1;
    }
    if (SDL_strcasecmp(key, "f") == 0) {
        s_frameNext = 1;
    } else if (SDL_strcasecmp(key, "home") == 0 || SDL_strcasecmp(key, "r") == 0) {
        s.yaw = 0.6f;
        s.pitch = 0.3f;
        s.spin = 0;
        s_frameNext = 1;
    } else if (SDL_strcasecmp(key, "space") == 0) {
        s.spin = !s.spin;
    } else if (SDL_strcasecmp(key, "left") == 0) {
        s.yaw -= 15.0f * 0.017453292f;
    } else if (SDL_strcasecmp(key, "right") == 0) {
        s.yaw += 15.0f * 0.017453292f;
    } else if (SDL_strcasecmp(key, "up") == 0) {
        s.pitch += 10.0f * 0.017453292f;
    } else if (SDL_strcasecmp(key, "down") == 0) {
        s.pitch -= 10.0f * 0.017453292f;
    } else if (SDL_strcasecmp(key, "plus") == 0 || SDL_strcmp(key, "+") == 0 || SDL_strcmp(key, "=") == 0) {
        s.distance *= 0.8f;
    } else if (SDL_strcasecmp(key, "minus") == 0 || SDL_strcmp(key, "-") == 0) {
        s.distance *= 1.25f;
    } else {
        return 0;
    }
    return 1;
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
    Jw_String(w, "mode", s.fly ? "fly" : "orbit");
    PlaceCamera();
    Jw_BeginArray(w, "pos", 1);
    Jw_Float(w, NULL, s.camera->pos.x);
    Jw_Float(w, NULL, s.camera->pos.y);
    Jw_Float(w, NULL, s.camera->pos.z);
    Jw_EndArray(w);
    Jw_Float(w, "speed", s.flySpeed);
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
        s.selectedIndex = -1;
    }
    if (vp->slot < 0) {
        ViewerWorld_Unload();       /* the world's pack: the world goes */
        return;
    }
    ViewerMedia_PackGoing(vp->pack);
    Viewer_FreeGraph(vp);
    Viewer_ForgetPack(vp->pack);
    FileStreaming_ReleaseDataPack((u32)vp->slot, vp->type);
    FileStreaming_HeartBeat();      /* frees it now */
    vp->used = 0;
    vp->pack = NULL;
}

/* A copy of `name` that lives as long as the program: the streaming slots keep
 * the name's pointer (FileStreaming_LoadDataPack), and a slot can outlive the
 * viewer's pack that asked first (a car's share pack, still referenced). */
static const char *InternName(const char *name) {
    static char *s_names[256];
    static int s_count;
    int i;
    char *copy;

    for (i = 0; i < s_count; i++) {
        if (SDL_strcmp(s_names[i], name) == 0) {
            return s_names[i];
        }
    }
    copy = SDL_strdup(name);
    if (copy != NULL && s_count < (int)SDL_arraysize(s_names)) {
        s_names[s_count++] = copy;
    }
    return copy;    /* (past 256 names: kept, not found again) */
}

static int CmdLoad(JsonWriter *w, const JsonNode *req, char *err, size_t errSize) {
    extern u32 g_MaxStreamingDataPacks[11];
    const char *archive = Json_GetString(req, "archive", NULL), *last;
    const char *typeName = Json_GetString(req, "type", NULL);
    int type = (int)Json_GetInt(req, "type", -1), i;
    long long id = Json_GetInt(req, "id", -1);
    VPack *vp = NULL;
    DataPack *dp;
    DataPackRef refs[2];
    u32 songBank = 0;
    const char *interned;
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
    /* there before anything is unloaded for it: a wrong name leaves what is shown */
    if (SDL_strlen(archive) >= sizeof(vp->archive) || id > 0xFFFFFFFFll || !PackRefs(archive, type, (u32)id, refs, &songBank)) {
        SDL_snprintf(err, errSize, "%s has no %s pack %lld", archive, g_viewerTypeNames[type], id);
        return 0;
    }
    if ((interned = InternName(archive)) == NULL) {
        SDL_snprintf(err, errSize, "out of memory");
        return 0;
    }
    ViewerWorld_Unload();   /* a pack shown alone again */
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
    ViewerMedia_BeforeLoad(songBank != 0);
    LoadShares(vp->archive, type, (u32)id, 0);
    vp->slot = FileStreaming_LoadDataPack((void *)interned, type, (u32)id, 0);
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
    s.selectedIndex = -1;
    s_frameNext = 1;
    Port_Log("viewer: loaded %s (%u nodes)", full, vp->numNodes);
    WritePack(w, vp);
    return 1;
}

void Viewer_UnloadAll(void) {
    int i;

    for (i = 0; i < VIEWER_MAX_PACKS; i++) {
        if (g_viewerPacks[i].used && (!ViewerWorld_Active() || g_viewerPacks[i].type != 4)) {
            Unload(&g_viewerPacks[i]);
        }
    }
}

VPack *Viewer_AdoptPack(const char *archive, int type, u32 id, DataPack *dp) {
    const char *last = SDL_strrchr(archive, '/');
    VPack *vp = NULL;
    int i;

    for (i = 0; i < VIEWER_MAX_PACKS && vp == NULL; i++) {
        if (!g_viewerPacks[i].used) {
            vp = &g_viewerPacks[i];
        }
    }
    if (vp == NULL || dp == NULL || GETP(dp->base) == NULL) {
        return NULL;
    }
    last = last != NULL ? last + 1 : archive;
    memset(vp, 0, sizeof(*vp));
    SDL_strlcpy(vp->archive, archive, sizeof(vp->archive));
    SDL_strlcpy(vp->archiveName, last, sizeof(vp->archiveName));
    SDL_snprintf(vp->name, sizeof(vp->name), "%s_%u", g_viewerTypeNames[type], (unsigned)id);
    vp->type = type;
    vp->id = id;
    vp->slot = -1;
    vp->used = 1;
    vp->pack = dp;
    vp->size = dp->dataSize;
    Viewer_BuildGraph(vp);
    s.shown = vp;
    s.selected[0] = '\0';
    s.selectedIndex = -1;
    return vp;
}

void Viewer_DropPack(VPack *vp) {
    if (s.shown == vp) {
        s.shown = NULL;
        s.selected[0] = '\0';
        s.selectedIndex = -1;
    }
    ViewerMedia_PackGoing(vp->pack);
    Viewer_FreeGraph(vp);
    Viewer_ForgetPack(vp->pack);
    vp->used = 0;
    vp->pack = NULL;
}

/* The model's lowest point (its rest pose): a tile car stands on it. */
static f32 ModelBottom(MotionAnim *model) {
    u32 numBones = 0, used = s.numMatrices, i;
    Matrix4 *bones = ModelPose(NULL, model, &numBones);
    MotionPart *rigid = GETP(model->rigid);
    f32 lo = 0.0f;

    for (i = 0; bones != NULL && rigid != NULL && i < model->numRigid; i++) {
        Geometry *g = GETP(rigid[i].geom);

        if (g != NULL && rigid[i].mtxIndex < numBones && bones[rigid[i].mtxIndex].v[3].y + g->bboxMin[1] < lo) {
            lo = bones[rigid[i].mtxIndex].v[3].y + g->bboxMin[1];
        }
    }
    s.numMatrices = used;
    return lo;
}

void Viewer_DrawCar(CarAInfo *info, const Matrix4 *at) {
    MotionAnim *model = ModelOf(NULL, info);
    f32 min[3], max[3];
    Matrix4 m;

    if (model != NULL) {
        m = *at;
        m.v[3].y -= ModelBottom(model);     /* on its wheels (the game drops it there) */
        DrawModel(NULL, model, min, max, &m);
    }
}

/* A world loaded: the camera behind player 1's start, looking along it. */
void Viewer_WorldLoaded(void) {
    f32 p[3], yaw;

    s_frameNext = 0;
    s.spin = 0;
    if (ViewerWorld_Start(1, p, &yaw)) {
        s.target.x = p[0];
        s.target.y = p[1] + 2.0f;
        s.target.z = p[2];
        s.distance = 30.0f;
        s.yaw = yaw;
        s.pitch = 0.25f;
        s.flySpeed = 60.0f;
    } else {
        s_frameNext = 1;
    }
    if (s.fly) {
        PlaceCamera();
        s.flyPos = s.camera->pos;
    }
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

/* A screenshot goes to a .bmp or .png, and over nothing but an image: the
 * port is a local one, but what asks for a frame shouldn't get to replace a file. */
static int ShotPathOk(const char *path, char *err, size_t errSize) {
    size_t n = SDL_strlen(path);
    const char *ext = n > 4 ? path + n - 4 : "";
    SDL_IOStream *io;
    u8 head[8];
    size_t got;

    if (SDL_strcasecmp(ext, ".bmp") != 0 && SDL_strcasecmp(ext, ".png") != 0) {
        SDL_snprintf(err, errSize, "a screenshot is a .bmp or .png");
        return 0;
    }
    io = SDL_IOFromFile(path, "rb");
    if (io == NULL) {
        return 1;   /* nothing there (or not readable: saving says why) */
    }
    got = SDL_ReadIO(io, head, sizeof(head));
    SDL_CloseIO(io);
    if (got == 0 || (got >= 2 && head[0] == 'B' && head[1] == 'M') ||
        (got >= 8 && SDL_memcmp(head, "\x89PNG\r\n\x1a\n", 8) == 0)) {
        return 1;
    }
    SDL_snprintf(err, errSize, "%s is there and isn't an image: not overwritten", path);
    return 0;
}

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
        Jw_Int(&w, "media", 1);     /* the media commands (viewer_media.c) */
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
                s.selectedIndex = -1;
            }
        } else if ((ok = NeedNode(req, &vp, &nd, err, sizeof(err)))) {
            s.shown = vp;
            SDL_strlcpy(s.selected, nd->id, sizeof(s.selected));
            s.selectedIndex = (s32)(nd - vp->nodes);
        }
        if (ok) {
            f32 p[3], r;

            if (cmd[0] == 'f' && ViewerWorld_Active() && s.selectedIndex >= 0 && s.shown != NULL && s.shown->type == 4) {
                VNode *sel = &s.shown->nodes[s.selectedIndex];

                if (ViewerWorld_PlaceOf(sel->off, sel->size, p, &r)) {
                    s.target.x = p[0], s.target.y = p[1], s.target.z = p[2];
                    s.distance = r * 3.0f + 5.0f;
                    if (s.fly) {
                        s.fly = 0;
                        PlaceCamera();
                        s.flyPos = s.camera->pos;
                        s.fly = 1;
                    }
                }
            } else if (cmd[0] == 'f') {
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
            {
                const char *mode = Json_GetString(req, "mode", NULL);
                const JsonNode *pos = Json_Get(req, "pos");

                if (mode != NULL) {
                    SetFly(SDL_strcmp(mode, "fly") == 0);
                }
                s.flySpeed = (f32)Json_GetNumber(req, "speed", s.flySpeed);
                if (pos != NULL && pos->type == JSON_ARRAY && pos->count == 3) {
                    SetFly(1);
                    s.flyPos.x = (f32)Json_At(pos, 0)->number;
                    s.flyPos.y = (f32)Json_At(pos, 1)->number;
                    s.flyPos.z = (f32)Json_At(pos, 2)->number;
                }
            }
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

        u8 rgb[3];
        int k;

        for (k = 0; c != NULL && c->type == JSON_ARRAY && c->count == 3 && k < 3; k++) {
            const JsonNode *x = Json_At(c, k);

            if (x == NULL || x->type != JSON_NUMBER || !(x->number == x->number)) {
                break;
            }
            rgb[k] = x->number <= 0.0 ? 0 : x->number >= 255.0 ? 255 : (u8)x->number;
        }
        if (k == 3) {
            s.background.r = rgb[0];
            s.background.g = rgb[1];
            s.background.b = rgb[2];
        } else {
            SDL_snprintf(err, sizeof(err), "background takes color: [r, g, b], 0 .. 255");
            ok = 0;
        }
    } else if (SDL_strcmp(cmd, "screenshot") == 0) {
        const char *path = Json_GetString(req, "path", NULL);

        if (path == NULL || s.shotPath != NULL) {
            SDL_snprintf(err, sizeof(err), path == NULL ? "screenshot takes path (.bmp or .png)" : "a screenshot is pending");
            ok = 0;
        } else if (!ShotPathOk(path, err, sizeof(err))) {
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
    } else if (SDL_strcmp(cmd, "frames") == 0) {
        /* the frames to the browser through shared memory, at a size; on: false stops */
        if (!Json_GetBool(req, "on", 1)) {
            ViewerFrames_Stop();
            ViewerFrames_WriteInfo(&w);
        } else if (!ViewerFrames_Configure((int)Json_GetNumber(req, "width", 0.0), (int)Json_GetNumber(req, "height", 0.0),
                                           err, sizeof(err))) {
            ok = 0;
        } else {
            ViewerFrames_WriteInfo(&w);
        }
    } else if (SDL_strcmp(cmd, "input") == 0) {
        /* the pointer and keys over the browser's picture: events, in image pixels */
        const JsonNode *events = Json_Get(req, "events");
        int h = (int)Json_GetNumber(req, "height", 0.0), ew, eh, k;

        GX_DrawEfbSize(&ew, &eh);
        if (h <= 0) {
            h = eh;
        }
        for (k = 0; events != NULL && events->type == JSON_ARRAY && k < (int)events->count; k++) {
            const JsonNode *e = Json_At(events, k);
            const char *type = Json_GetString(e, "type", "");

            if (SDL_strcmp(type, "drag") == 0) {
                const char *button = Json_GetString(e, "button", "left");

                DragView((f32)Json_GetNumber(e, "dx", 0.0), (f32)Json_GetNumber(e, "dy", 0.0), h,
                         SDL_strcmp(button, "left") != 0 || Json_GetBool(e, "pan", 0));
            } else if (SDL_strcmp(type, "wheel") == 0) {
                if (s.fly) {
                    s.flySpeed *= (f32)pow(1.25, Json_GetNumber(e, "dy", 0.0));   /* fly: the wheel sets the speed */
                } else {
                    s.distance *= (f32)pow(0.9, Json_GetNumber(e, "dy", 0.0));
                }
            } else if (SDL_strcmp(type, "move") == 0) {
                /* fly: held keys, forward / right / up in -1 .. 1 over dt seconds */
                SetFly(1);
                FlyMove((f32)Json_GetNumber(e, "forward", 0.0), (f32)Json_GetNumber(e, "right", 0.0),
                        (f32)Json_GetNumber(e, "up", 0.0), (f32)Json_GetNumber(e, "dt", 0.016), Json_GetBool(e, "fast", 0));
            } else if (SDL_strcmp(type, "key") == 0) {
                if (!ViewKey(Json_GetString(e, "key", ""))) {
                    SDL_snprintf(err, sizeof(err),
                                 "no key %.40s (f, home, r, space, left, right, up, down, plus, minus, w a s d q e, g)",
                                 Json_GetString(e, "key", ""));
                    ok = 0;
                }
            } else {
                SDL_snprintf(err, sizeof(err), "an input event is drag, wheel, move or key, not %.40s", type);
                ok = 0;
            }
        }
        WriteCamera(&w);
    } else if (SDL_strcmp(cmd, "pick") == 0) {
        /* the node drawn under image pixel x, y (of the last frame); select: also select it */
        const JsonNode *x = Json_Get(req, "x"), *y = Json_Get(req, "y");

        if (x == NULL || y == NULL || x->type != JSON_NUMBER || y->type != JSON_NUMBER) {
            SDL_snprintf(err, sizeof(err), "pick takes x and y, image pixels");
            ok = 0;
        } else {
            f32 dist = 0.0f;
            int i;
            VNode *nd;

            PlaceCamera();
            if (ViewerWorld_Active() && s.shown != NULL && s.shown->type == 4) {
                /* a world: its objects (instances, items, effect points, tiles) */
                f32 d[3], o[3] = { s.camera->pos.x, s.camera->pos.y, s.camera->pos.z };
                const char *kind = NULL;
                u32 off;

                ScreenRay((f32)x->number, (f32)y->number, d);
                off = ViewerWorld_Pick(o, d, &dist, &kind);
                nd = off != VIEWER_NO_OFFSET && off / 4 < s.shown->size / 4 && s.shown->owner != NULL &&
                             s.shown->owner[off / 4] >= 0
                         ? &s.shown->nodes[s.shown->owner[off / 4]]
                         : NULL;
                if (off != VIEWER_NO_OFFSET) {
                    char at[16], rpath[96];

                    Jw_String(&w, "kind", kind);
                    SDL_snprintf(at, sizeof(at), "@%x", off);
                    Jw_String(&w, "at", at);
                    ViewerWorld_PathOf(off, rpath, sizeof(rpath));
                    if (rpath[0]) {
                        Jw_String(&w, "path", rpath);   /* the record, by structural path (level editor) */
                    }
                    Jw_Float(&w, "t", dist);
                }
                {
                    /* the ray, for the level editor's own objects */
                    Jw_BeginArray(&w, "origin", 1);
                    Jw_Float(&w, NULL, o[0]);
                    Jw_Float(&w, NULL, o[1]);
                    Jw_Float(&w, NULL, o[2]);
                    Jw_EndArray(&w);
                    Jw_BeginArray(&w, "dir", 1);
                    Jw_Float(&w, NULL, d[0]);
                    Jw_Float(&w, NULL, d[1]);
                    Jw_Float(&w, NULL, d[2]);
                    Jw_EndArray(&w);
                }
            } else {
                i = Pick((f32)x->number, (f32)y->number, &dist);
                nd = i >= 0 ? GeometryNode(s.drawnGeom[i]) : NULL;
            }
            if (nd != NULL) {
                char full[200];

                SDL_snprintf(full, sizeof(full), "%s/%s", s.shown->archiveName, s.shown->name);
                Jw_String(&w, "pack", full);
                Jw_String(&w, "node", nd->id);
                Jw_Float(&w, "distance", dist);
                if (Json_GetBool(req, "select", 0)) {
                    SDL_strlcpy(s.selected, nd->id, sizeof(s.selected));
                    s.selectedIndex = (s32)(nd - s.shown->nodes);
                }
            } else {
                Viewer_WriteNull(&w, "node");
            }
        }
    } else if (SDL_strcmp(cmd, "raycast") == 0) {
        /* a world's collision (the game's RayHitsTerrain): from `from` to `to`; or through
         * image pixel x, y from the camera (`range`, default 20000); or straight down at
         * `down` [x, z] (from the collision's top) */
        const JsonNode *from = Json_Get(req, "from"), *to = Json_Get(req, "to"), *down = Json_Get(req, "down");
        const JsonNode *x = Json_Get(req, "x"), *y = Json_Get(req, "y");
        Vector4 a __attribute__((aligned(16))), b __attribute__((aligned(16)));
        f32 lo[3], hi[3], t = 1.0f;
        int have = 1;

        SDL_zero(a);
        SDL_zero(b);
        if (!ViewerWorld_Active() || !ViewerWorld_Bounds(lo, hi)) {
            SDL_snprintf(err, sizeof(err), "raycast needs a world (world)");
            ok = 0;
            have = 0;
        } else if (from != NULL && to != NULL && from->count == 3 && to->count == 3) {
            a.x = (f32)Json_At(from, 0)->number, a.y = (f32)Json_At(from, 1)->number, a.z = (f32)Json_At(from, 2)->number;
            b.x = (f32)Json_At(to, 0)->number, b.y = (f32)Json_At(to, 1)->number, b.z = (f32)Json_At(to, 2)->number;
        } else if (down != NULL && down->count == 2) {
            a.x = b.x = (f32)Json_At(down, 0)->number;
            a.z = b.z = (f32)Json_At(down, 1)->number;
            a.y = hi[1] + 10.0f;
            b.y = lo[1] - 10.0f;
        } else if (x != NULL && y != NULL && x->type == JSON_NUMBER && y->type == JSON_NUMBER) {
            f32 d[3], range = (f32)Json_GetNumber(req, "range", 20000.0);

            PlaceCamera();
            ScreenRay((f32)x->number, (f32)y->number, d);
            a = s.camera->pos;
            b.x = a.x + d[0] * range, b.y = a.y + d[1] * range, b.z = a.z + d[2] * range;
        } else {
            SDL_snprintf(err, sizeof(err), "raycast takes from and to [x, y, z], down [x, z], or x and y (image pixels)");
            ok = 0;
            have = 0;
        }
        if (have) {
            int hit = RayHitsTerrain(&a, &b, &t, NULL) != 0;

            Jw_Bool(&w, "hit", hit);
            if (hit) {
                Jw_BeginArray(&w, "point", 1);
                Jw_Float(&w, NULL, a.x + (b.x - a.x) * t);
                Jw_Float(&w, NULL, a.y + (b.y - a.y) * t);
                Jw_Float(&w, NULL, a.z + (b.z - a.z) * t);
                Jw_EndArray(&w);
                Jw_Float(&w, "t", t);
            }
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
        {
            int ew, eh;

            GX_DrawEfbSize(&ew, &eh);
            Jw_Int(&w, "width", ew);
            Jw_Int(&w, "height", eh);
        }
        Jw_Bool(&w, "headless", Port_ViewerHeadless());
        ViewerFrames_WriteStats(&w);
        ViewerMedia_WriteStatus(&w);
        ViewerWorld_WriteStatus(&w);
        WriteCamera(&w);
    } else if (SDL_strcmp(cmd, "goto") == 0) {
        /* a world's start: player `port`'s tile car (default 1), or nav point `nav` */
        f32 p[3], yaw;
        const JsonNode *navIdx = Json_Get(req, "nav");

        if (navIdx != NULL ? !ViewerWorld_NavPoint((u32)Json_GetNumber(req, "nav", 0.0), p, &yaw)
                           : !ViewerWorld_Start((int)Json_GetNumber(req, "port", 1.0), p, &yaw)) {
            SDL_snprintf(err, sizeof(err), navIdx != NULL ? "no world loaded, or no such nav point"
                                                          : "no world loaded, or it has no start");
            ok = 0;
        } else {
            s.target.x = p[0], s.target.y = p[1] + 2.0f, s.target.z = p[2];
            s.distance = 30.0f;
            s.yaw = yaw;
            s.pitch = 0.25f;
            if (s.fly) {
                s.fly = 0;
                PlaceCamera();
                s.flyPos = s.camera->pos;
                s.fly = 1;
            }
            WriteCamera(&w);
        }
    } else if (ViewerWorld_Dispatch(cmd, req, &w, &ok, err, sizeof(err))) {
    } else if (ViewerLevel_Dispatch(cmd, req, &w, &ok, err, sizeof(err))) {
        /* worlds, world, world_unload, overlays */
    } else if (SDL_strcmp(cmd, "quit") == 0) {
        s.quit = 1;
    } else if (ViewerMedia_Dispatch(cmd, req, &w, &ok, err, sizeof(err))) {
        /* textures, texture, sounds, sound, play, stop */
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

static void SendError(const char *msg) {
    JsonWriter w;

    Jw_Init(&w);
    Jw_BeginObject(&w, NULL, 0);
    Jw_Bool(&w, "ok", 0);
    Jw_String(&w, "error", msg);
    Jw_EndObject(&w);
    SendJson(&w);
    Jw_Free(&w);
}

/* An HTTP request's first line ("POST / HTTP/1.1"): a web page aiming at the port. */
static int LooksLikeHttp(const char *line, size_t len) {
    static const char *const methods[] = { "GET ", "POST ", "PUT ", "HEAD ", "OPTIONS ", "DELETE ", "PATCH ", "CONNECT ", "TRACE " };
    size_t i;

    for (i = 0; i < SDL_arraysize(methods); i++) {
        size_t n = SDL_strlen(methods[i]);

        if (len >= n && SDL_strncasecmp(line, methods[i], n) == 0) {
            return 1;
        }
    }
    for (i = 0; i + 5 <= len; i++) {
        if (SDL_memcmp(line + i, "HTTP/", 5) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Equal tokens, compared in the same time whatever matches. */
static int SameToken(const char *a, const char *b) {
    size_t n = SDL_strlen(a), m = SDL_strlen(b), i;
    unsigned diff = n != m;

    for (i = 0; i < n; i++) {
        diff |= (unsigned)(a[i] ^ b[i < m ? i : 0]);
    }
    return diff == 0 && m > 0;
}

/* One request line. A connection's first is hello with the token, or it is dropped:
 * anything able to reach 127.0.0.1 (a web page's POST, say) is not a client. */
static void HandleLine(const char *line, size_t len) {
    char err[160];
    JsonNode *req;

    if (len == 0 || (len == 1 && line[0] == '\r')) {
        return;
    }
    if (!s.authed && LooksLikeHttp(line, len)) {
        Port_Log("viewer: an HTTP request on the viewer's port: dropped");
        DropClient();
        return;
    }
    req = Json_Parse(line, len, err, sizeof(err));
    if (!s.authed) {
        const char *cmd = req != NULL && req->type == JSON_OBJECT ? Json_GetString(req, "cmd", "") : "";
        const char *token = req != NULL && req->type == JSON_OBJECT ? Json_GetString(req, "token", "") : "";

        if (SDL_strcmp(cmd, "hello") != 0 || !SameToken(s.token, token)) {
            Port_Log("viewer: a client without the token: dropped");
            SendError(SDL_strcmp(cmd, "hello") != 0 ? "the first request is hello, with the viewer's token"
                                                    : "wrong token (HWVX_VIEWER_TOKEN)");
            Json_Free(req);
            DropClient();
            return;
        }
        s.authed = 1;
    }
    if (req != NULL && req->type == JSON_OBJECT) {
        Dispatch(req);
    } else {
        SendError(req != NULL ? "a request is a JSON object" : err);
    }
    Json_Free(req);
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
        size_t start = 0;

        if (n == 0 || (n < 0 && !SockWouldBlock())) {
            DropClient();
            break;
        }
        if (n < 0) {
            break;
        }
        if (s.inLen + (size_t)n + 1 > s.inCap) {
            size_t cap = (s.inLen + (size_t)n + 1) * 2;
            char *p = SDL_realloc(s.in, cap);

            if (p == NULL) {
                DropClient();
                break;
            }
            s.in = p;
            s.inCap = cap;
        }
        memcpy(s.in + s.inLen, buf, (size_t)n);
        s.inLen += (size_t)n;
        /* the lines in it, each scanned once (s.scan: where the last search stopped) */
        while (s.client != SOCK_NONE) {
            char *nl = memchr(s.in + s.scan, '\n', s.inLen - s.scan);
            size_t len;

            if (nl == NULL) {
                s.scan = s.inLen;
                break;
            }
            len = (size_t)(nl - (s.in + start));
            s.scan = start + len + 1;
            HandleLine(s.in + start, len);
            start += len + 1;
        }
        if (s.client == SOCK_NONE) {
            break;  /* (dropped: the buffer is empty) */
        }
        if (start > 0) {
            s.inLen -= start;
            s.scan -= start;
            memmove(s.in, s.in + start, s.inLen);
        }
        if (s.inLen > MAX_LINE) {
            SendError("a request line is over 1 MB");
            DropClient();
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

    GX_DrawEfbSize(&w, &h);
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

/* The token a client's hello has to carry: HWVX_VIEWER_TOKEN (the browser and
 * viewer_client.py make one up and pass it), or one made up here and logged. */
static void SetToken(void) {
    const char *e = SDL_getenv("HWVX_VIEWER_TOKEN");
    u8 bytes[16];
    SDL_IOStream *io;
    size_t got = 0, i;

    if (e != NULL && *e != '\0') {
        SDL_strlcpy(s.token, e, sizeof(s.token));
        return;
    }
    if ((io = SDL_IOFromFile("/dev/urandom", "rb")) != NULL) {
        got = SDL_ReadIO(io, bytes, sizeof(bytes));
        SDL_CloseIO(io);
    }
    if (got != sizeof(bytes)) {
        u64 x = SDL_GetPerformanceCounter() ^ ((u64)(uintptr_t)&e << 17) ^ SDL_GetTicksNS();

        for (i = 0; i < sizeof(bytes); i++) {
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            bytes[i] = (u8)(x >> 24);
        }
    }
    for (i = 0; i < sizeof(bytes); i++) {
        SDL_snprintf(s.token + i * 2, 3, "%02x", bytes[i]);
    }
    Port_Log("viewer: token %s (HWVX_VIEWER_TOKEN sets one)", s.token);
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
    /* the MCP's textures (its environment maps) again, as each state's Init
     * has them: Main_Init's Gfx_TextureManager_Init dropped them after the
     * content registry loaded the MCP, and their slots went to the next pack
     * loaded (a car's share pack: its wheels and chrome sampled that) */
    StateGameManager_RefreshMCPTextures();
    ViewerMedia_Start();    /* the sound manager: packs load with their samples (viewer_media.c) */
    Heartbeat_Process = Heartbeat;  /* (Main_Loop runs Port_ViewerFrame instead) */

    s.matrices = Allocate(MATRIX_POOL * sizeof(Matrix4), 0, "");
    if (s.camera == NULL || s.matrices == NULL) {
        return 0;
    }
    s.selectedIndex = -1;
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

    SetToken();
    if (!Listen()) {
        Port_Fatal("The viewer could not listen on 127.0.0.1:%d.", s.port);
    }
    SDL_AddEventWatch(MouseWatch, NULL);
    SDL_SetWindowTitle(g_port.window, "HWVX viewer");
    if (Port_ViewerHeadless()) {
        Port_Log("viewer: the window is hidden; frames go to the browser (HWVX_VIEWER_WINDOW=1 shows it)");
    }
    Port_Log("viewer: listening on 127.0.0.1:%d", s.port);
    return 1;
}

s32 Port_ViewerFrame(void) {
    extern GXColor m_clearColor;
    static const GXColor white = { 0xFF, 0xFF, 0xFF, 0xFF };
    Vector4 lightDir = { -0.4f, -1.0f, -0.5f, 0.0f };
    f32 min[3], max[3];
    GXColor c;
    int any;

    Poll();
    if (s.quit) {
        ViewerFrames_Shutdown();
        ViewerMedia_Shutdown();
        return 0;
    }
    if (s.spin) {
        s.yaw += 0.01f;
    }
    ShowStats_StartFrame();
    ProcessManager_Heartbeat(0);
    FileStreaming_HeartBeat();
    ShaderHolder_Heartbeat();
    if (ViewerWorld_Active()) {
        static const GXColor black = { 0, 0, 0, 0 };

        m_clearColor = black;   /* the game's (Render_.c); the hub's sky covers it */
    } else {
        m_clearColor = s.background;
    }
    Gfx_SetProjectionMatrix(s.fov);
    GetRenderWindow()->fov = s.fov;
    PlaceCamera();
    Camera_PrepareForRender(s.camera);
    ShowStats_BeginScene();
    /* as LightManager_LightThing lights a car outside a world (the car select) */
    c = white;
    Gfx_SetAmbientColor(&c);
    c = white;
    Gfx_SetParallelLight(&c, &lightDir);
    if (!ViewerWorld_Active()) {
        /* the reflections (windows, chrome, wheels) as the car select draws
         * them: the MCP's environment map (GameSpecificInterfaces.c); without
         * one those parts are black. A world has its own (World_Refresh). */
        MCP *mcp = GameSettings_GetMCP();

        if (mcp != NULL) {
            Gfx_SetEnvmap(mcp->envmapX, mcp->envmapY, 1, 100.0f, 100.0f);
            Gfx_SetEnviroMapMode(1);
        }
    }
    if (ViewerWorld_Active()) {
        s.numMatrices = 0;
        s.drawn = 0;
        s.numDrawnGeom = 0;
        ViewerWorld_Draw();
        any = 0;
        if (s_frameNext && ViewerWorld_Bounds(min, max)) {
            Frame(min, max);
            s_frameNext = 0;
        }
    } else {
        any = DrawScene(min, max);
    }
    if (any && s_frameNext) {
        /* the camera for the frame after this one: this one's draws are queued */
        Frame(min, max);
        s_frameNext = 0;
    }
    ProcessManager_Render(1);
    if (s.shotPath != NULL && s.shotWait > 0) {
        s.shotWait--;   /* a frame first: the camera framed, the buffers built */
    }
    if ((s.shotPath != NULL && s.shotWait == 0) || ViewerFrames_On()) {
        /* the frame read back between its draws and the display copy (which clears it) */
        Gfx_EndScene();
        ViewerWorld_Overlays(s.fov);
        if (s.shotPath != NULL && s.shotWait == 0) {
            SaveShot();
        }
        ViewerFrames_EndFrame(s.frame);
        Gfx_FlipBuffers();
        Performance_End();
    } else if (ViewerWorld_Active()) {
        Gfx_EndScene();
        ViewerWorld_Overlays(s.fov);
        Gfx_FlipBuffers();
        Performance_End();
    } else {
        ShowStats_EndScene();
    }
    s.frame++;
    return 1;
}

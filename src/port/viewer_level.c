/* Viewer mode: the level editor's commands on a world on show (docs/viewer-protocol.md,
 * "Levels"; tools/browser/src/level_ui.rs).
 *
 *   level    {path} applies a level file (tools/xdl_level.py compile) to the world shown,
 *            as the game does when the level loads (Port_Level.c); {path: null} takes it out.
 *            The editor's live preview: it compiles the project as it is edited and applies
 *            it again; patched base records (a moved start) show at once.
 *   records  {table} the World's records of a table, by structural path
 *            (World/0/tileCars/0, World/0/quads/16/contents/items/2): where they are and
 *            what they are, for the editor's lists.
 *   record   {path} one base record's fields, decoded by its schema (as `get` decodes
 *            a typed node), with its struct and offset in the world pack.
 *   marks    {marks: [{kind: sphere|arrow|cross|line, pos, to?, radius?, rotY?, color?}]}
 *            shapes drawn over the world each frame (the editor's selection, its gizmo).
 *
 * The level's own volumes are drawn too (the "level" overlay, on while a level is applied). */
#include "port_internal.h"
#include "gx_render.h"
#include "viewer_internal.h"
#include "World.h"
#include "Port_Level.h"
#include "Port_SpawnBox.h"
#include <math.h>

typedef struct Mark {
    char kind;          /* 's' sphere, 'a' arrow, 'c' cross, 'l' line, 'b' a start's box */
    f32 pos[3], to[3];
    f32 radius, rotY;
    u8 color[3];
} Mark;

#define MAX_MARKS 256
static Mark s_marks[MAX_MARKS];
static int s_numMarks;
static int s_levelOverlay = 1;

/* ---- shapes ---------------------------------------------------------------- */

static void Circle(const f32 *c, f32 rad, int axis, const u8 *col) {
    int i;

    for (i = 0; i < 32; i++) {
        f32 t0 = i * 6.2831853f / 32, t1 = (i + 1) * 6.2831853f / 32, a[3], b[3];
        int u = axis == 0 ? 1 : 0, v = axis == 2 ? 1 : 2;

        SDL_memcpy(a, c, sizeof(a));
        SDL_memcpy(b, c, sizeof(b));
        a[u] += rad * cosf(t0), a[v] += rad * sinf(t0);
        b[u] += rad * cosf(t1), b[v] += rad * sinf(t1);
        ViewerWorld_Line(a, b, col[0], col[1], col[2]);
    }
}

static void Sphere(const f32 *c, f32 rad, const u8 *col) {
    Circle(c, rad, 0, col);
    Circle(c, rad, 1, col);
    Circle(c, rad, 2, col);
}

static void Cross(const f32 *p, f32 s, const u8 *col) {
    int k;

    for (k = 0; k < 3; k++) {
        f32 a[3], b[3];

        SDL_memcpy(a, p, sizeof(a));
        SDL_memcpy(b, p, sizeof(b));
        a[k] -= s;
        b[k] += s;
        ViewerWorld_Line(a, b, col[0], col[1], col[2]);
    }
}

/* An arrow along rotY (forward sin, 0, cos), `len` long, with a ring. */
static void Arrow(const f32 *p, f32 rotY, f32 len, const u8 *col) {
    f32 s = sinf(rotY), c = cosf(rotY);
    f32 base[3] = { p[0], p[1] + 1.0f, p[2] }, tip[3], l[3], r[3];

    tip[0] = base[0] + s * len, tip[1] = base[1], tip[2] = base[2] + c * len;
    l[0] = tip[0] - s * len * 0.3f + c * len * 0.2f, l[1] = tip[1], l[2] = tip[2] - c * len * 0.3f - s * len * 0.2f;
    r[0] = tip[0] - s * len * 0.3f - c * len * 0.2f, r[1] = tip[1], r[2] = tip[2] - c * len * 0.3f + s * len * 0.2f;
    ViewerWorld_Line(base, tip, col[0], col[1], col[2]);
    ViewerWorld_Line(tip, l, col[0], col[1], col[2]);
    ViewerWorld_Line(tip, r, col[0], col[1], col[2]);
    Circle(p, len * 0.25f, 1, col);
}

/* The corners of a start's box at pos turned as a tile car: i's bits pick max x, y, z. */
static void BoxCorners(const f32 *pos, f32 rotY, f32 rotX, f32 rotZ, f32 out[8][3]) {
    f32 lo[3], hi[3];
    Matrix4 m __attribute__((aligned(16)));
    int i, k;

    Port_SpawnEnvelopeKnown(lo, hi, 1);   /* drawing: never loads (the world load computed it) */
    SetOrientation(&m, rotY, rotX, rotZ);   /* as StateGamePlay_Init places a tile car */
    for (i = 0; i < 8; i++) {
        f32 l[3] = { (i & 1) ? hi[0] : lo[0], (i & 2) ? hi[1] : lo[1], (i & 4) ? hi[2] : lo[2] };

        for (k = 0; k < 3; k++) {
            out[i][k] = pos[k] + (&m.v[0].x)[k] * l[0] + (&m.v[1].x)[k] * l[1] + (&m.v[2].x)[k] * l[2];
        }
    }
}

void ViewerLevel_SpawnBox(const f32 *pos, f32 rotY, f32 rotX, f32 rotZ, const u8 *col) {
    static const int e[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 },
                                  { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
    f32 c[8][3];
    int i;

    BoxCorners(pos, rotY, rotX, rotZ, c);
    for (i = 0; i < 12; i++) {
        ViewerWorld_Line(c[e[i][0]], c[e[i][1]], col[0], col[1], col[2]);
    }
    /* the front: a cross on its face */
    ViewerWorld_Line(c[4], c[7], col[0], col[1], col[2]);
    ViewerWorld_Line(c[5], c[6], col[0], col[1], col[2]);
}

/* Is the start's box clear (walls, ceiling, ground)? Rays along its edges and through it
 * against the world's collision, and down from above each bottom corner and its middle.
 * *seat: the highest ground under it (where the car's origin goes, "to the ground"); the
 * problems as text into `out` (comma separated; "" when clear). */
static int CheckBox(const f32 *pos, f32 rotY, f32 *seat, char *out, size_t size) {
    static const int e[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 },
                                  { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
    f32 c[8][3], lifted[3], lowest = 1e30f, highest = -1e30f, lo[3], hi[3], bottom;
    int i, n = 0, ground = 0, walls = 0, ceiling = 0, gaps = 0;

    out[0] = 0;
    Port_SpawnEnvelope(lo, hi, NULL, 1);
    bottom = pos[1] + lo[1];
    /* the ground under the four bottom corners and the middle */
    BoxCorners(pos, rotY, 0.0f, 0.0f, c);
    for (i = 0; i <= 4; i++) {
        Vector4 a __attribute__((aligned(16))), b __attribute__((aligned(16)));
        const f32 *p = i < 4 ? c[i == 0 ? 0 : i == 1 ? 1 : i == 2 ? 4 : 5] : pos;
        f32 t = 1.0f;

        SDL_zero(a);
        SDL_zero(b);
        a.x = b.x = p[0];
        a.z = b.z = p[2];
        a.y = c[2][1] + 1.0f;   /* above the box's top */
        b.y = pos[1] - 40.0f;
        if (RayHitsTerrain(&a, &b, &t, NULL)) {
            f32 g = a.y + (b.y - a.y) * t;

            ground++;
            lowest = g < lowest ? g : lowest;
            highest = g > highest ? g : highest;
        }
    }
    /* where the car's origin rests on the ground: the box reaches below it by the envelope's
     * below-origin depth (-lo[1]: 1.4 on retail, car 7's), as a car standing there does */
    *seat = ground ? highest : pos[1];
    /* the box's own edges and diagonals, its bottom raised off the ground a little (the
     * ground under it is the check above): anything they cross is inside the box */
    lifted[0] = pos[0], lifted[2] = pos[2];
    lifted[1] = (ground && highest > bottom ? highest - lo[1] : pos[1]) + 0.5f;
    BoxCorners(lifted, rotY, 0.0f, 0.0f, c);
    for (i = 0; i < 16; i++) {
        Vector4 a __attribute__((aligned(16))), b __attribute__((aligned(16)));
        const f32 *p = i < 12 ? c[e[i][0]] : c[i - 12], *q = i < 12 ? c[e[i][1]] : c[7 - (i - 12)];
        f32 t = 1.0f;

        SDL_zero(a);
        SDL_zero(b);
        a.x = p[0], a.y = p[1], a.z = p[2];
        b.x = q[0], b.y = q[1], b.z = q[2];
        if (RayHitsTerrain(&a, &b, &t, NULL) || RayHitsTerrain(&b, &a, &t, NULL)) {
            /* vertical edges (4..7) meeting something: a ceiling or an overhang */
            if (i >= 4 && i < 8) {
                ceiling++;
            } else {
                walls++;
            }
        }
    }
    if (ground == 0) {
        n += SDL_snprintf(out + n, size - (size_t)n, "%sno ground under it", n ? ", " : "");
    } else {
        /* into the ground deeper than the box's own below-origin depth (+ 0.05): the origin
         * itself below the ground */
        if (highest > pos[1] + 0.05f) {
            n += SDL_snprintf(out + n, size - (size_t)n, "%sits box %.2f into the ground (deeper than the cars' %.2f "
                              "below their origin)", n ? ", " : "", (double)(highest - bottom), (double)-lo[1]);
        } else if (pos[1] - highest > 3.0f) {
            n += SDL_snprintf(out + n, size - (size_t)n, "%s%.1f above the ground", n ? ", " : "", (double)(pos[1] - highest));
        }
        if (highest - lowest > 3.0f || ground < 5) {
            gaps = 1;
            n += SDL_snprintf(out + n, size - (size_t)n, "%sover a ledge or slope (ground %.1f to %.1f)", n ? ", " : "",
                              (double)lowest, (double)highest);
        }
    }
    if (walls) {
        n += SDL_snprintf(out + n, size - (size_t)n, "%sa wall crosses the box", n ? ", " : "");
    }
    if (ceiling) {
        n += SDL_snprintf(out + n, size - (size_t)n, "%sa ceiling or overhang inside the box", n ? ", " : "");
    }
    (void)gaps;
    return n == 0;
}

/* viewer_world.c's Collect: the level's volumes and the marks. */
void ViewerLevel_Collect(void) {
    static const u8 levelCol[3] = { 255, 80, 220 };
    const void *v;
    const char *id;
    int i;

    for (i = 0; s_levelOverlay && Port_LevelVolume(i, &v, &id); i++) {
        const WorldVolume *vol = v;
        f32 c[3] = { vol->center.x, vol->center.y, vol->center.z };

        if (vol->shape == VOLUME_SHAPE_SPHERE && vol->radiusSq > 0.0f) {
            Sphere(c, sqrtf(vol->radiusSq), levelCol);
        } else {
            Cross(c, 5.0f, levelCol);
        }
    }
    for (i = 0; i < s_numMarks; i++) {
        const Mark *m = &s_marks[i];

        switch (m->kind) {
        case 's':
            Sphere(m->pos, m->radius > 0.0f ? m->radius : 5.0f, m->color);
            break;
        case 'a':
            Arrow(m->pos, m->rotY, m->radius > 0.0f ? m->radius : 8.0f, m->color);
            break;
        case 'l':
            ViewerWorld_Line(m->pos, m->to, m->color[0], m->color[1], m->color[2]);
            break;
        case 'b':
            ViewerLevel_SpawnBox(m->pos, m->rotY, 0.0f, 0.0f, m->color);
            Arrow(m->pos, m->rotY, 10.0f, m->color);
            break;
        default:
            Cross(m->pos, m->radius > 0.0f ? m->radius : 3.0f, m->color);
            break;
        }
    }
}

int ViewerLevel_HasLines(void) {
    const void *v;
    const char *id;

    return s_numMarks > 0 || (s_levelOverlay && Port_LevelVolume(0, &v, &id));
}

/* ---- the World's records by structural path ------------------------------------ */

typedef struct Table {
    const char *name, *schema;
    u32 countOff, ptrOff, size;
    int slots;          /* a table of pointers (timers, variables): /to is the record */
} Table;

static const Table s_tables[] = {
    { "tileCars", "TileCar", 0x08, 0x0C, 0x60, 0 },
    { "tilePaths", "TilePath", 0x10, 0x14, 0x20, 0 },
    { "quads", "WorldQuad", 0x20, 0x24, 0x50, 0 },
    { "teleporters", "Teleporter", 0x30, 0x34, 0x70, 0 },
    { "tileCreatures", "TileCreature", 0x40, 0x44, 0x40, 0 },
    { "variables", "WorldVariable", 0x48, 0x4C, 4, 1 },
    { "timers", "WorldTimer", 0x50, 0x54, 4, 1 },
    { "objectives", "WorldObjective", 0x58, 0x5C, 0x08, 0 },
    { "volumes", "WorldVolume", 0x68, 0x6C, 0x30, 0 },
};

static const Table s_quadTables[] = {
    { "instances", "QuadInstance", 0x04, 0x08, 0x40, 0 },
    { "fxPoints", "QuadFxPoint", 0x0C, 0x10, 0x40, 0 },
    { "items", "QuadItem", 0x14, 0x18, 0x40, 0 },
};

static VPack *WorldPack(void) {
    VPack *vp = Viewer_ShownPack();

    return ViewerWorld_Active() && vp != NULL && vp->type == 4 ? vp : NULL;
}

static u8 *Blob(VPack *vp) {
    return GETP(vp->pack->base);
}

static int InPack(VPack *vp, const u8 *p, u32 size) {
    const u8 *b = Blob(vp);

    return p != NULL && p >= b && (u32)(p - b) <= vp->size && size <= vp->size - (u32)(p - b);
}

static const Table *TableNamed(const Table *t, int n, const char *name, size_t len) {
    int i;

    for (i = 0; i < n; i++) {
        if (SDL_strlen(t[i].name) == len && SDL_strncmp(t[i].name, name, len) == 0) {
            return &t[i];
        }
    }
    return NULL;
}

static size_t SegLen(const char *p) {
    const char *e = SDL_strchr(p, '/');

    return e != NULL ? (size_t)(e - p) : SDL_strlen(p);
}

static int Num(const char **s, u32 *out) {
    char *end;
    unsigned long v;

    if (!SDL_isdigit((unsigned char)**s)) {
        return 0;
    }
    v = SDL_strtoul(*s, &end, 10);
    if (*end != '/' && *end != 0) {
        return 0;
    }
    *out = (u32)v;
    *s = end;
    return 1;
}

/* The base record a path names: its address in the pack and its struct. 0 and why. */
static int Resolve(VPack *vp, const char *path, u8 **at, const char **schema, char *err, size_t errSize) {
    u8 *base = Blob(vp), *rec = base;
    const char *p = path;
    const Table *t;
    u32 i, n;

    if (SDL_strncmp(p, "World/0", 7) != 0 || (p[7] != 0 && p[7] != '/')) {
        SDL_snprintf(err, errSize, "%s: a path from World/0", path);
        return 0;
    }
    p += 7;
    *schema = "World";
    if (*p == 0) {
        *at = base;
        return 1;
    }
    p++;
    if (SDL_strcmp(p, "gameData") == 0) {
        *at = Port_HandleToPtr(*(u32 *)(base + 0x9C));
        *schema = "WorldGameData";
        return InPack(vp, *at, 0x110) || (SDL_snprintf(err, errSize, "no gameData"), 0);
    }
    t = TableNamed(s_tables, SDL_arraysize(s_tables), p, SegLen(p));
    p += SegLen(p);
    if (t == NULL || *p != '/' || (p++, !Num(&p, &i))) {
        SDL_snprintf(err, errSize, "%s: World/0/<table>/<n> (tileCars, volumes, timers, variables, objectives, "
                                   "teleporters, tilePaths, tileCreatures, quads)", path);
        return 0;
    }
    n = *(u32 *)(base + t->countOff);
    if (i >= n) {
        SDL_snprintf(err, errSize, "%s: the world has %u %s", path, (unsigned)n, t->name);
        return 0;
    }
    rec = (u8 *)Port_HandleToPtr(*(u32 *)(base + t->ptrOff)) + i * t->size;
    if (t->slots) {
        /* timers and variables: the table's entry (what scripts name); /to the record */
        if (SDL_strcmp(p, "/to") != 0 && *p != 0) {
            SDL_snprintf(err, errSize, "%s: %s/<n>/to", path, t->name);
            return 0;
        }
        rec = Port_HandleToPtr(*(u32 *)rec);
        p += *p ? 3 : 0;
    }
    *schema = t->schema;
    if (SDL_strcmp(t->name, "quads") == 0 && SDL_strncmp(p, "/contents", 9) == 0) {
        u8 *qc = Port_HandleToPtr(*(u32 *)(rec + 0x30));

        p += 9;
        *schema = "QuadContents";
        rec = qc;
        if (*p == '/') {
            p++;
            t = TableNamed(s_quadTables, SDL_arraysize(s_quadTables), p, SegLen(p));
            p += SegLen(p);
            if (t == NULL || qc == NULL || *p != '/' || (p++, !Num(&p, &i))) {
                SDL_snprintf(err, errSize, "%s: .../contents/<instances|fxPoints|items>/<n>", path);
                return 0;
            }
            n = *(u32 *)(qc + t->countOff);
            if (i >= n) {
                SDL_snprintf(err, errSize, "%s: the quadrant has %u %s", path, (unsigned)n, t->name);
                return 0;
            }
            rec = (u8 *)Port_HandleToPtr(*(u32 *)(qc + t->ptrOff)) + i * t->size;
            *schema = t->schema;
        }
    }
    if (*p != 0) {
        SDL_snprintf(err, errSize, "%s: %s isn't a record of the world", path, p);
        return 0;
    }
    if (!InPack(vp, rec, 4)) {
        SDL_snprintf(err, errSize, "%s: not in the world pack", path);
        return 0;
    }
    *at = rec;
    return 1;
}

static void Vec(JsonWriter *w, const char *key, const Vector4 *v) {
    Jw_BeginArray(w, key, 1);
    Jw_Float(w, NULL, v->x);
    Jw_Float(w, NULL, v->y);
    Jw_Float(w, NULL, v->z);
    Jw_EndArray(w);
}

/* One entry of `records`. */
static void Entry(JsonWriter *w, const char *table, const char *path, const u8 *rec) {
    Jw_BeginObject(w, NULL, 1);
    Jw_String(w, "path", path);
    if (SDL_strcmp(table, "tileCars") == 0) {
        const TileCar *c = (const TileCar *)rec;

        Vec(w, "pos", &c->pos);
        Jw_Float(w, "rotY", c->rotY);
        Jw_Int(w, "port", c->port);
    } else if (SDL_strcmp(table, "volumes") == 0) {
        const WorldVolume *v = (const WorldVolume *)rec;

        Jw_String(w, "shape", v->shape == VOLUME_SHAPE_SPHERE ? "sphere" : "bsp");
        Jw_Int(w, "type", (s32)v->type);
        Jw_Int(w, "enabled", (s32)v->enabled);
        if (v->shape == VOLUME_SHAPE_SPHERE) {
            Vec(w, "pos", &v->center);
            Jw_Float(w, "radius", v->radiusSq > 0.0f ? sqrtf(v->radiusSq) : 0.0f);
        }
    } else if (SDL_strcmp(table, "items") == 0 || SDL_strcmp(table, "fxPoints") == 0 ||
               SDL_strcmp(table, "instances") == 0 || SDL_strcmp(table, "tileCreatures") == 0) {
        Vec(w, "pos", (const Vector4 *)rec);
        if (SDL_strcmp(table, "items") == 0) {
            const QuadItem *it = (const QuadItem *)rec;

            Jw_Int(w, "id", it->id);
        }
    } else if (SDL_strcmp(table, "teleporters") == 0) {
        const Teleporter *t = (const Teleporter *)rec;
        Vector4 p = t->mtx.v[3];

        Vec(w, "pos", &p);
    } else if (SDL_strcmp(table, "timers") == 0) {
        const WorldTimer *t = (const WorldTimer *)rec;

        if (t != NULL) {
            Jw_Float(w, "time", t->time);
            Jw_Int(w, "type", t->type);
            Jw_Int(w, "countUp", t->countUp);
        }
    } else if (SDL_strcmp(table, "variables") == 0) {
        const WorldVariable *v = (const WorldVariable *)rec;

        if (v != NULL) {
            Jw_Float(w, "value", v->value);
            Jw_Int(w, "id", v->id);
        }
    }
    Jw_EndObject(w);
}

static void Records(JsonWriter *w, VPack *vp, const char *table) {
    u8 *base = Blob(vp);
    const Table *t = TableNamed(s_tables, SDL_arraysize(s_tables), table, SDL_strlen(table));
    const Table *qt = TableNamed(s_quadTables, SDL_arraysize(s_quadTables), table, SDL_strlen(table));
    char path[96];
    u32 i, q;

    Jw_BeginArray(w, "records", 0);
    if (t != NULL) {
        u32 n = *(u32 *)(base + t->countOff);
        u8 *arr = Port_HandleToPtr(*(u32 *)(base + t->ptrOff));

        for (i = 0; arr != NULL && i < n; i++) {
            u8 *rec = arr + i * t->size;

            if (t->slots) {
                rec = Port_HandleToPtr(*(u32 *)rec);
            }
            SDL_snprintf(path, sizeof(path), "World/0/%s/%u", t->name, (unsigned)i);
            Entry(w, table, path, rec);
        }
    } else if (qt != NULL) {
        World *wd = (World *)base;

        for (q = 0; q < wd->numQuads; q++) {
            u8 *qc = GETP(GETP(wd->quads)[q].contents) != NULL ? (u8 *)GETP(GETP(wd->quads)[q].contents) : NULL;
            u32 n = qc != NULL ? *(u32 *)(qc + qt->countOff) : 0;
            u8 *arr = qc != NULL ? Port_HandleToPtr(*(u32 *)(qc + qt->ptrOff)) : NULL;

            for (i = 0; arr != NULL && i < n; i++) {
                SDL_snprintf(path, sizeof(path), "World/0/quads/%u/contents/%s/%u", (unsigned)q, qt->name, (unsigned)i);
                Entry(w, table, path, arr + i * qt->size);
            }
        }
    }
    Jw_EndArray(w);
}

/* A volume's settings by its type (WorldVolume.data; VolumeManager.c, Thing.c, LightManager.c):
 * "volumeData": {...} or null. */
static void WriteVolumeData(JsonWriter *w, VPack *vp, const WorldVolume *v) {
    const u8 *d = Port_HandleToPtr(v->data.h);
    u32 lo = Port_PtrToHandle(Blob(vp));

    if (v->data.h == 0 || v->data.h - lo >= vp->size) {
        Viewer_WriteNull(w, "volumeData");
        return;
    }
    Jw_BeginObject(w, "volumeData", 1);
    switch (v->type) {
    case VOLUME_FORCE_BOX:
        Jw_String(w, "kind", "force_box");
        Jw_BeginArray(w, "force", 1);
        Jw_Float(w, NULL, ((const f32 *)d)[0]);
        Jw_Float(w, NULL, ((const f32 *)d)[1]);
        Jw_Float(w, NULL, ((const f32 *)d)[2]);
        Jw_EndArray(w);
        Jw_Int(w, "oneShot", (s8)d[0x10]);
        Jw_Int(w, "carOnly", (s8)d[0x11]);
        Jw_Int(w, "type", *(const u16 *)(d + 0x12));
        break;
    case VOLUME_REVERB:
        Jw_String(w, "kind", "reverb");
        Jw_Int(w, "type", *(const u32 *)d);
        Jw_Int(w, "volume", *(const u16 *)(d + 4));
        Jw_Int(w, "feedback", *(const u16 *)(d + 6));
        break;
    case VOLUME_FOG:
        Jw_String(w, "kind", "fog");
        Jw_Int(w, "type", d[0]);
        Jw_Float(w, "near", *(const f32 *)(d + 4));
        Jw_Float(w, "far", *(const f32 *)(d + 8));
        Jw_BeginArray(w, "color", 1);
        Jw_Int(w, NULL, d[12]);
        Jw_Int(w, NULL, d[13]);
        Jw_Int(w, NULL, d[14]);
        Jw_Int(w, NULL, d[15]);
        Jw_EndArray(w);
        break;
    case VOLUME_LIGHT: {
        const Light *l = (const Light *)d;

        Jw_String(w, "kind", "light");
        Jw_BeginArray(w, "direction", 1);
        Jw_Float(w, NULL, l->direction.x);
        Jw_Float(w, NULL, l->direction.y);
        Jw_Float(w, NULL, l->direction.z);
        Jw_EndArray(w);
        Jw_BeginArray(w, "color", 1);
        Jw_Int(w, NULL, l->r);
        Jw_Int(w, NULL, l->g);
        Jw_Int(w, NULL, l->b);
        Jw_EndArray(w);
        Jw_Int(w, "lightType", l->type);
        Jw_Int(w, "subtract", l->subtract);
        break;
    }
    case VOLUME_CREATURE:
        Jw_String(w, "kind", "creature");
        Jw_Int(w, "rescue", d[0]);
        break;
    default:
        Jw_String(w, "kind", "other");
        break;
    }
    Jw_EndObject(w);
}

/* ---- the commands ------------------------------------------------------------- */

static int ParseColor(const JsonNode *c, u8 *out) {
    int k;

    if (c == NULL || c->type != JSON_ARRAY || c->count != 3) {
        return 0;
    }
    for (k = 0; k < 3; k++) {
        f64 v = Json_At(c, k)->number;

        out[k] = (u8)(v < 0 ? 0 : v > 255 ? 255 : v);
    }
    return 1;
}

static int ParseVec(const JsonNode *a, f32 *out) {
    int k;

    if (a == NULL || a->type != JSON_ARRAY || a->count != 3) {
        return 0;
    }
    for (k = 0; k < 3; k++) {
        out[k] = (f32)Json_At(a, k)->number;
    }
    return 1;
}

/* "box": the envelope the starts are shown and checked with, and how many cars it is of. */
static void WriteEnvelope(JsonWriter *w) {
    f32 lo[3], hi[3];
    int cars, k;

    Port_SpawnEnvelope(lo, hi, &cars, 1);
    Jw_BeginObject(w, "box", 1);
    Jw_Int(w, "cars", cars);
    Jw_BeginArray(w, "min", 1);
    for (k = 0; k < 3; k++) {
        Jw_Float(w, NULL, lo[k]);
    }
    Jw_EndArray(w);
    Jw_BeginArray(w, "max", 1);
    for (k = 0; k < 3; k++) {
        Jw_Float(w, NULL, hi[k]);
    }
    Jw_EndArray(w);
    Jw_EndObject(w);
}

int ViewerLevel_Dispatch(const char *cmd, const JsonNode *req, JsonWriter *w, int *ok, char *err, size_t errSize) {
    VPack *vp;

    if (SDL_strcmp(cmd, "spawnbox") == 0) {
        /* the starts' box (Port_SpawnBox.h); with `path`, also written there as JSON for
         * tools/xdl_level.py check --spawn-box */
        const char *path = Json_GetString(req, "path", NULL);

        WriteEnvelope(w);
        if (path != NULL) {
            JsonWriter f;
            SDL_IOStream *io;

            Jw_Init(&f);
            Jw_BeginObject(&f, NULL, 0);
            Jw_String(&f, "about", "every car's collision box at once (Port_SpawnBox.h), from the viewer's spawnbox");
            WriteEnvelope(&f);
            Jw_EndObject(&f);
            io = f.buf != NULL ? SDL_IOFromFile(path, "w") : NULL;
            if (io == NULL || SDL_WriteIO(io, f.buf, f.len) != f.len) {
                SDL_snprintf(err, errSize, "%s: can't write it", path);
                *ok = 0;
            }
            if (io != NULL) {
                SDL_CloseIO(io);
            }
            Jw_Free(&f);
        }
        return 1;
    }

    if (SDL_strcmp(cmd, "level") == 0) {
        const JsonNode *path = Json_Get(req, "path");
        const JsonNode *overlay = Json_Get(req, "overlay");

        if (overlay != NULL && overlay->type == JSON_BOOL) {
            s_levelOverlay = overlay->boolean;
        }
        if ((vp = WorldPack()) == NULL) {
            SDL_snprintf(err, errSize, "level needs a world shown (world)");
            *ok = 0;
        } else if (path != NULL && path->type == JSON_STRING) {
            if (!Port_LevelApply(vp->pack, vp->archive, path->string, err, errSize)) {
                *ok = 0;
            }
        } else if (path != NULL) {
            Port_LevelPackReleased(vp->pack);   /* taken out */
        }
        if (*ok) {
            const char *file = NULL, *id = Port_LevelCurrent(&file);

            if (id != NULL) {
                Jw_String(w, "level", id);
                Jw_String(w, "path", file);
            } else {
                Viewer_WriteNull(w, "level");
            }
            Jw_Bool(w, "overlay", s_levelOverlay);
        }
        return 1;
    }
    if (SDL_strcmp(cmd, "records") == 0) {
        const char *table = Json_GetString(req, "table", "");

        if ((vp = WorldPack()) == NULL) {
            SDL_snprintf(err, errSize, "records needs a world shown (world)");
            *ok = 0;
        } else if (TableNamed(s_tables, SDL_arraysize(s_tables), table, SDL_strlen(table)) == NULL &&
                   TableNamed(s_quadTables, SDL_arraysize(s_quadTables), table, SDL_strlen(table)) == NULL) {
            SDL_snprintf(err, errSize, "records takes table: tileCars, volumes, timers, variables, objectives, "
                                       "teleporters, tilePaths, tileCreatures, quads, items, fxPoints, instances");
            *ok = 0;
        } else {
            Jw_String(w, "table", table);
            Records(w, vp, table);
        }
        return 1;
    }
    if (SDL_strcmp(cmd, "record") == 0) {
        const char *path = Json_GetString(req, "path", ""), *schema = NULL;
        u8 *at = NULL;

        if ((vp = WorldPack()) == NULL) {
            SDL_snprintf(err, errSize, "record needs a world shown (world)");
            *ok = 0;
        } else if (!Resolve(vp, path, &at, &schema, err, errSize)) {
            *ok = 0;
        } else {
            char hex[16];

            Jw_String(w, "path", path);
            Jw_String(w, "struct", schema);
            SDL_snprintf(hex, sizeof(hex), "@%x", (unsigned)(at - Blob(vp)));
            Jw_String(w, "at", hex);
            if (!Viewer_WriteStruct(w, "fields", vp, schema, (u32)(at - Blob(vp)))) {
                Viewer_WriteNull(w, "fields");
            }
            if (SDL_strcmp(schema, "WorldVolume") == 0) {
                WriteVolumeData(w, vp, (const WorldVolume *)at);
            }
        }
        return 1;
    }
    if (SDL_strcmp(cmd, "quadat") == 0) {
        /* the quadrant (streaming region, BSPN0) a point is in: an item placed there goes into
         * its contents. -1: none (the hub only) */
        f32 p[3];
        s32 found = -1;
        u32 q;

        if (WorldPack() == NULL || !ParseVec(Json_Get(req, "pos"), p)) {
            SDL_snprintf(err, errSize, WorldPack() == NULL ? "quadat needs a world shown (world)" : "quadat takes pos [x, y, z]");
            *ok = 0;
            return 1;
        }
        {
            World *wd = (World *)Blob(WorldPack());
            Vector4 v __attribute__((aligned(16)));

            v.x = p[0], v.y = p[1], v.z = p[2], v.w = 1.0f;
            for (q = 0; q < wd->numQuads && found < 0; q++) {
                WorldQuad *quad = &GETP(wd->quads)[q];

                if (GETP(quad->bsp) != NULL && BSPN0_CollideWithVector(GETP(quad->bsp), &v)) {
                    found = (s32)q;
                }
            }
        }
        Jw_Int(w, "quad", found);
        return 1;
    }
    if (SDL_strcmp(cmd, "startcheck") == 0) {
        /* starts as the largest car's box (Port_SpawnBox.h) against the world's collision:
         * {starts: [{pos, rotY}...]} -> per start {clear, problems, seat} */
        const JsonNode *list = Json_Get(req, "starts"), *st;

        if (WorldPack() == NULL || list == NULL || list->type != JSON_ARRAY) {
            SDL_snprintf(err, errSize, WorldPack() == NULL ? "startcheck needs a world shown (world)"
                                                            : "startcheck takes starts: [{pos, rotY}]");
            *ok = 0;
            return 1;
        }
        Jw_BeginArray(w, "starts", 0);
        for (st = list->child; st != NULL; st = st->next) {
            f32 p[3], seat;
            char why[256];
            int clear;

            if (!ParseVec(Json_Get(st, "pos"), p)) {
                Viewer_WriteNull(w, NULL);
                continue;
            }
            clear = CheckBox(p, (f32)Json_GetNumber(st, "rotY", 0.0), &seat, why, sizeof(why));
            Jw_BeginObject(w, NULL, 1);
            Jw_Bool(w, "clear", clear);
            Jw_String(w, "problems", why);
            Jw_Float(w, "seat", seat);
            Jw_EndObject(w);
        }
        Jw_EndArray(w);
        WriteEnvelope(w);
        return 1;
    }
    if (SDL_strcmp(cmd, "marks") == 0) {
        const JsonNode *list = Json_Get(req, "marks"), *m;

        s_numMarks = 0;
        for (m = list != NULL && list->type == JSON_ARRAY ? list->child : NULL; m != NULL && s_numMarks < MAX_MARKS;
             m = m->next) {
            Mark *k = &s_marks[s_numMarks];
            const char *kind = Json_GetString(m, "kind", "cross");

            SDL_zero(*k);
            k->kind = SDL_strcmp(kind, "sphere") == 0 ? 's' : SDL_strcmp(kind, "arrow") == 0 ? 'a'
                      : SDL_strcmp(kind, "line") == 0 ? 'l' : SDL_strcmp(kind, "start") == 0 ? 'b' : 'c';
            if (!ParseVec(Json_Get(m, "pos"), k->pos) || (k->kind == 'l' && !ParseVec(Json_Get(m, "to"), k->to))) {
                SDL_snprintf(err, errSize, "marks: each has pos [x, y, z] (a line also to)");
                *ok = 0;
                s_numMarks = 0;
                return 1;
            }
            k->radius = (f32)Json_GetNumber(m, "radius", 0.0);
            k->rotY = (f32)Json_GetNumber(m, "rotY", 0.0);
            if (!ParseColor(Json_Get(m, "color"), k->color)) {
                k->color[0] = 255, k->color[1] = 240, k->color[2] = 40;
            }
            s_numMarks++;
        }
        Jw_Int(w, "marks", s_numMarks);
        return 1;
    }
    return 0;
}

/* The world went: its marks with it. */
void ViewerLevel_WorldGone(void) {
    s_numMarks = 0;
}

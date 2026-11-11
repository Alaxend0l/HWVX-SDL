/* Viewer mode: a loaded pack as a graph of named nodes, and its fields by
 * the schemas (schemas/<Struct>.schema.json, generated from the C structs by
 * tools/gen_schemas.py).
 *
 * The names follow tools/xdl_graph.py (docs/xdl-format/README.md §10.8): fixed
 * ids (geometry/N in header-scan order, texture/N, shader/N, trigger/N, the
 * root table's entries, header/0), and every other node by the first path
 * that reaches it breadth-first from those, a step being the field path
 * where a schema covers the slot (CarAInfo/0/engine) or p<hex offset>
 * elsewhere, repeats counted (p0_r12). Differences from the tools, which
 * work on the files: nodes nothing reaches are not listed (no orphan/K), a
 * geometry claims only its header here, world objects the tools find by
 * their research walkers are raw nodes, and geometry and texture numbers
 * are this release's own (the tools map PC and PS2 ones to the GameCube's
 * through the pairing tables). */
#include "viewer_internal.h"

#define MAX_NODES 200000

/* ---- schemas ------------------------------------------------------------ */

static JsonNode *s_schemas[64];
static int s_numSchemas;

typedef struct SchemaScan {
    const char *dir;
    int count;
} SchemaScan;

static SDL_EnumerationResult SDLCALL ScanSchema(void *userdata, const char *dir, const char *name) {
    SchemaScan *scan = userdata;
    size_t n = SDL_strlen(name), size;
    char *path = NULL, *text;
    char err[128];
    JsonNode *root;

    if (n < 12 || SDL_strcmp(name + n - 12, ".schema.json") != 0 || s_numSchemas >= (int)SDL_arraysize(s_schemas)) {
        return SDL_ENUM_CONTINUE;
    }
    if (SDL_asprintf(&path, "%s%s", dir, name) < 0) {
        return SDL_ENUM_CONTINUE;
    }
    text = SDL_LoadFile(path, &size);
    SDL_free(path);
    if (text == NULL) {
        return SDL_ENUM_CONTINUE;
    }
    root = Json_Parse(text, size, err, sizeof(err));
    SDL_free(text);
    if (root == NULL) {
        Port_Log("viewer: schema %s: %s", name, err);
        return SDL_ENUM_CONTINUE;
    }
    /* a pack struct's schema; the media tools' (no x-c-struct) aren't pack data */
    if (Json_GetString(root, "x-c-struct", NULL) == NULL || Json_GetString(root, "title", NULL) == NULL) {
        Json_Free(root);
        return SDL_ENUM_CONTINUE;
    }
    s_schemas[s_numSchemas++] = root;
    scan->count++;
    return SDL_ENUM_CONTINUE;
}

int Viewer_LoadSchemas(const char *dir) {
    SchemaScan scan = { dir, 0 };
    int i;

    for (i = 0; i < s_numSchemas; i++) {
        Json_Free(s_schemas[i]);
    }
    s_numSchemas = 0;
    SDL_EnumerateDirectory(dir, ScanSchema, &scan);
    return scan.count;
}

const JsonNode *Viewer_Schema(const char *name) {
    int i;

    for (i = 0; name != NULL && i < s_numSchemas; i++) {
        if (SDL_strcmp(Json_GetString(s_schemas[i], "title", ""), name) == 0) {
            return s_schemas[i];
        }
    }
    return NULL;
}

void Viewer_WriteSchemaNames(JsonWriter *w, const char *key) {
    int i;

    Jw_BeginArray(w, key, 1);
    for (i = 0; i < s_numSchemas; i++) {
        Jw_String(w, NULL, Json_GetString(s_schemas[i], "title", ""));
    }
    Jw_EndArray(w);
}

void Viewer_WriteNull(JsonWriter *w, const char *key) {
    static const JsonNode null = { JSON_NULL };

    Jw_Node(w, key, &null);
}

/* ---- C types as the schemas write them ------------------------------------ */

typedef struct CType {
    char base[64];      /* "f32", "PTR32(CarAEngine)", "CarACameras" */
    int dims[4];
    int numDims;
} CType;

static void ParseCType(const char *s, CType *t) {
    const char *b = SDL_strchr(s, '[');
    const char *paren = SDL_strrchr(s, ')');
    size_t n;

    if (paren != NULL && b != NULL && b < paren) {     /* a '[' inside PTR32(...) */
        b = SDL_strchr(paren, '[');
    }
    n = b != NULL ? (size_t)(b - s) : SDL_strlen(s);
    if (n >= sizeof(t->base)) {
        n = sizeof(t->base) - 1;
    }
    SDL_memcpy(t->base, s, n);
    t->base[n] = '\0';
    t->numDims = 0;
    while (b != NULL && *b == '[' && t->numDims < 4) {
        t->dims[t->numDims++] = SDL_atoi(b + 1);
        b = SDL_strchr(b + 1, '[');
    }
}

typedef struct Prim {
    const char *name;
    u32 size;
    int isFloat, isSigned;
} Prim;

static const Prim s_prims[] = {
    { "u8", 1, 0, 0 }, { "s8", 1, 0, 1 }, { "u16", 2, 0, 0 }, { "s16", 2, 0, 1 }, { "u32", 4, 0, 0 },
    { "s32", 4, 0, 1 }, { "f32", 4, 1, 1 }, { "f64", 8, 1, 1 }, { "char", 1, 0, 1 },
};

static const Prim *FindPrim(const char *name) {
    size_t i;

    for (i = 0; i < SDL_arraysize(s_prims); i++) {
        if (SDL_strcmp(s_prims[i].name, name) == 0) {
            return &s_prims[i];
        }
    }
    return NULL;
}

/* A property's embedded struct ("$ref": "#/$defs/Name"), from the schema root. */
static const JsonNode *RefOf(const JsonNode *root, const JsonNode *prop) {
    const char *ref = prop != NULL ? Json_GetString(prop, "$ref", NULL) : NULL;

    if (ref == NULL || SDL_strncmp(ref, "#/$defs/", 8) != 0) {
        return NULL;
    }
    return Json_Get(Json_Get(root, "$defs"), ref + 8);
}

static u32 HexOf(const JsonNode *node, const char *key) {
    const char *s = Json_GetString(node, key, NULL);

    return s != NULL ? (u32)SDL_strtoul(s, NULL, 16) : 0;
}

static u32 BaseSize(const JsonNode *root, const char *base) {
    const Prim *p = FindPrim(base);
    const JsonNode *def;

    if (p != NULL) {
        return p->size;
    }
    def = root != NULL ? Json_Get(Json_Get(root, "$defs"), base) : NULL;
    if (def != NULL) {
        return HexOf(def, "x-size");
    }
    return 4;   /* a pointer, a handle, an enum */
}

/* ---- the packs the loader told us about ------------------------------------ */

typedef struct Noted {
    DataPack *pack;
    u32 *pointers, *refs;
    u32 numPointers, numRefs;
} Noted;

static Noted s_noted[64];

static void FreeNoted(Noted *n) {
    SDL_free(n->pointers);
    SDL_free(n->refs);
    SDL_memset(n, 0, sizeof(*n));
}

void Viewer_ForgetPack(DataPack *pack) {
    size_t i;

    for (i = 0; i < SDL_arraysize(s_noted); i++) {
        if (s_noted[i].pack == pack) {
            FreeNoted(&s_noted[i]);
        }
    }
}

void Port_ViewerNotePack(DataPack *pack, const u32 *pointers, u32 numPointers, const u32 *refs, u32 numRefs) {
    Noted *n = NULL;
    size_t i;

    if (!Port_ViewerOn()) {
        return;
    }
    Viewer_ForgetPack(pack);
    for (i = 0; i < SDL_arraysize(s_noted) && n == NULL; i++) {
        if (s_noted[i].pack == NULL) {
            n = &s_noted[i];
        }
    }
    if (n == NULL) {    /* packs the viewer never asked about: the oldest goes */
        FreeNoted(&s_noted[0]);
        SDL_memmove(&s_noted[0], &s_noted[1], sizeof(s_noted) - sizeof(s_noted[0]));
        n = &s_noted[SDL_arraysize(s_noted) - 1];
        SDL_memset(n, 0, sizeof(*n));
    }
    n->pack = pack;
    n->numPointers = numPointers;
    n->numRefs = numRefs;
    n->pointers = SDL_malloc(numPointers * 4 + 4);
    n->refs = SDL_malloc(numRefs * 8 + 4);
    if (n->pointers == NULL || n->refs == NULL) {
        FreeNoted(n);
        return;
    }
    SDL_memcpy(n->pointers, pointers, numPointers * 4);
    SDL_memcpy(n->refs, refs, numRefs * 8);
}

/* ---- nodes ---------------------------------------------------------------- */

static u8 *Base(VPack *vp) {
    return GETP(vp->pack->base);
}

static u32 RdU32(VPack *vp, u32 off) {
    u32 v;

    SDL_memcpy(&v, Base(vp) + off, 4);
    return v;
}

/* Where the handle in the slot at `off` points in this pack, or
 * VIEWER_NO_OFFSET (null, or another pack's). */
static u32 Target(VPack *vp, u32 off) {
    u32 h, t;

    if (off + 4 > vp->size) {
        return VIEWER_NO_OFFSET;
    }
    h = RdU32(vp, off);
    t = h - (u32)HND(Base(vp));
    return h != 0 && t < vp->size ? t : VIEWER_NO_OFFSET;
}

static int SlotKind(VPack *vp, u32 off) {
    return (off & 3) == 0 && off + 4 <= vp->size ? vp->slotKind[off / 4] : VS_NONE;
}

static int IsPointerSlot(VPack *vp, u32 off) {
    int k = SlotKind(vp, off);

    return k == VS_POINTER || k == VS_REF;
}

static VNode *AddNode(VPack *vp, int kind, const char *id, u32 off, u32 size, s32 parent) {
    VNode *nd;
    u32 i;

    if (vp->numNodes >= MAX_NODES) {
        return NULL;
    }
    if (vp->numNodes == vp->capNodes) {
        u32 cap = vp->capNodes ? vp->capNodes * 2 : 256;
        VNode *p = SDL_realloc(vp->nodes, cap * sizeof(VNode));

        if (p == NULL) {
            return NULL;
        }
        vp->nodes = p;
        vp->capNodes = cap;
    }
    nd = &vp->nodes[vp->numNodes];
    SDL_memset(nd, 0, sizeof(*nd));
    nd->id = SDL_strdup(id);
    nd->kind = (u8)kind;
    nd->off = off;
    nd->size = size;
    nd->parent = parent;
    if (off != VIEWER_NO_OFFSET) {
        /* (a raw node's size comes later: its first word marks it) */
        for (i = off / 4; i < (off + (size ? size : 4) + 3) / 4 && i < vp->size / 4 + 1; i++) {
            if (vp->owner[i] < 0) {
                vp->owner[i] = (s32)vp->numNodes;
            }
        }
    }
    vp->numNodes++;
    return nd;
}

/* The node that starts at `off`, or NULL. */
static VNode *NodeAt(VPack *vp, u32 off) {
    s32 i;

    if (off >= vp->size || (off & 3)) {
        return NULL;
    }
    i = vp->owner[off / 4];
    return i >= 0 && vp->nodes[i].off == off ? &vp->nodes[i] : NULL;
}

/* The link field of `schema` at `rel` bytes into the struct: its path
 * ("aiScripts/2") and what it links to. 0: no link field there. */
static int LinkFieldIn(const JsonNode *root, const JsonNode *props, u32 base, u32 rel, char *path, size_t pathSize,
                       const JsonNode **link);

static int LinkFieldAt(const JsonNode *root, const JsonNode *prop, const CType *t, int dim, u32 off, u32 rel,
                       char *path, size_t pathSize, const JsonNode **link) {
    const JsonNode *def;
    size_t n = SDL_strlen(path);

    if (dim < t->numDims) {
        u32 elem = BaseSize(root, t->base), i;
        int d;

        for (d = dim + 1; d < t->numDims; d++) {
            elem *= (u32)t->dims[d];
        }
        if (elem == 0 || rel < off || (i = (rel - off) / elem) >= (u32)t->dims[dim]) {
            return 0;
        }
        SDL_snprintf(path + n, pathSize - n, "/%u", i);
        return LinkFieldAt(root, Json_Get(prop, "items"), t, dim + 1, off + i * elem, rel, path, pathSize, link);
    }
    if (prop != NULL && Json_Get(prop, "x-link") != NULL) {
        *link = Json_Get(prop, "x-link");
        return off == rel;
    }
    def = RefOf(root, prop);
    if (def != NULL) {
        SDL_strlcat(path, "/", pathSize);
        return LinkFieldIn(root, Json_Get(def, "properties"), off, rel, path, pathSize, link);
    }
    return 0;
}

static int LinkFieldIn(const JsonNode *root, const JsonNode *props, u32 base, u32 rel, char *path, size_t pathSize,
                       const JsonNode **link) {
    const JsonNode *p;
    size_t n = SDL_strlen(path);

    for (p = props != NULL ? props->child : NULL; p != NULL; p = p->next) {
        const char *ctype = Json_GetString(p, "x-c-type", NULL);
        CType t;
        u32 off, size;
        int d;

        if (ctype == NULL || Json_Get(p, "x-offset") == NULL) {
            continue;
        }
        ParseCType(ctype, &t);
        off = base + HexOf(p, "x-offset");
        size = BaseSize(root, t.base);
        for (d = 0; d < t.numDims; d++) {
            size *= (u32)t.dims[d];
        }
        if (rel < off || rel >= off + size) {
            continue;
        }
        SDL_strlcpy(path + n, p->key, pathSize - n);
        if (LinkFieldAt(root, p, &t, 0, off, rel, path, pathSize, link)) {
            return 1;
        }
        path[n] = '\0';
    }
    return 0;
}

/* id + "/" + step, a repeated p-step counted (p0, p0_r2, p0_r3: linked lists). */
static char *StepId(const char *id, const char *step) {
    const char *last = SDL_strrchr(id, '/');
    char *out = NULL;

    last = last != NULL ? last + 1 : id;
    if (step[0] == 'p') {
        size_t n = SDL_strlen(step);

        if (SDL_strncmp(last, step, n) == 0 && (last[n] == '\0' || (last[n] == '_' && last[n + 1] == 'r'))) {
            int count = last[n] == '\0' ? 1 : SDL_atoi(last + n + 2);

            SDL_asprintf(&out, "%.*s%s_r%d", (int)(last - id), id, step, count + 1);
            return out;
        }
    }
    SDL_asprintf(&out, "%s/%s", id, step);
    return out;
}

static const u16 s_geometryMagic[PORT_DATA_COUNT] = {
    [PORT_DATA_GC] = 0x2003, [PORT_DATA_PC] = 0x0002, [PORT_DATA_PS2] = 0x1005,
};

static const char *const s_rootKind[11] = { [0] = "CarAInfo", [1] = "IMHeader", [2] = "ItemRecord", [9] = "SoundSeq" };

void Viewer_FreeGraph(VPack *vp) {
    u32 i;

    for (i = 0; i < vp->numNodes; i++) {
        SDL_free(vp->nodes[i].id);
    }
    SDL_free(vp->nodes);
    SDL_free(vp->slotKind);
    SDL_free(vp->owner);
    SDL_free(vp->refs);
    vp->nodes = NULL;
    vp->slotKind = NULL;
    vp->owner = NULL;
    vp->refs = NULL;
    vp->numNodes = vp->capNodes = vp->numRefs = 0;
}

void Viewer_BuildGraph(VPack *vp) {
    DataPack *dp = vp->pack;
    u8 *base = Base(vp), *cut;
    u32 words = vp->size / 4 + 1, i, head;
    Noted *noted = NULL;
    char id[64];
    VNode *nd;
    u16 magic = dp->portFormat < PORT_DATA_COUNT ? s_geometryMagic[dp->portFormat] : 0;

    Viewer_FreeGraph(vp);
    vp->slotKind = SDL_calloc(words, 1);
    vp->owner = SDL_malloc(words * sizeof(s32));
    cut = SDL_calloc(words, 1);
    if (vp->slotKind == NULL || vp->owner == NULL || cut == NULL) {
        SDL_free(cut);
        return;
    }
    SDL_memset(vp->owner, 0xFF, words * sizeof(s32));
    for (i = 0; i < SDL_arraysize(s_noted); i++) {
        if (s_noted[i].pack == dp) {
            noted = &s_noted[i];
        }
    }
    if (noted != NULL) {
        for (i = 0; i < noted->numPointers; i++) {
            if (!(noted->pointers[i] & 3) && noted->pointers[i] + 4 <= vp->size) {
                vp->slotKind[noted->pointers[i] / 4] = VS_POINTER;
            }
        }
        for (i = 0; i < noted->numRefs; i++) {
            u32 slot = noted->refs[i * 2];

            if (!(slot & 3) && slot + 4 <= vp->size) {
                vp->slotKind[slot / 4] = (noted->refs[i * 2 + 1] >> 16) ? VS_EXPORT : VS_REF;
            }
        }
        vp->refs = noted->refs;
        vp->numRefs = noted->numRefs;
        noted->refs = NULL;
        FreeNoted(noted);
    }
    for (i = 0; i < dp->numRelocs; i++) {
        u32 slot = (u32)GETP(dp->relocs)[i * 2] & ~PORT_TEXRELOC_U16;

        if (slot + 4 <= vp->size) {
            vp->slotKind[slot / 4] = VS_TEXTURE;
        }
    }
    for (i = 0; i < dp->numSoundRelocs; i++) {
        u32 slot = GETP(dp->soundRelocs)[i * 2];

        if (slot + 4 <= vp->size) {
            vp->slotKind[slot / 4] = VS_SOUND;
        }
    }
    /* where nodes can end: every pointer's target */
    for (i = 0; i < words - 1; i++) {
        if (vp->slotKind[i] == VS_POINTER || vp->slotKind[i] == VS_REF) {
            u32 t = Target(vp, i * 4);

            if (t != VIEWER_NO_OFFSET && !(t & 3)) {
                cut[t / 4] = 1;
            }
        }
    }

    /* fixed ids */
    for (i = 0; i < dp->numTextures; i++) {
        SDL_snprintf(id, sizeof(id), "texture/%u", i);
        if ((nd = AddNode(vp, VN_TEXTURE, id, VIEWER_NO_OFFSET, 0, -1)) != NULL) {
            nd->index = i;
        }
    }
    if (magic != 0) {
        u32 n = 0;

        for (i = 0; i + sizeof(Geometry) <= vp->size; i += 4) {
            if (*(u16 *)(base + i) == magic && IsPointerSlot(vp, i + 8) && IsPointerSlot(vp, i + 0xC) &&
                ((Geometry *)(base + i))->numGroups >= 1 && ((Geometry *)(base + i))->numGroups <= 8) {
                SDL_snprintf(id, sizeof(id), "geometry/%u", n);
                if ((nd = AddNode(vp, VN_GEOMETRY, id, i, sizeof(Geometry), -1)) != NULL) {
                    nd->index = n;
                    nd->type = "Geometry";
                }
                cut[i / 4] = 1;
                n++;
            }
        }
    }
    head = vp->numNodes;    /* the breadth-first queue starts here */
    /* the root table: a count, then a pointer to {id, pointer} entries */
    if (vp->type >= 0 && vp->type < 11 && s_rootKind[vp->type] != NULL && vp->size >= 8) {
        u32 n = RdU32(vp, 0), tab = IsPointerSlot(vp, 4) ? Target(vp, 4) : VIEWER_NO_OFFSET;
        const JsonNode *schema = Viewer_Schema(s_rootKind[vp->type]);

        for (i = 0; tab != VIEWER_NO_OFFSET && i < n && i < 0x1000 && tab + i * 8 + 8 <= vp->size; i++) {
            u32 o = IsPointerSlot(vp, tab + i * 8 + 4) ? Target(vp, tab + i * 8 + 4) : VIEWER_NO_OFFSET;
            u32 size = schema != NULL ? HexOf(schema, "x-size") : 0;

            /* (the entry's pointer is its second word on every release) */
            if (o == VIEWER_NO_OFFSET) {
                o = IsPointerSlot(vp, tab + i * 8) ? Target(vp, tab + i * 8) : VIEWER_NO_OFFSET;
            }
            if (o == VIEWER_NO_OFFSET || (o & 3) || vp->owner[o / 4] >= 0) {
                continue;
            }
            SDL_snprintf(id, sizeof(id), "%s/%u", s_rootKind[vp->type], i);
            if (schema != NULL && o + size <= vp->size) {
                if ((nd = AddNode(vp, VN_TYPED, id, o, size, -1)) != NULL) {
                    nd->schema = schema;
                }
            } else {
                nd = AddNode(vp, VN_RAW, id, o, 0, -1);
            }
            if (nd != NULL) {
                nd->type = s_rootKind[vp->type];
                nd->index = i;
            }
        }
    }
    if (dp->numShaders != 0) {
        u32 off = (u32)((u8 *)GETP(dp->shaders) - base);

        for (i = 0; i < dp->numShaders && off + (i + 1) * sizeof(DataPackShader) <= vp->size; i++) {
            SDL_snprintf(id, sizeof(id), "shader/%u", i);
            if (NodeAt(vp, off + i * (u32)sizeof(DataPackShader)) == NULL &&
                (nd = AddNode(vp, VN_SHADER, id, off + i * (u32)sizeof(DataPackShader), sizeof(DataPackShader), -1)) != NULL) {
                nd->index = i;
            }
        }
    }
    if (dp->numTriggers != 0) {
        u32 off = (u32)(GETP(dp->triggers) - base);

        for (i = 0; i < dp->numTriggers && off + (i + 1) * 0x2C <= vp->size; i++) {
            SDL_snprintf(id, sizeof(id), "trigger/%u", i);
            if (NodeAt(vp, off + i * 0x2C) == NULL && (nd = AddNode(vp, VN_TRIGGER, id, off + i * 0x2C, 0x2C, -1)) != NULL) {
                nd->index = i;
                nd->type = "Trigger";
            }
        }
    }
    if (vp->owner[0] < 0) {
        AddNode(vp, VN_RAW, vp->type == 4 ? "World/0" : "header/0", 0, 0, -1);
    }
    /* breadth-first: each node's pointer slots in offset order */
    for (; head < vp->numNodes; head++) {
        u32 o, off = vp->nodes[head].off, end;

        if (vp->nodes[head].kind == VN_RAW && vp->nodes[head].size == 0) {
            /* a raw node runs to the next pointer target or node */
            for (i = off / 4 + 1; i < words - 1 && !cut[i] && (vp->owner[i] < 0 || vp->owner[i] == (s32)head); i++) {
            }
            vp->nodes[head].size = i * 4 - off > vp->size - off ? vp->size - off : i * 4 - off;
            for (i = off / 4; i < (off + vp->nodes[head].size + 3) / 4; i++) {
                if (vp->owner[i] < 0) {
                    vp->owner[i] = (s32)head;
                }
            }
        }
        end = off + vp->nodes[head].size;
        for (o = off; o + 4 <= end; o += 4) {
            const JsonNode *link = NULL, *schema = NULL;
            char step[160] = "";
            const char *type = NULL;
            char *cid;
            u32 t, size = 0;

            if (!IsPointerSlot(vp, o) || (t = Target(vp, o)) == VIEWER_NO_OFFSET || (t & 3) || vp->owner[t / 4] >= 0) {
                continue;   /* no pointer, or into a node there already is: a link */
            }
            if (vp->nodes[head].schema != NULL &&
                LinkFieldIn(vp->nodes[head].schema, Json_Get(vp->nodes[head].schema, "properties"), 0, o - off, step,
                            sizeof(step), &link)) {
                type = Json_GetString(link, "target", NULL);
                schema = Viewer_Schema(type);
                if (type != NULL && (SDL_strcmp(type, "void") == 0 || type[0] == '\0')) {
                    type = NULL;
                }
            } else {
                SDL_snprintf(step, sizeof(step), "p%x", o - off);
            }
            if (schema != NULL) {
                size = HexOf(schema, "x-size");
                for (i = t / 4; i < (t + size + 3) / 4 && i < words; i++) {
                    if (vp->owner[i] >= 0) {
                        schema = NULL;      /* overlaps another node: not that struct */
                    }
                }
                if (t + size > vp->size) {
                    schema = NULL;
                }
            }
            cid = StepId(vp->nodes[head].id, step);
            if (cid == NULL) {
                continue;
            }
            nd = AddNode(vp, schema != NULL ? VN_TYPED : VN_RAW, cid, t, schema != NULL ? size : 0, (s32)head);
            SDL_free(cid);
            if (nd == NULL) {
                break;
            }
            nd->schema = schema;
            nd->type = type;
        }
    }
    SDL_free(cut);
}

VNode *Viewer_FindNode(VPack *vp, const char *id) {
    u32 i;

    for (i = 0; i < vp->numNodes; i++) {
        if (SDL_strcmp(vp->nodes[i].id, id) == 0) {
            return &vp->nodes[i];
        }
    }
    return NULL;
}

static const char *const s_kindNames[] = { "typed", "raw", "geometry", "texture", "shader", "trigger" };

void Viewer_WriteOutline(JsonWriter *w, VPack *vp) {
    u32 i;

    Jw_BeginArray(w, "nodes", 0);
    for (i = 0; i < vp->numNodes; i++) {
        VNode *nd = &vp->nodes[i];
        const char *name = SDL_strrchr(nd->id, '/');

        Jw_BeginObject(w, NULL, 1);
        Jw_String(w, "id", nd->id);
        Jw_String(w, "name", nd->parent >= 0 && name != NULL ? name + 1 : nd->id);
        Jw_String(w, "kind", s_kindNames[nd->kind]);
        if (nd->type != NULL) {
            Jw_String(w, "type", nd->type);
        }
        if (nd->parent >= 0) {
            Jw_String(w, "parent", vp->nodes[nd->parent].id);
        }
        if (nd->off != VIEWER_NO_OFFSET) {
            Jw_Int(w, "offset", nd->off);
            Jw_Int(w, "size", nd->size);
        }
        Jw_EndObject(w);
    }
    Jw_EndArray(w);
}

/* ---- links ---------------------------------------------------------------- */

static VPack *PackOf(u32 handle, u32 *off) {
    int i;

    for (i = 0; i < VIEWER_MAX_PACKS; i++) {
        VPack *vp = &g_viewerPacks[i];

        if (vp->used && vp->pack != NULL && GETP(vp->pack->base) != NULL &&
            handle - (u32)HND(GETP(vp->pack->base)) < vp->size) {
            *off = handle - (u32)HND(GETP(vp->pack->base));
            return vp;
        }
    }
    return NULL;
}

/* An offset of a pack as a link: a node's id, or id/o<hex> inside one, or
 * @<hex> where no node is. */
static void OffsetLink(VPack *from, VPack *vp, u32 off, char *out, size_t size) {
    char prefix[200] = "";
    s32 i = vp->owner != NULL && off < vp->size ? vp->owner[off / 4] : -1;

    if (vp != from) {
        SDL_snprintf(prefix, sizeof(prefix), "base://%s/%s/", vp->archiveName, vp->name);
    }
    if (i >= 0 && vp->nodes[i].off == off) {
        SDL_snprintf(out, size, "%s%s", prefix, vp->nodes[i].id);
    } else if (i >= 0 && off >= vp->nodes[i].off) {
        SDL_snprintf(out, size, "%s%s/o%x", prefix, vp->nodes[i].id, off - vp->nodes[i].off);
    } else {
        SDL_snprintf(out, size, "%s@%x", prefix, off);
    }
}

/* The reference relocation of the slot at `off`, or NULL. */
static const u32 *RefOfSlot(VPack *vp, u32 off) {
    u32 i;

    for (i = 0; i < vp->numRefs; i++) {
        if (vp->refs[i * 2] == off) {
            return &vp->refs[i * 2];
        }
    }
    return NULL;
}

/* The share pack a relocation's id names (its high half), as "archive/share_N". */
static void SharePackName(VPack *vp, u32 packID, char *out, size_t size) {
    int idx = vp->pack->refs[0].id != packID;

    SDL_snprintf(out, size, "%.12s/share_%u", vp->pack->refs[idx].name, packID);
}

/* What the link slot at `off` holds, as text; 0: null. */
static int LinkText(VPack *vp, u32 off, const char *kind, char *out, size_t size) {
    u32 v = RdU32(vp, off), t, i;
    VPack *other;
    DataPack *dp = vp->pack;
    char share[40];

    if (SDL_strcmp(kind, "texture") == 0) {
        for (i = 0; i < dp->numRelocs; i++) {
            if (((u32)GETP(dp->relocs)[i * 2] & ~PORT_TEXRELOC_U16) == off) {
                u32 id = (u32)GETP(dp->relocs)[i * 2 + 1];

                if (id >> 16) {
                    SharePackName(vp, id >> 16, share, sizeof(share));
                    SDL_snprintf(out, size, "base://%s/texture/%u", share, id & 0xFFFF);
                } else {
                    SDL_snprintf(out, size, "texture/%u", id);
                }
                return 1;
            }
        }
        return 0;   /* no relocation: no texture */
    }
    if (v == 0) {
        return 0;
    }
    if ((t = Target(vp, off)) != VIEWER_NO_OFFSET) {
        OffsetLink(vp, vp, t, out, size);
        return 1;
    }
    if (SlotKind(vp, off) == VS_EXPORT) {
        const u32 *ref = RefOfSlot(vp, off);

        if (ref != NULL) {
            SharePackName(vp, ref[1] >> 16, share, sizeof(share));
            SDL_snprintf(out, size, "base://%s/export/%u", share, ref[1] & 0xFFFF);
            return 1;
        }
    }
    if ((other = PackOf(v, &t)) != NULL) {
        OffsetLink(vp, other, t, out, size);
        return 1;
    }
    SDL_snprintf(out, size, "handle:0x%08X", v);
    return 1;
}

/* ---- values --------------------------------------------------------------- */

static void WritePrim(JsonWriter *w, const char *key, const Prim *p, const u8 *at) {
    if (p->isFloat) {
        if (p->size == 4) {
            f32 f;

            SDL_memcpy(&f, at, 4);
            Jw_Float(w, key, f);
        } else {
            f64 d;
            JsonNode n = { JSON_NUMBER };

            SDL_memcpy(&d, at, 8);
            n.number = d;
            Jw_Node(w, key, &n);
        }
    } else {
        long long v = 0;

        switch (p->size) {
        case 1: v = p->isSigned ? (long long)*(const s8 *)at : (long long)*at; break;
        case 2: { u16 x; SDL_memcpy(&x, at, 2); v = p->isSigned ? (long long)(s16)x : (long long)x; break; }
        default: { u32 x; SDL_memcpy(&x, at, 4); v = p->isSigned ? (long long)(s32)x : (long long)x; break; }
        }
        Jw_Int(w, key, v);
    }
}

static void WriteProps(JsonWriter *w, VPack *vp, const JsonNode *root, const JsonNode *props, u32 base);

static void WriteValue(JsonWriter *w, const char *key, VPack *vp, const JsonNode *root, const JsonNode *prop,
                       const CType *t, int dim, u32 off) {
    const JsonNode *link, *def;
    const Prim *p;

    if (dim < t->numDims) {
        u32 elem = BaseSize(root, t->base);
        int d, i;

        for (d = dim + 1; d < t->numDims; d++) {
            elem *= (u32)t->dims[d];
        }
        Jw_BeginArray(w, key, 1);
        for (i = 0; i < t->dims[dim]; i++) {
            WriteValue(w, NULL, vp, root, prop != NULL ? Json_Get(prop, "items") : NULL, t, dim + 1, off + (u32)i * elem);
        }
        Jw_EndArray(w);
        return;
    }
    if (off + BaseSize(root, t->base) > vp->size) {
        Viewer_WriteNull(w, key);
        return;
    }
    if (prop != NULL && (link = Json_Get(prop, "x-link")) != NULL) {
        char text[320];

        if (LinkText(vp, off, Json_GetString(link, "kind", "object"), text, sizeof(text))) {
            Jw_String(w, key, text);
        } else {
            Viewer_WriteNull(w, key);
        }
        return;
    }
    if ((def = RefOf(root, prop)) != NULL) {
        Jw_BeginObject(w, key, 1);
        WriteProps(w, vp, root, Json_Get(def, "properties"), off);
        Jw_EndObject(w);
        return;
    }
    if ((p = FindPrim(t->base)) != NULL) {
        WritePrim(w, key, p, Base(vp) + off);
    } else {
        static const Prim word = { "u32", 4, 0, 0 };

        WritePrim(w, key, &word, Base(vp) + off);
    }
}

static void WriteProps(JsonWriter *w, VPack *vp, const JsonNode *root, const JsonNode *props, u32 base) {
    const JsonNode *p;

    for (p = props != NULL ? props->child : NULL; p != NULL; p = p->next) {
        const char *ctype = Json_GetString(p, "x-c-type", NULL);
        CType t;

        if (ctype == NULL || Json_Get(p, "x-offset") == NULL) {
            continue;
        }
        ParseCType(ctype, &t);
        WriteValue(w, p->key, vp, root, p, &t, 0, base + HexOf(p, "x-offset"));
    }
}

static void WriteSlots(JsonWriter *w, VPack *vp, VNode *nd) {
    static const char *const kinds[] = { "", "object", "geometry", "export", "texture", "sound" };
    u32 o;

    Jw_BeginArray(w, "slots", 0);
    for (o = nd->off; o + 4 <= nd->off + nd->size; o += 4) {
        int k = SlotKind(vp, o);
        char text[320];

        if (k == VS_NONE) {
            continue;
        }
        Jw_BeginObject(w, NULL, 1);
        Jw_Int(w, "offset", o - nd->off);
        Jw_String(w, "kind", k == VS_REF ? "object" : kinds[k]);
        if (k == VS_SOUND) {
            Jw_Int(w, "id", RdU32(vp, o));
        } else if (LinkText(vp, o, k == VS_TEXTURE ? "texture" : "object", text, sizeof(text))) {
            Jw_String(w, "target", text);
        } else {
            Viewer_WriteNull(w, "target");
        }
        Jw_EndObject(w);
    }
    Jw_EndArray(w);
}

static void WriteVec(JsonWriter *w, const char *key, const f32 *v, int n) {
    int i;

    Jw_BeginArray(w, key, 1);
    for (i = 0; i < n; i++) {
        Jw_Float(w, NULL, v[i]);
    }
    Jw_EndArray(w);
}

void Viewer_WriteNode(JsonWriter *w, VPack *vp, VNode *nd) {
    static const char *const kindNames[] = { "typed", "raw", "geometry", "texture", "shader", "trigger" };
    u8 *base = Base(vp);

    Jw_String(w, "id", nd->id);
    Jw_String(w, "kind", kindNames[nd->kind]);
    if (nd->type != NULL) {
        Jw_String(w, "type", nd->type);
    }
    if (nd->off != VIEWER_NO_OFFSET) {
        Jw_Int(w, "offset", nd->off);
        Jw_Int(w, "size", nd->size);
    }
    switch (nd->kind) {
    case VN_TYPED: {
        const JsonNode *p;

        Jw_BeginArray(w, "fields", 0);
        for (p = Json_Get(nd->schema, "properties")->child; p != NULL; p = p->next) {
            const char *ctype = Json_GetString(p, "x-c-type", NULL);
            const JsonNode *link = Json_Get(p, "x-link");
            CType t;

            if (ctype == NULL || Json_Get(p, "x-offset") == NULL) {
                continue;
            }
            ParseCType(ctype, &t);
            Jw_BeginObject(w, NULL, 1);
            Jw_String(w, "name", p->key);
            Jw_String(w, "ctype", ctype);
            Jw_Int(w, "offset", HexOf(p, "x-offset"));
            if (Json_GetString(p, "x-units", NULL) != NULL) {
                Jw_String(w, "units", Json_GetString(p, "x-units", ""));
            }
            if (link != NULL) {
                Jw_String(w, "link", Json_GetString(link, "kind", "object"));
                if (Json_GetString(link, "target", NULL) != NULL) {
                    Jw_String(w, "target", Json_GetString(link, "target", ""));
                }
            }
            WriteValue(w, "value", vp, nd->schema, p, &t, 0, nd->off + HexOf(p, "x-offset"));
            Jw_EndObject(w);
        }
        Jw_EndArray(w);
        break;
    }
    case VN_GEOMETRY: {
        Geometry *g = (Geometry *)(base + nd->off);
        u32 lodsOff = Target(vp, nd->off + 8), i;

        Jw_Int(w, "levels", g->numGroups);
        Jw_Bool(w, "skinned", g->skipDraw != 0);
        WriteVec(w, "bboxMin", g->bboxMin, 3);
        WriteVec(w, "bboxMax", g->bboxMax, 3);
        WriteVec(w, "center", g->center, 3);
        Jw_Float(w, "radius", g->bboxMin[3]);
        Jw_BeginArray(w, "lods", 0);
        /* a level is 0x18 bytes: its layout mark, group count and groups */
        for (i = 0; lodsOff != VIEWER_NO_OFFSET && i < g->numGroups && lodsOff + (i + 1) * 0x18 <= vp->size; i++) {
            Geometry *lod = (Geometry *)(base + lodsOff + i * 0x18);
            u32 groupsOff = Target(vp, lodsOff + i * 0x18 + 8), j, tris = 0;
            int indexed = lod->portLayout == PORT_LAYOUT_INDEXED || lod->portLayout == PORT_LAYOUT_INDEXED_NOCULL;

            Jw_BeginObject(w, NULL, 1);
            Jw_Int(w, "groups", lod->numGroups);
            for (j = 0; indexed && groupsOff != VIEWER_NO_OFFSET && j < lod->numGroups &&
                        groupsOff + (j + 1) * sizeof(GeometryGroup) <= vp->size; j++) {
                tris += ((GeometryGroup *)(base + groupsOff + j * sizeof(GeometryGroup)))->dlSize;
            }
            if (indexed) {
                Jw_Int(w, "triangles", tris);
            }
            Jw_EndObject(w);
        }
        Jw_EndArray(w);
        break;
    }
    case VN_TEXTURE:
        Jw_Int(w, "index", nd->index);
        Jw_Int(w, "textureId", GETP(vp->pack->textureIDs)[nd->index]);
        break;
    default:
        break;
    }
    if (nd->kind != VN_TYPED && nd->off != VIEWER_NO_OFFSET) {
        u32 n = nd->size > 256 ? 256 : nd->size;

        Jw_Hex(w, "bytes", base + nd->off, n);  /* the first 256; pointer slots hold this run's handles */
        WriteSlots(w, vp, nd);
    }
}

/* ---- set ------------------------------------------------------------------ */

static int Fail(char *err, size_t errSize, const char *fmt, ...) PORT_PRINTF(3, 4);

static int Fail(char *err, size_t errSize, const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    SDL_vsnprintf(err, errSize, fmt, ap);
    va_end(ap);
    return 0;
}

static int StorePrim(const Prim *p, u8 *at, const JsonNode *v, char *err, size_t errSize) {
    if (v == NULL || v->type != JSON_NUMBER) {
        /* a float that isn't a number is its bits, as get writes it ("0x7FC00000") */
        if (v != NULL && v->type == JSON_STRING && p->isFloat && p->size == 4 && SDL_strncmp(v->string, "0x", 2) == 0) {
            u32 bits = (u32)SDL_strtoul(v->string, NULL, 16);

            SDL_memcpy(at, &bits, 4);
            return 1;
        }
        return Fail(err, errSize, "a %s takes a number", p->name);
    }
    if (p->isFloat) {
        if (p->size == 4) {
            f32 f = (f32)v->number;

            SDL_memcpy(at, &f, 4);
        } else {
            SDL_memcpy(at, &v->number, 8);
        }
        return 1;
    }
    {
        long long x = v->isInteger ? v->integer : (long long)v->number;
        long long lo = p->isSigned ? -(1ll << (p->size * 8 - 1)) : 0;
        long long hi = p->isSigned ? (1ll << (p->size * 8 - 1)) - 1 : (1ll << (p->size * 8)) - 1;

        if (!v->isInteger && v->number != (double)x) {
            return Fail(err, errSize, "a %s takes a whole number", p->name);
        }
        if (x < lo || x > hi) {
            return Fail(err, errSize, "%lld is outside a %s (%lld .. %lld)", x, p->name, lo, hi);
        }
        switch (p->size) {
        case 1: *at = (u8)x; break;
        case 2: { u16 y = (u16)x; SDL_memcpy(at, &y, 2); break; }
        default: { u32 y = (u32)x; SDL_memcpy(at, &y, 4); break; }
        }
    }
    return 1;
}

static int StoreLink(VPack *vp, u32 off, const char *kind, const JsonNode *v, char *err, size_t errSize) {
    char prefix[200];
    const char *id;
    VNode *nd;
    u32 word = 0;

    if (v == NULL || (v->type != JSON_NULL && v->type != JSON_STRING)) {
        return Fail(err, errSize, "a link takes an id or null");
    }
    if (SDL_strcmp(kind, "texture") == 0) {
        DataPack *dp = vp->pack;
        u32 i;

        /* the slot's relocation has to follow, or the next refresh puts the old texture back */
        for (i = 0; i < dp->numRelocs; i++) {
            if (((u32)GETP(dp->relocs)[i * 2] & ~PORT_TEXRELOC_U16) == off) {
                break;
            }
        }
        if (i == dp->numRelocs) {
            return Fail(err, errSize, "this slot has no texture relocation in the pack: it can't take a texture here");
        }
        if (v->type == JSON_NULL) {
            return Fail(err, errSize, "a texture slot with a relocation can't be null here");
        }
    }
    if (v->type == JSON_NULL) {
        SDL_memcpy(Base(vp) + off, &word, 4);
        return 1;
    }
    id = v->string;
    SDL_snprintf(prefix, sizeof(prefix), "base://%s/%s/", vp->archiveName, vp->name);
    if (SDL_strncmp(id, prefix, SDL_strlen(prefix)) == 0) {
        id += SDL_strlen(prefix);
    }
    if (SDL_strncmp(id, "base://", 7) == 0 || SDL_strncmp(id, "uid://", 6) == 0) {
        return Fail(err, errSize, "links to other packs can't be set in a loaded pack yet");
    }
    if (id[0] == '@') {
        word = (u32)SDL_strtoul(id + 1, NULL, 16);
        if (word >= vp->size || SDL_strcmp(kind, "object") != 0) {
            return Fail(err, errSize, "%s is outside the pack", id);
        }
        word += (u32)HND(Base(vp));
        SDL_memcpy(Base(vp) + off, &word, 4);
        return 1;
    }
    nd = Viewer_FindNode(vp, id);
    if (nd == NULL) {
        return Fail(err, errSize, "no node %s in %s/%s", id, vp->archiveName, vp->name);
    }
    if (SDL_strcmp(kind, "texture") == 0) {
        DataPack *dp = vp->pack;
        u32 i;

        if (nd->kind != VN_TEXTURE) {
            return Fail(err, errSize, "%s is not a texture", id);
        }
        for (i = 0; i < dp->numRelocs; i++) {
            if (((u32)GETP(dp->relocs)[i * 2] & ~PORT_TEXRELOC_U16) == off) {
                GETP(dp->relocs)[i * 2 + 1] = (s32)nd->index;
                PortSwap_StoreTextureID(Base(vp), (u32)GETP(dp->relocs)[i * 2], GETP(dp->textureIDs)[nd->index]);
            }
        }
        return 1;
    }
    if (SDL_strcmp(kind, "geometry") == 0 && nd->kind != VN_GEOMETRY) {
        return Fail(err, errSize, "%s is not a geometry", id);
    }
    if (nd->off == VIEWER_NO_OFFSET) {
        return Fail(err, errSize, "%s can't be linked to from an object field", id);
    }
    word = (u32)HND(Base(vp) + nd->off);
    SDL_memcpy(Base(vp) + off, &word, 4);
    return 1;
}

static int StoreValue(VPack *vp, const JsonNode *root, const JsonNode *prop, const CType *t, int dim, u32 off,
                      const JsonNode *v, char *err, size_t errSize) {
    const JsonNode *link, *def;
    const Prim *p;

    if (dim < t->numDims) {
        u32 elem = BaseSize(root, t->base);
        const JsonNode *e;
        int d, i;

        for (d = dim + 1; d < t->numDims; d++) {
            elem *= (u32)t->dims[d];
        }
        if (v == NULL || v->type != JSON_ARRAY || v->count != t->dims[dim]) {
            return Fail(err, errSize, "an array of %d is needed", t->dims[dim]);
        }
        for (i = 0, e = v->child; e != NULL; e = e->next, i++) {
            if (!StoreValue(vp, root, prop != NULL ? Json_Get(prop, "items") : NULL, t, dim + 1, off + (u32)i * elem, e, err,
                            errSize)) {
                return 0;
            }
        }
        return 1;
    }
    if (off + BaseSize(root, t->base) > vp->size) {
        return Fail(err, errSize, "outside the pack");
    }
    if (prop != NULL && (link = Json_Get(prop, "x-link")) != NULL) {
        return StoreLink(vp, off, Json_GetString(link, "kind", "object"), v, err, errSize);
    }
    if ((def = RefOf(root, prop)) != NULL) {
        const JsonNode *m;

        if (v == NULL || v->type != JSON_OBJECT) {
            return Fail(err, errSize, "an object is needed");
        }
        for (m = v->child; m != NULL; m = m->next) {
            const JsonNode *q = Json_Get(Json_Get(def, "properties"), m->key);
            const char *ctype = q != NULL ? Json_GetString(q, "x-c-type", NULL) : NULL;
            CType qt;

            if (ctype == NULL) {
                return Fail(err, errSize, "no field %s", m->key);
            }
            ParseCType(ctype, &qt);
            if (!StoreValue(vp, root, q, &qt, 0, off + HexOf(q, "x-offset"), m, err, errSize)) {
                return 0;
            }
        }
        return 1;
    }
    if ((p = FindPrim(t->base)) == NULL) {
        p = FindPrim("u32");
    }
    if (IsPointerSlot(vp, off & ~3u) || SlotKind(vp, off & ~3u) == VS_EXPORT) {
        return Fail(err, errSize, "this is a pointer slot: set it through a link field");
    }
    return StorePrim(p, Base(vp) + off, v, err, errSize);
}

int Viewer_SetField(JsonWriter *w, VPack *vp, VNode *nd, const char *field, const JsonNode *value, char *err,
                    size_t errSize) {
    const JsonNode *root = nd->schema, *props, *prop = NULL;
    char path[200], *seg, *next;
    CType t;
    int dim = 0;
    u32 off = nd->off;

    if (nd->off == VIEWER_NO_OFFSET) {
        return Fail(err, errSize, "%s has no fields to set", nd->id);
    }
    if (field[0] == '@') {
        /* @<hex offset>:<type>: a typed store anywhere in the node */
        const char *colon = SDL_strchr(field, ':');
        u32 rel = (u32)SDL_strtoul(field + 1, NULL, 16);

        SDL_strlcpy(t.base, colon != NULL ? colon + 1 : "u32", sizeof(t.base));
        t.numDims = 0;
        if (FindPrim(t.base) == NULL) {
            return Fail(err, errSize, "%s is not a scalar type", t.base);
        }
        if (rel + BaseSize(NULL, t.base) > nd->size) {
            return Fail(err, errSize, "offset 0x%x is outside %s (0x%x bytes)", rel, nd->id, nd->size);
        }
        off += rel;
        if (!StoreValue(vp, NULL, NULL, &t, 0, off, value, err, errSize)) {
            return 0;
        }
        WriteValue(w, "value", vp, NULL, NULL, &t, 0, off);
        return 1;
    }
    if (root == NULL) {
        return Fail(err, errSize, "%s has no schema: use @<hex offset>:<type>", nd->id);
    }
    props = Json_Get(root, "properties");
    SDL_strlcpy(path, field, sizeof(path));
    SDL_memset(&t, 0, sizeof(t));
    for (seg = path; seg != NULL && *seg; seg = next) {
        next = SDL_strchr(seg, '/');
        if (next != NULL) {
            *next++ = '\0';
        }
        if (prop != NULL && dim < t.numDims) {      /* an element */
            u32 elem = BaseSize(root, t.base);
            int d, i = SDL_atoi(seg);

            for (d = dim + 1; d < t.numDims; d++) {
                elem *= (u32)t.dims[d];
            }
            if (!SDL_isdigit((unsigned char)seg[0]) || i >= t.dims[dim]) {
                return Fail(err, errSize, "%s: index 0 .. %d", field, t.dims[dim] - 1);
            }
            off += (u32)i * elem;
            prop = Json_Get(prop, "items");
            dim++;
            continue;
        }
        if (prop != NULL) {                         /* a member of an embedded struct */
            const JsonNode *def = RefOf(root, prop);

            if (def == NULL) {
                return Fail(err, errSize, "%s: nothing below that field", field);
            }
            props = Json_Get(def, "properties");
        }
        prop = Json_Get(props, seg);
        if (prop == NULL || Json_GetString(prop, "x-c-type", NULL) == NULL || Json_Get(prop, "x-offset") == NULL) {
            return Fail(err, errSize, "no field %s in %s", field, nd->type != NULL ? nd->type : nd->id);
        }
        ParseCType(Json_GetString(prop, "x-c-type", ""), &t);
        off += HexOf(prop, "x-offset");
        dim = 0;
    }
    if (prop == NULL) {
        return Fail(err, errSize, "which field?");
    }
    if (!StoreValue(vp, root, prop, &t, dim, off, value, err, errSize)) {
        return 0;
    }
    WriteValue(w, "value", vp, root, prop, &t, dim, off);
    return 1;
}

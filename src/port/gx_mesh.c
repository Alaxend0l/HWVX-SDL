/* Indexed meshes: geometry drawn straight from vertex arrays in host order,
 * not through a display list (gx_fifo.c). Port-only API, gx.h.
 *
 * The PC release's geometry is drawn this way (Render_Geometry_.c): u16
 * triangle lists per group into one vertex array per LOD, as its OpenGL
 * build fed glDrawElements. Each vertex is built the way the display-list
 * decoder builds one (DecodeVertex): white unless a colour is given, the
 * current position matrix unless the mesh is skinned, normals at the
 * hardware's fixed 1/64 scale. Everything else (texture, TEV, lighting,
 * blending) is the GX state the caller set, as for a display list. */
#include "gx_internal.h"

/* Scratch vertices for one draw; GX_Draw copies or uploads them. */
static GXVertex *s_verts;
static u32 s_cap;

static GXVertex *Scratch(u32 n) {
    if (n > s_cap) {
        GXVertex *grown = SDL_realloc(s_verts, (n + 1024) * sizeof(GXVertex));

        if (grown == NULL) {
            return NULL;    /* the old buffer stays; this draw is skipped */
        }
        s_verts = grown;
        s_cap = n + 1024;
    }
    return s_verts;
}

/* Matrix slot of vertex `i` in a skinned mesh: its bone, by the per-bone
 * vertex counts (vertices are sorted by bone). Bone k's matrix is loaded
 * into position matrix 3k (Gfx_Anim.c matrixlookup), i.e. slot k. */
static f32 BoneSlot(const GXPortMesh *m, u32 i) {
    u32 k, first = 0;

    for (k = 0; k < m->numBones; k++) {
        first += m->boneCounts[k];
        if (i < first) {
            return (f32)k;
        }
    }
    return (f32)(m->numBones ? m->numBones - 1 : 0);
}

static void BuildVertex(const GXPortMesh *m, u32 i, u32 use, f32 curSlot, GXVertex *v) {
    memset(v, 0, sizeof(*v));
    v->mtxIdx = (use & GX_PORT_SKIN) && m->boneCounts != NULL ? BoneSlot(m, i) : curSlot;
    {
        const f32 *p = (const f32 *)(m->pos + i * m->posStride);
        v->pos[0] = p[0];
        v->pos[1] = p[1];
        v->pos[2] = p[2];
    }
    if ((use & GX_PORT_USE_NRM) && m->nrm != NULL) {
        const f32 *n = (const f32 *)(m->nrm + i * m->nrmStride);
        int k;
        for (k = 0; k < 3; k++) {
            f32 c = n[k] * 64.0f;
            int q = (int)(c >= 0 ? c + 0.5f : c - 0.5f);
            v->nrm[k] = (s8)(q > 127 ? 127 : q < -127 ? -127 : q);
        }
    }
    if ((use & GX_PORT_USE_CLR) && m->clr != NULL) {
        const u8 *c = m->clr + i * m->clrStride;
        v->clr[0][0] = c[0];
        v->clr[0][1] = c[1];
        v->clr[0][2] = c[2];
        v->clr[0][3] = (use & GX_PORT_CLR_ALPHA) ? c[3] : 255;
    } else {
        v->clr[0][0] = v->clr[0][1] = v->clr[0][2] = v->clr[0][3] = 255;
    }
    if ((use & GX_PORT_USE_UV) && m->uv != NULL) {
        const f32 *t = (const f32 *)(m->uv + i * m->uvStride);
        v->tex[0][0] = t[0];
        v->tex[0][1] = t[1];
    }
}

void GX_PortDrawIndexed(const GXPortMesh *m, int prim, const u16 *idx, u32 count, u32 use) {
    f32 curSlot = (f32)(g_gx.curMtx / 3 < GX_NUM_POSMTX ? g_gx.curMtx / 3 : GX_POSMTX_IDENTITY);
    GXVertex *v;
    u32 i;

    if (m == NULL || m->pos == NULL || idx == NULL || count == 0) {
        return;
    }
    v = Scratch(count);
    if (v == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        BuildVertex(m, idx[i], use, curSlot, &v[i]);
    }
    if (use & GX_PORT_NOCULL) {
        u8 cull = g_gx.cullMode;

        g_gx.cullMode = 0;      /* GX_CULL_NONE */
        GX_Draw(prim, v, count);
        g_gx.cullMode = cull;
        return;
    }
    GX_Draw(prim, v, count);
}

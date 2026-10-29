// Normals for the map's display lists, so GX hardware lights reach the baked-colour
// terrain. Each rigid POBJ's display list is rebuilt with a direct S8 normal after every
// position: the area-weighted normal of the triangles sharing that position within a
// crease angle of the vertex's own. Strips are split where they fold past the crease, so
// each side of a building corner keeps its own normal.

#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "gx.h"
#include "inline.h"

#include "custom_weather.h"

#define NRM_CREASE_COS 0.7071f  // 45 degrees
#define NRM_ONE        64.0f    // S8 normals carry 6 fraction bits
#define NRM_NONE       0xFFFF
#define NRM_MAX_SCALE  1.95f    // S8 normals stop short of 2

typedef struct NrmLayout
{
    HSD_VtxDescList *verts;
    HSD_VtxDescList *pos;
    int nattrs;    // entries before GX_VA_NULL
    int pos_at;    // POS's entry
    int vtx_size;  // bytes per vertex in the display list
    int pos_end;   // offset just past POS within a vertex
    float pos_scale;
} NrmLayout;

typedef struct NrmTri
{
    u16 pos[3];
    Vec3 n;     // outward, area-weighted
    float inv;  // 1 / |n|, 0 for a degenerate triangle
} NrmTri;

typedef enum NrmLayoutKind
{
    NRM_UNSUPPORTED,
    NRM_HAS_NORMALS,
    NRM_CONVERT,
} NrmLayoutKind;

typedef struct NrmStats
{
    int converted;
    int had;
    int unsupported;
    int max_tris;
    int max_pos;
    u32 bytes;
} NrmStats;

static NrmTri *stc_tris;
static u16    *stc_next;  // per triangle corner, the next corner at the same position
static u16    *stc_head;  // per position index, its first triangle corner

static int AttrBytes(const HSD_VtxDescList *a)
{
    static const u8 comp_bytes[] = {1, 1, 2, 2, 4};
    static const u8 clr_bytes[] = {2, 3, 4, 2, 3, 4};

    if (a->attr_type == GX_NONE)
        return 0;
    if (a->attr_type == GX_INDEX8)
        return 1;
    if (a->attr_type == GX_INDEX16)
        return 2;
    if (a->attr <= GX_VA_TEX7MTXIDX)
        return 1;
    if (a->attr == GX_VA_CLR0 || a->attr == GX_VA_CLR1)
        return (a->comp_type <= GX_RGBA8) ? clr_bytes[a->comp_type] : -1;
    if (a->comp_type > GX_F32)
        return -1;
    int c = comp_bytes[a->comp_type];
    if (a->attr == GX_VA_POS)
        return c * ((a->comp_cnt == GX_POS_XYZ) ? 3 : 2);
    if (a->attr >= GX_VA_TEX0 && a->attr <= GX_VA_TEX7)
        return c * ((a->comp_cnt == GX_TEX_ST) ? 2 : 1);
    return -1;
}

static NrmLayoutKind Layout(POBJ *p, NrmLayout *l)
{
    if ((p->flags & POBJ_TYPE_MASK) != 0 || p->u.jobj != NULL || !p->verts || !p->display)
        return NRM_UNSUPPORTED;

    *l = (NrmLayout){.verts = p->verts, .pos_at = -1};
    for (HSD_VtxDescList *a = p->verts; a->attr != GX_VA_NULL; a++, l->nattrs++)
    {
        if (a->attr == GX_VA_NRM || a->attr == GX_VA_NBT)
            return NRM_HAS_NORMALS;
        int b = AttrBytes(a);
        if (b < 0)
            return NRM_UNSUPPORTED;
        l->vtx_size += b;
        if (a->attr == GX_VA_POS)
        {
            l->pos = a;
            l->pos_at = l->nattrs;
            l->pos_end = l->vtx_size;
        }
    }
    HSD_VtxDescList *pos = l->pos;
    if (!pos || pos->comp_cnt != GX_POS_XYZ || pos->comp_type > GX_F32
        || (pos->attr_type != GX_INDEX8 && pos->attr_type != GX_INDEX16))
        return NRM_UNSUPPORTED;
    l->pos_scale = 1.0f / (float)(1 << pos->frac);
    return NRM_CONVERT;
}

static int PosIndex(const NrmLayout *l, const u8 *v)
{
    const u8 *i = v + l->pos_end - ((l->pos->attr_type == GX_INDEX16) ? 2 : 1);
    return (l->pos->attr_type == GX_INDEX16) ? (i[0] << 8) | i[1] : i[0];
}

static float Comp(const u8 *c, GXComponentType type, float scale)
{
    switch (type)
    {
    case GX_U8:
        return *c * scale;
    case GX_S8:
        return *(const s8 *)c * scale;
    case GX_U16:
        return *(const u16 *)c * scale;
    case GX_S16:
        return *(const s16 *)c * scale;
    default:
        return *(const float *)c;
    }
}

static void PosAt(const NrmLayout *l, int idx, Vec3 *out)
{
    static const u8 comp_bytes[] = {1, 1, 2, 2, 4};
    int c = comp_bytes[l->pos->comp_type];
    const u8 *p = (const u8 *)l->pos->vertex + idx * l->pos->stride;
    out->X = Comp(p, l->pos->comp_type, l->pos_scale);
    out->Y = Comp(p + c, l->pos->comp_type, l->pos_scale);
    out->Z = Comp(p + 2 * c, l->pos->comp_type, l->pos_scale);
}

// The next primitive at *cur, skipping NOP padding: its opcode, vertex count and first
// vertex. 0 at the end of the list, -1 on a primitive this pass does not handle.
static int NextPrim(u8 **cur, u8 *end, int vtx_size, u8 *op, int *n, u8 **v)
{
    u8 *c = *cur;
    while (c < end && *c == 0)
        c++;
    if (c + 3 > end)
        return 0;
    int kind = *c & 0xF8;
    if (kind != GX_QUADS && kind != GX_TRIANGLES && kind != GX_TRIANGLESTRIP
        && kind != GX_TRIANGLEFAN)
        return -1;
    *op = *c;
    *n = (c[1] << 8) | c[2];
    *v = c + 3;
    if (*v + *n * vtx_size > end)
        return -1;
    *cur = *v + *n * vtx_size;
    return 1;
}

static int PrimTris(int kind, int n)
{
    if (kind == GX_QUADS)
        return n / 4 * 2;
    if (kind == GX_TRIANGLES)
        return n / 3;
    return (n >= 3) ? n - 2 : 0;
}

// The corners of triangle t of a primitive, wound as GX draws it.
static void TriCorners(int kind, int t, int c[3])
{
    switch (kind)
    {
    case GX_QUADS:
        c[0] = (t >> 1) * 4;
        c[1] = c[0] + ((t & 1) ? 2 : 1);
        c[2] = c[0] + ((t & 1) ? 3 : 2);
        break;
    case GX_TRIANGLES:
        c[0] = t * 3;
        c[1] = t * 3 + 1;
        c[2] = t * 3 + 2;
        break;
    case GX_TRIANGLESTRIP:
        c[0] = (t & 1) ? t + 1 : t;
        c[1] = (t & 1) ? t : t + 1;
        c[2] = t + 2;
        break;
    default:
        c[0] = 0;
        c[1] = t + 1;
        c[2] = t + 2;
        break;
    }
}

// Triangles, tris and the max position index of a POBJ's display list; 0 if it has a
// primitive this pass does not handle.
static int Measure(POBJ *p, const NrmLayout *l, int *tris, int *max_pos)
{
    u8 *cur = p->display;
    u8 *end = cur + p->n_display * 32;
    u8 op, *v;
    int n, r;
    *tris = 0;
    *max_pos = 0;
    while ((r = NextPrim(&cur, end, l->vtx_size, &op, &n, &v)) > 0)
    {
        *tris += PrimTris(op & 0xF8, n);
        for (int i = 0; i < n; i++)
        {
            int idx = PosIndex(l, v + i * l->vtx_size);
            if (idx > *max_pos)
                *max_pos = idx;
        }
    }
    return r == 0 && *tris > 0;
}

static int Decode(POBJ *p, const NrmLayout *l)
{
    u8 *cur = p->display;
    u8 *end = cur + p->n_display * 32;
    u8 op, *v;
    int n;
    int tri = 0;
    while (NextPrim(&cur, end, l->vtx_size, &op, &n, &v) > 0)
    {
        int kind = op & 0xF8;
        for (int t = 0; t < PrimTris(kind, n); t++, tri++)
        {
            int c[3];
            Vec3 pt[3];
            NrmTri *nt = &stc_tris[tri];
            TriCorners(kind, t, c);
            for (int k = 0; k < 3; k++)
            {
                int idx = PosIndex(l, v + c[k] * l->vtx_size);
                nt->pos[k] = idx;
                PosAt(l, idx, &pt[k]);
                stc_next[tri * 3 + k] = stc_head[idx];
                stc_head[idx] = tri * 3 + k;
            }
            float e1x = pt[1].X - pt[0].X, e1y = pt[1].Y - pt[0].Y, e1z = pt[1].Z - pt[0].Z;
            float e2x = pt[2].X - pt[0].X, e2y = pt[2].Y - pt[0].Y, e2z = pt[2].Z - pt[0].Z;
            // GX's front faces wind clockwise, so outward is e2 x e1.
            nt->n.X = e2y * e1z - e2z * e1y;
            nt->n.Y = e2z * e1x - e2x * e1z;
            nt->n.Z = e2x * e1y - e2y * e1x;
            float len = sqrtf(nt->n.X * nt->n.X + nt->n.Y * nt->n.Y + nt->n.Z * nt->n.Z);
            nt->inv = (len > 1.0e-6f) ? 1.0f / len : 0.0f;
        }
    }
    return tri;
}

static float TriCos(const NrmTri *a, const NrmTri *b)
{
    return (a->n.X * b->n.X + a->n.Y * b->n.Y + a->n.Z * b->n.Z) * a->inv * b->inv;
}

static void AddUnit(Vec3 *sum, const NrmTri *t)
{
    sum->X += t->n.X * t->inv;
    sum->Y += t->n.Y * t->inv;
    sum->Z += t->n.Z * t->inv;
}

// The smoothed normal at a position: every triangle there within the crease of `own`.
static void Smooth(int pos, const Vec3 *own, float scale, s8 out[3])
{
    float own_len = sqrtf(own->X * own->X + own->Y * own->Y + own->Z * own->Z);
    Vec3 sum = {0.0f, 0.0f, 0.0f};
    if (own_len > 1.0e-6f)
    {
        for (int c = stc_head[pos]; c != NRM_NONE; c = stc_next[c])
        {
            const NrmTri *t = &stc_tris[c / 3];
            float d = t->n.X * own->X + t->n.Y * own->Y + t->n.Z * own->Z;
            if (t->inv > 0.0f && d * t->inv > NRM_CREASE_COS * own_len)
            {
                sum.X += t->n.X;
                sum.Y += t->n.Y;
                sum.Z += t->n.Z;
            }
        }
    }
    float len = sqrtf(sum.X * sum.X + sum.Y * sum.Y + sum.Z * sum.Z);
    if (len < 1.0e-6f)
    {
        sum = (own_len > 1.0e-6f) ? *own : (Vec3){0.0f, 1.0f, 0.0f};
        len = (own_len > 1.0e-6f) ? own_len : 1.0f;
    }
    float k = NRM_ONE * scale / len;
    float f[3] = {sum.X * k, sum.Y * k, sum.Z * k};
    for (int i = 0; i < 3; i++)
    {
        float r = f[i] + ((f[i] < 0.0f) ? -0.5f : 0.5f);
        out[i] = (r > 127.0f) ? 127 : (r < -128.0f) ? -128 : (s8)r;
    }
}

typedef struct NrmEmit
{
    const NrmLayout *l;
    float scale;
    u8 *out;    // NULL while sizing
    u32 bytes;
} NrmEmit;

static void EmitVertex(NrmEmit *e, const u8 *v, const Vec3 *own)
{
    const NrmLayout *l = e->l;
    e->bytes += l->vtx_size + 3;
    if (!e->out)
        return;
    memcpy(e->out, v, l->pos_end);
    e->out += l->pos_end;
    Smooth(PosIndex(l, v), own, e->scale, (s8 *)e->out);
    e->out += 3;
    memcpy(e->out, v + l->pos_end, l->vtx_size - l->pos_end);
    e->out += l->vtx_size - l->pos_end;
}

static void EmitHeader(NrmEmit *e, u8 op, int n)
{
    e->bytes += 3;
    if (!e->out)
        return;
    e->out[0] = op;
    e->out[1] = n >> 8;
    e->out[2] = n & 0xFF;
    e->out += 3;
}

// A strip's triangles a..b as a strip of their own. One starting on an odd triangle
// leads with a repeated vertex, so its triangles keep the winding they had.
static void EmitStripSegment(NrmEmit *e, u8 op, const u8 *v, const NrmTri *tris, int a, int b)
{
    int n = b - a + 3;
    EmitHeader(e, op, n + (a & 1));
    for (int j = a - (a & 1); j < a + n; j++)
    {
        int vert = (j < a) ? a : j;
        Vec3 own = {0.0f, 0.0f, 0.0f};
        for (int t = vert - 2; t <= vert; t++)
        {
            if (t >= a && t <= b && tris[t].inv > 0.0f)
                AddUnit(&own, &tris[t]);
        }
        EmitVertex(e, v + vert * e->l->vtx_size, &own);
    }
}

static void EmitStrip(NrmEmit *e, u8 op, const u8 *v, int n, const NrmTri *tris)
{
    int ntri = n - 2;
    int start = 0;
    int last = -1;
    for (int t = 0; t < ntri; t++)
    {
        if (tris[t].inv == 0.0f)
            continue;
        if (last >= 0 && TriCos(&tris[t], &tris[last]) < NRM_CREASE_COS)
        {
            EmitStripSegment(e, op, v, tris, start, t - 1);
            start = t;
        }
        last = t;
    }
    EmitStripSegment(e, op, v, tris, start, ntri - 1);
}

static void AddTri(Vec3 *own, const NrmTri *tris, int t, int ntri)
{
    if (t >= 0 && t < ntri && tris[t].inv > 0.0f)
        AddUnit(own, &tris[t]);
}

// Quads, triangles and fans keep their vertices; each takes the triangles it is a
// corner of.
static void EmitPrim(NrmEmit *e, u8 op, const u8 *v, int n, const NrmTri *tris)
{
    int kind = op & 0xF8;
    int ntri = PrimTris(kind, n);
    EmitHeader(e, op, n);
    for (int i = 0; i < n; i++)
    {
        Vec3 own = {0.0f, 0.0f, 0.0f};
        if (kind == GX_TRIANGLES)
            AddTri(&own, tris, i / 3, ntri);
        else if (kind == GX_QUADS)
        {
            if ((i & 3) != 3)
                AddTri(&own, tris, (i >> 2) * 2, ntri);
            if ((i & 3) != 1)
                AddTri(&own, tris, (i >> 2) * 2 + 1, ntri);
        }
        else if (i == 0)
        {
            for (int t = 0; t < ntri; t++)
                AddTri(&own, tris, t, ntri);
        }
        else
        {
            AddTri(&own, tris, i - 2, ntri);
            AddTri(&own, tris, i - 1, ntri);
        }
        EmitVertex(e, v + i * e->l->vtx_size, &own);
    }
}

static void EmitList(POBJ *p, NrmEmit *e)
{
    u8 *cur = p->display;
    u8 *end = cur + p->n_display * 32;
    u8 op, *v;
    int n;
    const NrmTri *tris = stc_tris;
    while (NextPrim(&cur, end, e->l->vtx_size, &op, &n, &v) > 0)
    {
        if ((op & 0xF8) == GX_TRIANGLESTRIP && n >= 3)
            EmitStrip(e, op, v, n, tris);
        else
            EmitPrim(e, op, v, n, tris);
        tris += PrimTris(op & 0xF8, n);
    }
}

static int Convert(POBJ *p, const NrmLayout *l, float scale, NrmStats *s)
{
    int ntri = Decode(p, l);

    NrmEmit e = {.l = l, .scale = scale};
    EmitList(p, &e);
    u32 size = (e.bytes + 31) & ~31;
    u8 *dl = HSD_MemAlloc(size);
    HSD_VtxDescList *verts = HSD_MemAlloc((l->nattrs + 2) * sizeof(HSD_VtxDescList));
    int ok = dl && verts;
    if (ok)
    {
        e.out = dl;
        e.bytes = 0;
        EmitList(p, &e);
        memset(e.out, 0, size - e.bytes);
        DCFlushRange(dl, size);

        int k = 0;
        for (int i = 0; i < l->nattrs; i++)
        {
            verts[k++] = l->verts[i];
            if (i == l->pos_at)
                verts[k++] = (HSD_VtxDescList){
                    .attr = GX_VA_NRM,
                    .attr_type = GX_DIRECT,
                    .comp_cnt = GX_NRM_XYZ,
                    .comp_type = GX_S8,
                    .frac = 6,
                    .stride = 3,
                };
        }
        verts[k] = (HSD_VtxDescList){.attr = GX_VA_NULL};

        p->display = dl;
        p->n_display = size / 32;
        p->verts = verts;
        s->converted++;
        s->bytes += size + (l->nattrs + 2) * sizeof(HSD_VtxDescList);
    }
    else
    {
        if (dl)
            HSD_Free(dl);
        if (verts)
            HSD_Free(verts);
    }

    for (int t = 0; t < ntri; t++)
    {
        for (int k = 0; k < 3; k++)
            stc_head[stc_tris[t].pos[k]] = NRM_NONE;
    }
    return ok;
}

typedef struct NrmWalk
{
    JOBJ *skip;
    int sizing;
    int failed;
    NrmStats *s;
} NrmWalk;

// Normals scale by the inverse of the joint's world scale on their way to eye space, so
// they are stored pre-scaled by it.
static float WorldScale(JOBJ *j)
{
    float s = 1.0f;
    for (; j != NULL; j = j->parent)
        s *= j->scale.X;
    if (s < 0.0f)
        s = -s;
    if (s > NRM_MAX_SCALE)
        return NRM_MAX_SCALE;
    return (s < 0.25f) ? 0.25f : s;
}

static void WalkJoint(JOBJ *j, void *arg)
{
    NrmWalk *w = arg;
    if ((j->flags & (JOBJ_SPLINE | JOBJ_PTCL)) || j == w->skip || w->failed)
        return;

    int lit = 0;
    for (DOBJ *d = j->dobj; d != NULL; d = d->next)
    {
        for (POBJ *p = d->pobj; p != NULL; p = p->next)
        {
            NrmLayout l;
            NrmLayoutKind kind = Layout(p, &l);
            int tris, max_pos;
            if (kind == NRM_HAS_NORMALS)
            {
                w->s->had += w->sizing;
                lit = 1;
                continue;
            }
            if (kind != NRM_CONVERT || !Measure(p, &l, &tris, &max_pos))
            {
                w->s->unsupported += w->sizing;
                continue;
            }
            if (w->sizing)
            {
                if (tris > w->s->max_tris)
                    w->s->max_tris = tris;
                if (max_pos > w->s->max_pos)
                    w->s->max_pos = max_pos;
                continue;
            }
            if (!Convert(p, &l, WorldScale(j), w->s))
            {
                w->failed = 1;
                return;
            }
            lit = 1;
        }
    }
    // PObjSetupMtx loads the normal matrix only for a joint flagged for lighting.
    if (lit && !w->sizing)
        j->flags |= JOBJ_LIGHTING;
}

static void WalkTree(JOBJ *root, NrmWalk *w)
{
    WalkJoint(root, w);
    if (root->child)
        JObj_ForEachJoint(root->child, WalkJoint, w);
}

int MapNormals_Add(JOBJ *root, JOBJ *skip)
{
    int start = OSGetTick();
    NrmStats s = {0};
    NrmWalk w = {.skip = skip, .sizing = 1, .s = &s};
    WalkTree(root, &w);
    if (s.max_tris == 0)
    {
        OSReport("[MapNormals] No display lists to convert\n");
        return 0;
    }
    // Corner links are u16 with NRM_NONE reserved.
    if (s.max_tris * 3 >= NRM_NONE || s.max_pos >= NRM_NONE)
    {
        OSReport("[MapNormals] Not added, a display list is too large\n");
        return -1;
    }

    stc_tris = HSD_MemAlloc(s.max_tris * sizeof(NrmTri));
    stc_next = HSD_MemAlloc(s.max_tris * 3 * sizeof(u16));
    stc_head = HSD_MemAlloc((s.max_pos + 1) * sizeof(u16));
    if (stc_tris && stc_next && stc_head)
    {
        memset(stc_head, 0xFF, (s.max_pos + 1) * sizeof(u16));
        w.sizing = 0;
        WalkTree(root, &w);
    }
    else
        w.failed = 1;
    if (stc_tris)
        HSD_Free(stc_tris);
    if (stc_next)
        HSD_Free(stc_next);
    if (stc_head)
        HSD_Free(stc_head);
    stc_tris = NULL;
    stc_next = NULL;
    stc_head = NULL;

    OSReport("[MapNormals] %s normals on %d POBJs (%d KB) in %.1f ms, %d had them, %d unsupported\n",
             w.failed ? "Ran out of memory after" : "Added", s.converted, s.bytes >> 10,
             OSTicksToMilliseconds(OSGetTick() - start), s.had, s.unsupported);
    return w.failed ? -1 : s.converted;
}

#include <string.h>

#include "os.h"
#include "game.h"
#include "obj.h"
#include "gx.h"
#include "stage.h"

#include "environment_destruction.h"
#include "carve.h"
#include "patch_mesh.h"

// The stage's playable terrain is a JObj tree whose root joint is entry 0 of the
// per-joint table at GrObj+0x104 (built from ModelSection.terrain by grLoadStage;
// 8-byte {JOBJ*, JOBJDesc*} stride, so [0] is the root JOBJ*). Its meshes render
// from POBJ display-list buffers that the draw path re-reads every frame, so
// editing those buffers in place changes what is drawn. The buffers live in the
// writable stage-archive blob and are reloaded fresh on the next stage load, so
// carving is permanent for the current stage only.
static JOBJ *stc_stage_root;

static JOBJ *StageRoot(void)
{
    GrObj *g;
    JOBJ **jt;

    if (stc_stage_root != NULL)
        return stc_stage_root;

    g = *stc_grobj;
    if (g == NULL)
        return NULL;
    jt = (JOBJ **)g->joint_table;
    if (jt == NULL)
        return NULL;

    stc_stage_root = jt[0];
    return stc_stage_root;
}

// One attribute's placement in a display-list vertex record, plus what it takes
// to turn the bytes there into floats. Indexed attributes hold an index into a
// shared array; direct ones hold the value inline.
typedef struct DlAttr
{
    int present;
    int off;        // byte offset within a vertex record
    int size;       // bytes it occupies there
    int indexed;
    u8 *array;      // indexed: array base
    int stride;     // indexed: array stride
    int comp_cnt;
    int comp_type;
    float scale;    // fixed-point to float
} DlAttr;

typedef struct DlLayout
{
    int stride;     // bytes per vertex record
    DlAttr pos, nrm, clr, tex;
    int attrs;      // ENV_ATTR_* set a patch mesh must mirror
} DlLayout;

static u16 ReadU16(u8 *p)
{
    return (u16)((p[0] << 8) | p[1]);
}

static float ReadF32(u8 *p)
{
    float f;
    memcpy(&f, p, 4);
    return f;
}

static int IsColorAttr(int attr)
{
    return attr == GX_VA_CLR0 || attr == GX_VA_CLR1;
}

// Bytes one attribute occupies in a vertex record. -1 for anything the walker
// cannot decode, which makes it leave the whole POBJ alone rather than risk
// misreading the stream.
static int AttrRecordBytes(HSD_VtxDescList *d)
{
    switch (d->attr_type)
    {
        case GX_NONE:
            return 0;
        case GX_INDEX8:
            return 1;
        case GX_INDEX16:
            return 2;
        case GX_DIRECT:
            if (d->attr <= GX_VA_TEX7MTXIDX)
                return 1; // matrix index
            if (IsColorAttr(d->attr))
            {
                switch (d->comp_type)
                {
                    case GX_RGB565: return 2;
                    case GX_RGB8:   return 3;
                    case GX_RGBX8:  return 4;
                    case GX_RGBA4:  return 2;
                    case GX_RGBA6:  return 3;
                    case GX_RGBA8:  return 4;
                    default:        return -1;
                }
            }
            {
                int comps = 0;
                int unit = 0;
                if (d->attr == GX_VA_POS)
                    comps = (d->comp_cnt == GX_POS_XYZ) ? 3 : 2;
                else if (d->attr == GX_VA_NRM)
                    comps = (d->comp_cnt == GX_NRM_XYZ) ? 3 : 9;
                else
                    comps = (d->comp_cnt == GX_TEX_ST) ? 2 : 1;
                switch (d->comp_type)
                {
                    case GX_U8:  case GX_S8:  unit = 1; break;
                    case GX_U16: case GX_S16: unit = 2; break;
                    case GX_F32:              unit = 4; break;
                    default: return -1;
                }
                return comps * unit;
            }
    }
    return -1;
}

static void FillAttr(DlAttr *a, HSD_VtxDescList *d, int off, int size)
{
    a->present = 1;
    a->off = off;
    a->size = size;
    a->indexed = (d->attr_type == GX_INDEX8 || d->attr_type == GX_INDEX16);
    a->array = (u8 *)d->vertex;
    a->stride = d->stride;
    a->comp_cnt = d->comp_cnt;
    a->comp_type = d->comp_type;
    a->scale = 1.0f / (float)(1 << d->frac);
    if (a->indexed && a->array == NULL)
        a->present = 0;
}

static int BuildLayout(POBJ *p, DlLayout *L)
{
    HSD_VtxDescList *d = p->verts;
    int off = 0;

    if (d == NULL)
        return -1;

    memset(L, 0, sizeof(*L));
    while (d->attr != GX_VA_NULL)
    {
        int n = AttrRecordBytes(d);
        if (n < 0)
            return -1;
        if (d->attr == GX_VA_POS)
            FillAttr(&L->pos, d, off, n);
        else if (d->attr == GX_VA_NRM)
            FillAttr(&L->nrm, d, off, n);
        else if (d->attr == GX_VA_CLR0)
            FillAttr(&L->clr, d, off, n);
        else if (d->attr == GX_VA_TEX0)
            FillAttr(&L->tex, d, off, n);
        off += n;
        d++;
    }

    if (!L->pos.present)
        return -1;
    L->stride = off;
    L->attrs = (L->nrm.present ? ENV_ATTR_NRM : 0) |
               (L->clr.present ? ENV_ATTR_CLR0 : 0) |
               (L->tex.present ? ENV_ATTR_TEX0 : 0);
    return 0;
}

static u8 *AttrData(DlAttr *a, u8 *rec)
{
    u8 *at = rec + a->off;

    if (!a->indexed)
        return at;
    return a->array + (a->size == 2 ? ReadU16(at) : at[0]) * a->stride;
}

static void ReadComps(DlAttr *a, u8 *rec, float *out, int n)
{
    u8 *src = AttrData(a, rec);
    int i;

    for (i = 0; i < n; i++)
    {
        switch (a->comp_type)
        {
            case GX_U8:  out[i] = (float)src[i] * a->scale; break;
            case GX_S8:  out[i] = (float)(s8)src[i] * a->scale; break;
            case GX_U16: out[i] = (float)ReadU16(src + i * 2) * a->scale; break;
            case GX_S16: out[i] = (float)(s16)ReadU16(src + i * 2) * a->scale; break;
            case GX_F32: out[i] = ReadF32(src + i * 4); break;
            default:     out[i] = 0.0f; break;
        }
    }
}

static u32 ReadColor(DlAttr *a, u8 *rec)
{
    u8 *src = AttrData(a, rec);

    switch (a->comp_type)
    {
        case GX_RGB8:  return ((u32)src[0] << 24) | ((u32)src[1] << 16) | ((u32)src[2] << 8) | 0xFF;
        case GX_RGBX8:
        case GX_RGBA8: return ((u32)src[0] << 24) | ((u32)src[1] << 16) | ((u32)src[2] << 8) |
                              (a->comp_type == GX_RGBA8 ? src[3] : 0xFF);
        default:       return 0xFFFFFFFF;
    }
}

// Position alone. Every triangle of a mesh the volume reaches gets tested, so
// the reject path never pays for the attributes only a carved triangle needs.
static void ReadPos(DlLayout *L, u8 *rec, Vec3 *out)
{
    float f[3];

    f[0] = f[1] = f[2] = 0.0f;
    ReadComps(&L->pos, rec, f, (L->pos.comp_cnt == GX_POS_XYZ) ? 3 : 2);
    out->X = f[0];
    out->Y = f[1];
    out->Z = f[2];
}

// One display-list vertex as the carver sees it, in the joint's local space.
static void ReadVtx(DlLayout *L, u8 *rec, EnvVtx *v)
{
    float f[3];

    ReadPos(L, rec, &v->pos);

    v->nrm.X = v->nrm.Y = v->nrm.Z = 0.0f;
    if (L->nrm.present)
    {
        ReadComps(&L->nrm, rec, f, 3);
        v->nrm.X = f[0];
        v->nrm.Y = f[1];
        v->nrm.Z = f[2];
    }

    v->s = v->t = 0.0f;
    if (L->tex.present)
    {
        ReadComps(&L->tex, rec, f, (L->tex.comp_cnt == GX_TEX_ST) ? 2 : 1);
        v->s = f[0];
        v->t = f[1];
    }

    v->clr = L->clr.present ? ReadColor(&L->clr, rec) : 0xFFFFFFFF;
}

static void XformPoint(MtxPtr m, Vec3 *in, Vec3 *out)
{
    out->X = m[0][0] * in->X + m[0][1] * in->Y + m[0][2] * in->Z + m[0][3];
    out->Y = m[1][0] * in->X + m[1][1] * in->Y + m[1][2] * in->Z + m[1][3];
    out->Z = m[2][0] * in->X + m[2][1] * in->Y + m[2][2] * in->Z + m[2][3];
}

static void FaceNormal(Vec3 *a, Vec3 *b, Vec3 *c, Vec3 *out)
{
    float ux = b->X - a->X, uy = b->Y - a->Y, uz = b->Z - a->Z;
    float vx = c->X - a->X, vy = c->Y - a->Y, vz = c->Z - a->Z;

    out->X = uy * vz - uz * vy;
    out->Y = uz * vx - ux * vz;
    out->Z = ux * vy - uy * vx;
}

// Twice the area of a triangle is the length of its cross product, so this
// compares against the square of twice the threshold and avoids a square root.
static int TriIsSliver(Vec3 *a, Vec3 *b, Vec3 *c)
{
    Vec3 n;
    float len2;

    FaceNormal(a, b, c, &n);
    len2 = n.X * n.X + n.Y * n.Y + n.Z * n.Z;
    return len2 < (2.0f * ENV_GEOM_SLIVER_AREA) * (2.0f * ENV_GEOM_SLIVER_AREA);
}

// A near-horizontal triangle is ground the rider stands on. Sparing it keeps the
// hole in the wall from swallowing the floor under the impact, and matches the
// same guard on the collision side so the two holes agree.
static int IsFloorTri(MtxPtr world, Vec3 *p0, Vec3 *p1, Vec3 *p2)
{
    Vec3 w0, w1, w2, n;

    XformPoint(world, p0, &w0);
    XformPoint(world, p1, &w1);
    XformPoint(world, p2, &w2);
    FaceNormal(&w0, &w1, &w2, &n);
    return EnvIsHorizontalSurface(n.X, n.Y, n.Z);
}

// A world-space box for one stage JOBJ or POBJ, so a carve can dismiss a mesh it
// does not reach without decoding a single vertex of it. Without this every
// chunk walks all ~20,000 triangles of the terrain to find the handful it cuts.
// Boxes cover the stage's own meshes only; a POBJ a carve generated has no entry
// and is always tested, which is correct - it is by definition near the action.
#define ENV_BOX_MAX 512

typedef struct GeomBox
{
    void *key;
    Vec3 lo, hi;
} GeomBox;

static GeomBox stc_box[ENV_BOX_MAX];
static int stc_box_num;
static int stc_box_built;

void EnvGeom_Reset(void)
{
    stc_stage_root = NULL;
    stc_box_num = 0;
    stc_box_built = 0;
    EnvPatch_Reset();
}

static GeomBox *BoxAdd(void *key)
{
    GeomBox *b;

    if (stc_box_num >= ENV_BOX_MAX)
        return NULL;
    b = &stc_box[stc_box_num++];
    b->key = key;
    b->lo.X = b->lo.Y = b->lo.Z = 1e30f;
    b->hi.X = b->hi.Y = b->hi.Z = -1e30f;
    return b;
}

static GeomBox *BoxFind(void *key)
{
    int lo = 0, hi = stc_box_num - 1;

    while (lo <= hi)
    {
        int mid = (lo + hi) >> 1;
        if (stc_box[mid].key == key)
            return &stc_box[mid];
        if ((u32)stc_box[mid].key < (u32)key)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

static void BoxGrowPoint(GeomBox *b, Vec3 *p)
{
    if (p->X < b->lo.X) b->lo.X = p->X;
    if (p->Y < b->lo.Y) b->lo.Y = p->Y;
    if (p->Z < b->lo.Z) b->lo.Z = p->Z;
    if (p->X > b->hi.X) b->hi.X = p->X;
    if (p->Y > b->hi.Y) b->hi.Y = p->Y;
    if (p->Z > b->hi.Z) b->hi.Z = p->Z;
}

static void BoxGrowBox(GeomBox *b, const GeomBox *s)
{
    if (s->lo.X > s->hi.X)
        return;
    BoxGrowPoint(b, (Vec3 *)&s->lo);
    BoxGrowPoint(b, (Vec3 *)&s->hi);
}

// 1 when nothing inside the box can be within `r` of `c`. An empty box - a mesh
// with no readable vertices - misses everything, which is what the carve would
// have concluded the long way round.
static int BoxMissesSphere(const GeomBox *b, const Vec3 *c, float r)
{
    float qx = 0.0f, qy = 0.0f, qz = 0.0f;

    if (b->lo.X > b->hi.X)
        return 1;
    if (c->X < b->lo.X) qx = b->lo.X - c->X; else if (c->X > b->hi.X) qx = c->X - b->hi.X;
    if (c->Y < b->lo.Y) qy = b->lo.Y - c->Y; else if (c->Y > b->hi.Y) qy = c->Y - b->hi.Y;
    if (c->Z < b->lo.Z) qz = b->lo.Z - c->Z; else if (c->Z > b->hi.Z) qz = c->Z - b->hi.Z;
    return qx * qx + qy * qy + qz * qz > r * r;
}

// Bounds are read one vertex record at a time rather than one triangle at a
// time - the primitive structure does not matter to a box.
static void PobjBounds(POBJ *p, MtxPtr world, GeomBox *pb)
{
    DlLayout L;
    u8 *dl = p->display;
    int dlbytes = (int)p->n_display * 32;
    int pos = 0;

    if (dl == NULL || dlbytes == 0 || BuildLayout(p, &L) != 0)
        return;

    while (pos + 3 <= dlbytes)
    {
        int op = dl[pos];
        int count, vbytes, i;

        if ((op & 0x80) == 0)
        {
            pos++;
            continue;
        }
        count = (dl[pos + 1] << 8) | dl[pos + 2];
        vbytes = count * L.stride;
        if (count == 0 || pos + 3 + vbytes > dlbytes)
            break;

        for (i = 0; i < count; i++)
        {
            Vec3 lp, wp;

            ReadPos(&L, dl + pos + 3 + i * L.stride, &lp);
            XformPoint(world, &lp, &wp);
            BoxGrowPoint(pb, &wp);
        }
        pos += 3 + vbytes;
    }
}

static void BoundsJoint(JOBJ *j)
{
    GeomBox *jb;
    DOBJ *d;

    if (j->dobj == NULL)
        return;
    jb = BoxAdd(j);
    if (jb == NULL)
        return;

    for (d = j->dobj; d != NULL; d = d->next)
    {
        POBJ *p;

        for (p = d->pobj; p != NULL; p = p->next)
        {
            GeomBox *pb = BoxAdd(p);

            if (pb == NULL)
                return;
            PobjBounds(p, j->rotMtx, pb);
            BoxGrowBox(jb, pb);
        }
    }
}

static void BoundsTree(JOBJ *j)
{
    for (; j != NULL; j = j->sibling)
    {
        BoundsJoint(j);
        BoundsTree(j->child);
    }
}

static void BoxSort(void)
{
    int i, j;

    for (i = 1; i < stc_box_num; i++)
    {
        GeomBox t = stc_box[i];

        for (j = i; j > 0 && (u32)stc_box[j - 1].key > (u32)t.key; j--)
            stc_box[j] = stc_box[j - 1];
        stc_box[j] = t;
    }
}

void EnvGeom_BuildBounds(void)
{
    JOBJ *root = StageRoot();

    if (stc_box_built || root == NULL)
        return;

    // Joint matrices have to be the live ones, so this runs on a gameplay frame
    // rather than at stage load. It reads every vertex of the terrain once and
    // then no carve ever has to again.
    stc_box_num = 0;
    BoundsTree(root);
    BoxSort();
    stc_box_built = 1;
    OSReport("[EnvGeom] Cached bounds for %d stage meshes\n", stc_box_num);
}

// Headroom over a re-issued primitive's own triangle count: clipping turns each
// carved triangle into several remnants.
#define ENV_REISSUE_SLACK 32

typedef struct CarveCtx
{
    JOBJ *joint;
    DOBJ *dobj;
    DlLayout *L;
    EnvVolume *lv;      // carve volume in the joint's local space
    MtxPtr world;
    int fill_mode;      // ENV_FILL_TEXTURE / ENV_FILL_COLOR
    EnvPatch *skin;     // patch sharing the source material
    int fail;           // a triangle did not fit: the source must be left whole
    int emitted;
    int retired;        // source primitives actually taken out of the stage mesh

    // Where a walk of generated geometry has to stop, so this carve never reads
    // back its own output.
    POBJ *limit_pobj;
    int limit_used;

    // Where the lining goes. The first mesh the volume actually cuts owns it -
    // the lining is one closed tube for the whole chunk, not one per triangle.
    int lin_have;
    JOBJ *lin_joint;
    DOBJ *lin_dobj;
    EnvVolume lin_vol;
    int lin_attrs;
    u32 lin_clr;
    float lin_s, lin_t, lin_scale;
} CarveCtx;

// Carving generated geometry emits straight back into the patch it came out of,
// as long as that patch is shaded the way this triangle wants. That is what lets
// a tunnel be driven deeper than once: the walk stops at the patch's entry-time
// length, so the appends behind it are never read back.
static EnvPatch *PatchFor(JOBJ *joint, DOBJ *dobj, int attrs, int flat)
{
    EnvPatch *own = EnvPatch_ForDobj(dobj);

    if (own != NULL && EnvPatch_IsFlat(own) == flat)
        return own;
    return EnvPatch_Get(joint, dobj, attrs, flat);
}

static EnvPatch *SkinPatch(CarveCtx *cx)
{
    if (cx->skin == NULL)
    {
        // A triangle already living in a patch is shaded by that patch's
        // material, so its remnants belong there whichever material that is.
        cx->skin = EnvPatch_ForDobj(cx->dobj);
        if (cx->skin == NULL)
            cx->skin = EnvPatch_Get(cx->joint, cx->dobj, cx->L->attrs, 0);
    }
    return cx->skin;
}

static void EmitTri(CarveCtx *cx, EnvPatch *p, EnvVtx *a, EnvVtx *b, EnvVtx *c)
{
    if (p == NULL || !EnvPatch_AddTri(p, a, b, c))
    {
        cx->fail = 1;
        return;
    }
    cx->emitted++;
}

// Fan out one clipped remnant of a source triangle. Its vertices keep the
// source's interpolated normal, texture coords and colour, so the surviving
// surface reads as the same wall it was cut from. Remnants too small to cover a
// pixel are dropped: a spot carved over and over would otherwise accumulate them
// without bound, and each one still costs a whole triangle's worth of arena.
static void EmitPoly(CarveCtx *cx, EnvPoly *poly)
{
    EnvPatch *p = SkinPatch(cx);
    int i;

    for (i = 1; i + 1 < poly->n; i++)
    {
        if (TriIsSliver(&poly->v[0].pos, &poly->v[i].pos, &poly->v[i + 1].pos))
            continue;
        EmitTri(cx, p, &poly->v[0], &poly->v[i], &poly->v[i + 1]);
    }
}

// Remember which mesh the lining will hang off, and how its texture runs, from
// the first piece the volume takes out. The lining is built from the volume once
// the whole tree has been walked, so it needs a joint to live in and a texel
// density to continue the wall's texture at.
static void NoteLiningSource(CarveCtx *cx, EnvPoly *removed)
{
    float s = 0.0f, t = 0.0f, inv, best = 0.0f;
    int i;

    if (cx->lin_have)
        return;

    cx->lin_have = 1;
    cx->lin_joint = cx->joint;
    cx->lin_dobj = cx->dobj;
    cx->lin_vol = *cx->lv;
    cx->lin_attrs = cx->L->attrs;
    cx->lin_clr = removed->v[0].clr;
    cx->lin_scale = 0.0f;

    inv = 1.0f / (float)removed->n;
    for (i = 0; i < removed->n; i++)
    {
        EnvVtx *a = &removed->v[i];
        EnvVtx *b = &removed->v[(i + 1) % removed->n];
        float dx = b->pos.X - a->pos.X;
        float dy = b->pos.Y - a->pos.Y;
        float dz = b->pos.Z - a->pos.Z;
        float dp2 = dx * dx + dy * dy + dz * dz;
        float du, dv;

        s += a->s;
        t += a->t;

        // The longest edge gives the steadiest reading of texture units per
        // world unit; a short one is mostly rounding.
        if (dp2 <= best || dp2 < 1e-6f)
            continue;
        best = dp2;
        du = b->s - a->s;
        dv = b->t - a->t;
        cx->lin_scale = sqrtf((du * du + dv * dv) / dp2);
    }

    cx->lin_s = s * inv;
    cx->lin_t = t * inv;
}

// Worth clipping: reaches the volume and is not floor the rider needs. Reads
// positions only.
static int TriTouches(CarveCtx *cx, u8 *r0, u8 *r1, u8 *r2)
{
    Vec3 p0, p1, p2;

    ReadPos(cx->L, r0, &p0);
    ReadPos(cx->L, r1, &p1);
    ReadPos(cx->L, r2, &p2);
    if (EnvVolume_TriOutside(cx->lv, &p0, &p1, &p2))
        return 0;
    return !IsFloorTri(cx->world, &p0, &p1, &p2);
}

// Clip one source triangle against the volume. Returns 1 when it was carved, in
// which case the caller must retire the original: its remnants now live in the
// patch mesh and the hole it opened is covered by the lining.
static int CarveTri(CarveCtx *cx, u8 *r0, u8 *r1, u8 *r2)
{
    // Static rather than automatic: the clip workspace is several kilobytes and
    // this sits at the bottom of the tree walk. Only one carve runs at a time.
    static EnvPoly kept[ENV_KEPT_MAX], removed;
    EnvVtx tri[3];
    int n_kept, i;

    if (!TriTouches(cx, r0, r1, r2))
        return 0;

    ReadVtx(cx->L, r0, &tri[0]);
    ReadVtx(cx->L, r1, &tri[1]);
    ReadVtx(cx->L, r2, &tri[2]);

    n_kept = EnvVolume_CarveTri(cx->lv, tri, kept, &removed);
    if (removed.n < 3)
        return 0;

    NoteLiningSource(cx, &removed);
    for (i = 0; i < n_kept; i++)
        EmitPoly(cx, &kept[i]);
    return 1;
}

static void CopyTri(CarveCtx *cx, u8 *r0, u8 *r1, u8 *r2)
{
    EnvVtx tri[3];

    ReadVtx(cx->L, r0, &tri[0]);
    ReadVtx(cx->L, r1, &tri[1]);
    ReadVtx(cx->L, r2, &tri[2]);
    EmitTri(cx, SkinPatch(cx), &tri[0], &tri[1], &tri[2]);
}

// Triangles implied by one display-list primitive, in submission order.
static int PrimTriNum(int prim, int count)
{
    switch (prim)
    {
        case GX_TRIANGLES:     return count / 3;
        case GX_TRIANGLESTRIP: return (count >= 3) ? count - 2 : 0;
        case GX_TRIANGLEFAN:   return (count >= 3) ? count - 2 : 0;
        case GX_QUADS:         return (count / 4) * 2;
        default:               return 0;
    }
}

static void PrimTriVerts(int prim, int i, int *a, int *b, int *c)
{
    switch (prim)
    {
        case GX_TRIANGLES:
            *a = i * 3; *b = i * 3 + 1; *c = i * 3 + 2;
            break;
        case GX_TRIANGLESTRIP:
            // Odd triangles run the other way round; swapping the first two
            // vertices restores the winding the hardware would use.
            if (i & 1) { *a = i + 1; *b = i; *c = i + 2; }
            else       { *a = i;     *b = i + 1; *c = i + 2; }
            break;
        case GX_TRIANGLEFAN:
            *a = 0; *b = i + 1; *c = i + 2;
            break;
        default: // GX_QUADS
            {
                int q = (i >> 1) * 4;
                if (i & 1) { *a = q; *b = q + 2; *c = q + 3; }
                else       { *a = q; *b = q + 1; *c = q + 2; }
            }
            break;
    }
}

// Retire one triangle of a GX_TRIANGLES batch by pointing all three of its
// vertices at the first one: a zero-area triangle the rasterizer drops, and no
// other triangle in the batch shares the record.
static void Degenerate(DlLayout *L, u8 *v0, u8 *v1, u8 *v2)
{
    memcpy(v1 + L->pos.off, v0 + L->pos.off, L->pos.size);
    memcpy(v2 + L->pos.off, v0 + L->pos.off, L->pos.size);
}

// What survives of a strip or quad batch the volume runs through: the untouched
// run in front of the hole, the untouched run behind it, and the triangles
// between them that the patch mesh has to take. Re-issuing a whole primitive
// costs GX_DIRECT records for geometry that never changed - twenty times the
// bytes the indexed source used - so a long strip through a building is what
// spends the arena.
typedef struct PrimSplit
{
    int pre_verts;   // vertices the original header keeps, 0 for none
    int suf_vtx;     // first vertex of the suffix, -1 for none
    int mid_lo;      // first triangle the patch must take
    int mid_hi;      // last
} PrimSplit;

static int PlanSplit(int prim, int count, int tris, int stride, int lo, int hi,
                     PrimSplit *s)
{
    int min_verts, sv, pre;

    if (prim == GX_TRIANGLESTRIP)
    {
        min_verts = 3;
        pre = (lo >= 1) ? lo + 2 : 0;
        // A strip always starts on even parity, so a suffix that began on an odd
        // vertex would run every one of its triangles the wrong way round.
        sv = (hi + 1 + 1) & ~1;
    }
    else if (prim == GX_QUADS)
    {
        min_verts = 4;
        pre = (lo >= 2) ? (lo / 2) * 4 : 0;
        sv = ((hi / 2) + 1) * 4;
    }
    else
    {
        return 0; // a fan shares its first vertex with every triangle it has
    }

    s->pre_verts = (pre >= min_verts) ? pre : 0;
    s->suf_vtx = -1;
    // The suffix's header is written into the tail of a vertex record the carve
    // is removing anyway, so there has to be a removed record to put it in.
    if (count - sv >= min_verts && (sv - s->pre_verts) * stride >= 3)
        s->suf_vtx = sv;
    if (s->pre_verts == 0 && s->suf_vtx < 0)
        return 0;

    if (s->pre_verts == 0)
        s->mid_lo = 0;
    else
        s->mid_lo = (prim == GX_TRIANGLESTRIP) ? s->pre_verts - 2 : (s->pre_verts / 4) * 2;
    if (s->suf_vtx < 0)
        s->mid_hi = tris - 1;
    else
        s->mid_hi = (prim == GX_TRIANGLESTRIP) ? s->suf_vtx - 1 : (s->suf_vtx / 4) * 2 - 1;
    return 1;
}

static void ApplySplit(u8 *prim_at, u8 *body, int op, int count, int vbytes,
                       int stride, const PrimSplit *s)
{
    u8 *gap_from, *gap_to;

    if (s->pre_verts > 0)
    {
        prim_at[1] = (u8)(s->pre_verts >> 8);
        prim_at[2] = (u8)s->pre_verts;
        gap_from = body + s->pre_verts * stride;
    }
    else
    {
        gap_from = prim_at;
    }

    if (s->suf_vtx >= 0)
    {
        int n = count - s->suf_vtx;

        gap_to = body + s->suf_vtx * stride - 3;
        memset(gap_from, 0, (int)(gap_to - gap_from));
        gap_to[0] = (u8)op;
        gap_to[1] = (u8)(n >> 8);
        gap_to[2] = (u8)n;
    }
    else
    {
        gap_to = body + vbytes;
        memset(gap_from, 0, (int)(gap_to - gap_from));
    }
}

static int CarvePobj(CarveCtx *cx, POBJ *p)
{
    DlLayout L;
    u8 *dl = p->display;
    int dlbytes = (int)p->n_display * 32;
    int pos = 0;
    int touched = 0;

    if (dl == NULL || dlbytes == 0)
        return 0;
    if (p == cx->limit_pobj)
        dlbytes = cx->limit_used;
    if (BuildLayout(p, &L) != 0)
        return 0; // vertex format the walker cannot decode: leave this mesh alone
    cx->L = &L;

    while (pos + 3 <= dlbytes)
    {
        u8 *prim_at = dl + pos;
        u8 *body;
        int op = dl[pos];
        int prim = op & 0xF8;
        int count = (dl[pos + 1] << 8) | dl[pos + 2];
        int vbytes = count * L.stride;
        int tris, i, lo = -1, hi = -1;

        // A zero byte is a GX_NOP: either the padding after the last primitive or
        // a primitive an earlier carve retired. Skipping it rather than stopping
        // is what lets a mesh be carved more than once - a retired primitive
        // sits in the middle of the list, and everything behind it has to stay
        // reachable.
        if ((op & 0x80) == 0)
        {
            pos++;
            continue;
        }
        if (count == 0 || pos + 3 + vbytes > dlbytes)
            break; // malformed: stop rather than run off the buffer

        body = dl + pos + 3;
        tris = PrimTriNum(prim, count);
        if (tris == 0)
        {
            pos = pos + 3 + vbytes;
            continue;
        }

        if (prim == GX_TRIANGLES)
        {
            for (i = 0; i < tris; i++)
            {
                u8 *r0 = body + (i * 3 + 0) * L.stride;
                u8 *r1 = body + (i * 3 + 1) * L.stride;
                u8 *r2 = body + (i * 3 + 2) * L.stride;
                int kept_before = cx->emitted;

                cx->fail = 0;
                EnvPatch_Mark();
                if (!CarveTri(cx, r0, r1, r2))
                    continue;

                // Retiring a triangle whose replacement did not fit would leave a
                // hole with nothing behind it, so the source stays as it was.
                if (cx->fail)
                {
                    EnvPatch_Rollback();
                    cx->emitted = kept_before;
                    continue;
                }
                Degenerate(&L, r0, r1, r2);
                touched++;
            }
            pos = pos + 3 + vbytes;
            continue;
        }

        // Strips, fans and quads share vertex records between neighbouring
        // triangles, so a single triangle cannot be retired on its own. Only the
        // run the volume actually reaches has to move into the patch mesh; the
        // untouched ends stay where they are as primitives of their own.
        for (i = 0; i < tris; i++)
        {
            int ia, ib, ic;

            PrimTriVerts(prim, i, &ia, &ib, &ic);
            if (TriTouches(cx, body + ia * L.stride, body + ib * L.stride,
                           body + ic * L.stride))
            {
                if (lo < 0)
                    lo = i;
                hi = i;
            }
        }

        if (lo >= 0)
        {
            PrimSplit sp;
            int split = PlanSplit(prim, count, tris, L.stride, lo, hi, &sp);
            int m_lo = split ? sp.mid_lo : 0;
            int m_hi = split ? sp.mid_hi : tris - 1;
            int kept_before = cx->emitted;
            int carved = 0;

            // Price the run up front rather than copying most of it and then
            // unwinding.
            if (!EnvPatch_CanFit(SkinPatch(cx), (m_hi - m_lo + 1) + ENV_REISSUE_SLACK))
            {
                pos = pos + 3 + vbytes;
                continue;
            }

            cx->fail = 0;
            EnvPatch_Mark();
            for (i = m_lo; i <= m_hi; i++)
            {
                int ia, ib, ic;
                u8 *r0, *r1, *r2;

                PrimTriVerts(prim, i, &ia, &ib, &ic);
                r0 = body + ia * L.stride;
                r1 = body + ib * L.stride;
                r2 = body + ic * L.stride;
                if (CarveTri(cx, r0, r1, r2))
                    carved++;
                else
                    CopyTri(cx, r0, r1, r2);
            }

            // TriTouches only rejects triangles wholly outside one plane, so a
            // primitive can pass it and still lose nothing to the clip. Rewriting
            // it then would spend the arena on an identical copy.
            if (cx->fail || carved == 0)
            {
                EnvPatch_Rollback();
                cx->emitted = kept_before;
            }
            else
            {
                if (split)
                    ApplySplit(prim_at, body, op, count, vbytes, L.stride, &sp);
                else
                    memset(prim_at, 0, 3 + vbytes);
                touched++;
            }
        }

        pos = pos + 3 + vbytes;
    }

    if (touched > 0)
        DCFlushRange(p->display, (u32)p->n_display * 32);

    cx->retired += touched;
    cx->L = NULL;
    return touched;
}

static void CarveDobj(CarveCtx *cx, DOBJ *d)
{
    EnvPatch *own = EnvPatch_ForDobj(d);
    POBJ *p, *tail;

    cx->dobj = d;
    cx->skin = NULL;
    cx->L = NULL;
    cx->limit_pobj = NULL;
    cx->limit_used = 0;
    if (own != NULL)
        EnvPatch_WalkLimit(own, &cx->limit_pobj, &cx->limit_used);

    // Emission appends POBJs to this same DOBJ when a display list fills up, so
    // stop at the chain end as it stood on entry.
    for (tail = d->pobj; tail != NULL && tail->next != NULL; tail = tail->next)
        ;
    for (p = d->pobj; p != NULL; p = p->next)
    {
        GeomBox *pb = BoxFind(p);

        if (pb == NULL || !BoxMissesSphere(pb, &cx->lv->center, cx->lv->radius))
            CarvePobj(cx, p);
        if (p == tail)
            break;
    }
}

static void CarveJoint(JOBJ *j, CarveCtx *cx, const EnvVolume *wv)
{
    EnvVolume lv;
    GeomBox *jb;
    DOBJ *d, *tail;
    int before;

    if (j->dobj == NULL || (j->flags & JOBJ_HIDDEN) != 0)
        return;

    jb = BoxFind(j);
    if (jb != NULL && BoxMissesSphere(jb, &wv->center, wv->radius))
        return;
    if (!EnvVolume_ToLocal(wv, j->rotMtx, &lv))
        return;

    cx->joint = j;
    cx->lv = &lv;
    cx->world = j->rotMtx;
    before = cx->emitted;

    for (tail = j->dobj; tail != NULL && tail->next != NULL; tail = tail->next)
        ;
    for (d = j->dobj; d != NULL; d = d->next)
    {
        if ((d->flags & DOBJ_HIDDEN) == 0)
            CarveDobj(cx, d);
        if (d == tail)
            break;
    }

    // Generated geometry lives on this joint but has no box of its own, and it
    // reaches as far as the volume that made it. Growing the joint's box by that
    // much keeps the joint reachable next time without leaving its POBJs to be
    // measured individually.
    if (cx->emitted > before && jb != NULL)
    {
        Vec3 lo, hi;

        lo.X = wv->center.X - wv->radius;
        lo.Y = wv->center.Y - wv->radius;
        lo.Z = wv->center.Z - wv->radius;
        hi.X = wv->center.X + wv->radius;
        hi.Y = wv->center.Y + wv->radius;
        hi.Z = wv->center.Z + wv->radius;
        BoxGrowPoint(jb, &lo);
        BoxGrowPoint(jb, &hi);
    }
}

// Siblings are walked iteratively: City Trial's terrain root has hundreds of
// them and recursing per sibling would run the stack down.
static void CarveTree(JOBJ *j, CarveCtx *cx, const EnvVolume *wv)
{
    for (; j != NULL; j = j->sibling)
    {
        CarveJoint(j, cx, wv);
        CarveTree(j->child, cx, wv);
    }
}

// The surface of the hole, built from the volume rather than from the triangles
// it cut. Every face lies exactly in one of the clip planes, which is where the
// cut rim of every surface it went through also lies, so the two meet with
// nothing between them to see through - however coarsely the wall happened to be
// tessellated, and whichever way its triangles faced.
//
// Drawn two-sided like the rest of the generated mesh: a lining is looked at
// from inside the cavity, which is the back of the face bounding it.
static void EmitLining(CarveCtx *cx, int with_back)
{
    static EnvLinTri lin[ENV_LINING_MAX];
    EnvPatch *p;
    int n, i, j, flat;

    if (!cx->lin_have)
        return;

    cx->joint = cx->lin_joint;
    cx->dobj = cx->lin_dobj;
    cx->skin = NULL;

    flat = (cx->fill_mode == ENV_FILL_COLOR) ? 1 : 0;
    p = PatchFor(cx->lin_joint, cx->lin_dobj,
                 flat ? (cx->lin_attrs & ~(ENV_ATTR_TEX0 | ENV_ATTR_CLR0)) : cx->lin_attrs,
                 flat);

    n = EnvVolume_Lining(&cx->lin_vol, with_back, lin);
    for (i = 0; i < n; i++)
    {
        EnvVtx v[3];

        for (j = 0; j < 3; j++)
        {
            v[j].pos = lin[i].v[j];
            // Unit length and pointing out of the cavity. Lighting takes a normal
            // at face value, and a raw cross product is twice the face's area: at
            // chunk scale that drives the diffuse term past its clamp either way,
            // so a face comes out blown white or solid black depending only on
            // which way its winding ran.
            v[j].nrm = lin[i].n;
            v[j].s = cx->lin_s + lin[i].uv[j][0] * cx->lin_scale;
            v[j].t = cx->lin_t + lin[i].uv[j][1] * cx->lin_scale;
            v[j].clr = cx->lin_clr;
        }
        EmitTri(cx, p, &v[0], &v[1], &v[2]);
    }
}

int env_geom_stat_retired;

int EnvGeom_CarveVolume(const EnvVolume *wv, int fill_mode, int with_back)
{
    JOBJ *root = StageRoot();
    CarveCtx cx;

    env_geom_stat_retired = 0;
    if (root == NULL)
        return 0;
    EnvGeom_BuildBounds();

    memset(&cx, 0, sizeof(cx));
    cx.fill_mode = fill_mode;

    CarveTree(root, &cx, wv);
    if (cx.retired > 0)
        EmitLining(&cx, with_back);
    EnvPatch_Flush();
    env_geom_stat_retired = cx.retired;
    return cx.emitted;
}

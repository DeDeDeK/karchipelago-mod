#include <string.h>

#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "gx.h"

#include "patch_mesh.h"

// Everything a carve generates - patch bookkeeping, HSD objects, and the display
// lists themselves - comes out of one arena reserved at boot. Nothing is ever
// freed piecemeal; a stage load rewinds the whole thing.
// Chunks are small because a patch that only ever holds a few triangles still
// costs a whole one: a carve that touches many meshes at once would otherwise
// spend the arena on padding.
#define ENV_ARENA_SIZE (512 * 1024)
#define ENV_CHUNK_SIZE 2048
#define ENV_PATCH_MAX 192

// Display lists are fed to GXCallDisplayList, which reads 32-byte lines.
#define ENV_DL_ALIGN 32

// All patch geometry is emitted as one GX_TRIANGLES primitive per chunk, in
// vertex format 0 - the format HSD_PObjDisp programs from the POBJ's descriptor
// list before calling the list.
#define ENV_PATCH_VTXFMT 0
#define ENV_DL_OPCODE (GX_TRIANGLES | ENV_PATCH_VTXFMT)

// Crater walls in flat-fill mode. Concrete grey, lit by the joint's own lights.
static HSD_Material stc_fill_material = {
    { 60, 60, 66, 255 },    // ambient
    { 150, 150, 158, 255 }, // diffuse
    { 0, 0, 0, 255 },       // specular
    1.0f,                   // alpha
    0.0f,                   // shininess
};

struct EnvPatch
{
    DOBJ *src;       // the mesh piece this patch extends
    DOBJ *dobj;      // ours, appended to the source joint's DOBJ list
    POBJ *pobj;      // tail of dobj's POBJ chain, the one being filled
    u8 *chunk;       // pobj's display list
    int used;        // bytes written into chunk, header included
    int stride;      // bytes per vertex record
    int attrs;
    int flat_fill;
    int dirty;
    int full;      // arena ran out; stop retrying
    POBJ *mark_pobj;
    int mark_used;
};

static u8 *stc_arena;
static int stc_arena_used;
static EnvPatch stc_patch[ENV_PATCH_MAX];
static int stc_patch_num;

void EnvPatch_OnBoot(void)
{
    stc_arena = HSD_MemAlloc(ENV_ARENA_SIZE + ENV_DL_ALIGN);
    stc_arena_used = 0;
    stc_patch_num = 0;
    if (stc_arena == NULL)
        OSReport("[EnvPatch] Arena alloc failed (%d bytes)\n", ENV_ARENA_SIZE);
}

void EnvPatch_Reset(void)
{
    stc_arena_used = 0;
    stc_patch_num = 0;
}

int EnvPatch_ArenaUsed(void)
{
    return stc_arena_used;
}

int EnvPatch_ArenaSize(void)
{
    return ENV_ARENA_SIZE;
}

static void *ArenaAlloc(int size, int align)
{
    u8 *p;
    int base = (int)stc_arena;
    int at = (stc_arena_used + base + align - 1) & ~(align - 1);

    at -= base;
    if (stc_arena == NULL || at + size > ENV_ARENA_SIZE)
        return NULL;

    p = stc_arena + at;
    stc_arena_used = at + size;
    memset(p, 0, size);
    return p;
}

// Descriptor list for a patch POBJ. Every attribute is GX_DIRECT, so the display
// list carries the values themselves and no vertex array has to be reachable.
// Order matches the record layout, which must follow GX's canonical attribute
// order: POS, NRM, CLR0, TEX0.
static int BuildDesc(HSD_VtxDescList *d, int attrs)
{
    int stride = 0;

    d->attr = GX_VA_POS;
    d->attr_type = GX_DIRECT;
    d->comp_cnt = GX_POS_XYZ;
    d->comp_type = GX_F32;
    d->frac = 0;
    d->stride = 12;
    d->vertex = NULL;
    stride += 12;
    d++;

    if (attrs & ENV_ATTR_NRM)
    {
        d->attr = GX_VA_NRM;
        d->attr_type = GX_DIRECT;
        d->comp_cnt = GX_NRM_XYZ;
        d->comp_type = GX_F32;
        d->frac = 0;
        d->stride = 12;
        d->vertex = NULL;
        stride += 12;
        d++;
    }

    if (attrs & ENV_ATTR_CLR0)
    {
        d->attr = GX_VA_CLR0;
        d->attr_type = GX_DIRECT;
        d->comp_cnt = GX_CLR_RGBA;
        d->comp_type = GX_RGBA8;
        d->frac = 0;
        d->stride = 4;
        d->vertex = NULL;
        stride += 4;
        d++;
    }

    if (attrs & ENV_ATTR_TEX0)
    {
        d->attr = GX_VA_TEX0;
        d->attr_type = GX_DIRECT;
        d->comp_cnt = GX_TEX_ST;
        d->comp_type = GX_F32;
        d->frac = 0;
        d->stride = 8;
        d->vertex = NULL;
        stride += 8;
        d++;
    }

    d->attr = GX_VA_NULL;
    return stride;
}

// Materials are cloned rather than shared. HSD_MObjAnim (0x803f9ebc) steps
// mobj->aobj and then unconditionally walks mobj->tobj, and JObj_AnimAll reaches
// every DOBJ on a joint - so a patch pointing at the stage's own MObj would run
// that material's texture animation twice a frame. The clones carry no AObj of
// their own, so an animated source texture keeps its own pace and the patch
// holds the frame it was cut at.
#define ENV_TOBJ_CHAIN_MAX 4

static MOBJ *CloneMaterial(MOBJ *src, int flat_fill)
{
    MOBJ *m = ArenaAlloc(sizeof(MOBJ), 4);
    TOBJ *st, *prev = NULL;
    int i;

    if (m == NULL || src == NULL)
        return NULL;
    *m = *src;
    m->aobj = NULL;

    if (flat_fill)
    {
        // Untextured: the crater walls take the material colour straight, with
        // no texture stage and no vertex colour to modulate it.
        m->rendermode = (m->rendermode & ~(RENDER_TEXTURES | RENDER_TOON | RENDER_VERTEX)) |
                        RENDER_CONSTANT;
        m->tobj = NULL;
        m->tevdesc = NULL;
        m->texp = NULL;
        m->mat = &stc_fill_material;
        return m;
    }

    m->tobj = NULL;
    st = src->tobj;
    for (i = 0; i < ENV_TOBJ_CHAIN_MAX && st != NULL; i++, st = st->next)
    {
        TOBJ *t = ArenaAlloc(sizeof(TOBJ), 4);
        if (t == NULL)
            break;
        *t = *st;
        t->aobj = NULL;
        t->next = NULL;
        if (prev == NULL)
            m->tobj = t;
        else
            prev->next = t;
        prev = t;
    }

    return m;
}

// Start a fresh display list for `p` and hang it off a new POBJ at the tail of
// its DOBJ's chain. Called for the first triangle and again whenever a chunk
// fills, so a patch grows without ever moving a buffer the GP may be reading.
static int NewChunk(EnvPatch *p)
{
    POBJ *pobj = ArenaAlloc(sizeof(POBJ), 4);
    u8 *chunk = ArenaAlloc(ENV_CHUNK_SIZE, ENV_DL_ALIGN);
    HSD_VtxDescList *desc;

    if (pobj == NULL || chunk == NULL)
        return 0;

    if (p->pobj != NULL)
    {
        desc = p->pobj->verts;
    }
    else
    {
        desc = ArenaAlloc(sizeof(HSD_VtxDescList) * 5, 4);
        if (desc == NULL)
            return 0;
        BuildDesc(desc, p->attrs);
    }

    pobj->parent = (p->src->pobj != NULL) ? p->src->pobj->parent : 0;
    pobj->next = NULL;
    pobj->verts = desc;
    // Neither cull bit: generated geometry is two-sided. A crater's walls are
    // seen from inside the cavity, which is the back of the face that bounds it,
    // and a wall thin enough to be punched clean through leaves remnants that get
    // looked at from behind. Culling either side shows the hole as see-through.
    pobj->flags = 0;
    pobj->n_display = 0;
    pobj->display = chunk;

    chunk[0] = ENV_DL_OPCODE;
    chunk[1] = 0;
    chunk[2] = 0;

    if (p->pobj != NULL)
        p->pobj->next = pobj;
    else
        p->dobj->pobj = pobj;

    p->pobj = pobj;
    p->chunk = chunk;
    p->used = 3;
    return 1;
}

EnvPatch *EnvPatch_Get(JOBJ *joint, DOBJ *src, int attrs, int flat_fill)
{
    EnvPatch *p;
    DOBJ *d, *tail;
    int i;

    if (joint == NULL || src == NULL)
        return NULL;

    for (i = 0; i < stc_patch_num; i++)
        if (stc_patch[i].src == src && stc_patch[i].flat_fill == flat_fill)
            return &stc_patch[i];

    if (stc_patch_num >= ENV_PATCH_MAX)
        return NULL;

    d = ArenaAlloc(sizeof(DOBJ), 4);
    if (d == NULL)
        return NULL;

    d->parent = src->parent;
    d->next = NULL;
    d->mobj = CloneMaterial(src->mobj, flat_fill);
    d->pobj = NULL;
    d->aobj = NULL;
    // Keeps the source's render bucket bits (1/2/3) so the patch draws in the
    // same pass as the surface it came out of.
    d->flags = src->flags & ~DOBJ_HIDDEN;
    if (d->mobj == NULL)
        return NULL;

    p = &stc_patch[stc_patch_num];
    p->src = src;
    p->dobj = d;
    p->pobj = NULL;
    p->chunk = NULL;
    p->used = 0;
    p->attrs = attrs;
    p->flat_fill = flat_fill;
    p->dirty = 0;
    p->full = 0;
    p->stride = 12 +
                ((attrs & ENV_ATTR_NRM) ? 12 : 0) +
                ((attrs & ENV_ATTR_CLR0) ? 4 : 0) +
                ((attrs & ENV_ATTR_TEX0) ? 8 : 0);

    if (!NewChunk(p))
        return NULL;

    // A patch created mid-carve marks as empty, so a rollback rewinds it to
    // holding nothing rather than leaving a half-written list behind.
    p->mark_pobj = p->pobj;
    p->mark_used = p->used;

    // Append at the tail so anything that pairs the joint's DOBJ list with a
    // parallel list (material animation) still lines up with the original run.
    for (tail = joint->dobj; tail != NULL && tail->next != NULL; tail = tail->next)
        ;
    if (tail == NULL)
        joint->dobj = d;
    else
        tail->next = d;

    stc_patch_num++;
    return p;
}

static void PutF32(u8 *dst, float v)
{
    memcpy(dst, &v, 4);
}

static void PutVtx(EnvPatch *p, u8 *dst, EnvVtx *v)
{
    int off = 0;

    PutF32(dst + off + 0, v->pos.X);
    PutF32(dst + off + 4, v->pos.Y);
    PutF32(dst + off + 8, v->pos.Z);
    off += 12;

    if (p->attrs & ENV_ATTR_NRM)
    {
        PutF32(dst + off + 0, v->nrm.X);
        PutF32(dst + off + 4, v->nrm.Y);
        PutF32(dst + off + 8, v->nrm.Z);
        off += 12;
    }

    if (p->attrs & ENV_ATTR_CLR0)
    {
        dst[off + 0] = (u8)(v->clr >> 24);
        dst[off + 1] = (u8)(v->clr >> 16);
        dst[off + 2] = (u8)(v->clr >> 8);
        dst[off + 3] = (u8)v->clr;
        off += 4;
    }

    if (p->attrs & ENV_ATTR_TEX0)
    {
        PutF32(dst + off + 0, v->s);
        PutF32(dst + off + 4, v->t);
    }
}

// Re-stamp the current chunk's vertex count and the POBJ's line count from
// `used`, which is the only thing that changes as triangles land or are rewound.
static void WriteCount(EnvPatch *p)
{
    int count = (p->used - 3) / p->stride;

    p->chunk[1] = (u8)(count >> 8);
    p->chunk[2] = (u8)count;
    p->pobj->n_display = (u16)((p->used + ENV_DL_ALIGN - 1) / ENV_DL_ALIGN);
}

int EnvPatch_AddTri(EnvPatch *p, EnvVtx *a, EnvVtx *b, EnvVtx *c)
{
    int need;

    if (p == NULL)
        return 0;
    need = p->stride * 3;

    // Leave a spare line so the tail of the list stays zeroed: the command
    // processor reads whole 32-byte lines and treats the zero bytes as NOPs.
    if (p->full)
        return 0;
    if (p->used + need + ENV_DL_ALIGN > ENV_CHUNK_SIZE)
    {
        p->pobj->n_display = (u16)((p->used + ENV_DL_ALIGN - 1) / ENV_DL_ALIGN);
        DCFlushRange(p->chunk, ENV_CHUNK_SIZE);
        if (!NewChunk(p))
        {
            p->full = 1;
            return 0;
        }
    }

    PutVtx(p, p->chunk + p->used, a);
    PutVtx(p, p->chunk + p->used + p->stride, b);
    PutVtx(p, p->chunk + p->used + p->stride * 2, c);
    p->used += need;

    WriteCount(p);
    p->dirty = 1;
    return 1;
}

int EnvPatch_CanFit(EnvPatch *p, int tris)
{
    int need, avail, chunks;

    if (p == NULL || p->full || p->chunk == NULL)
        return 0;

    need = tris * p->stride * 3;
    avail = ENV_CHUNK_SIZE - ENV_DL_ALIGN - p->used;
    if (avail < 0)
        avail = 0;
    if (need <= avail)
        return 1;

    // What the arena could still turn into chunks for this patch, each costing
    // its own POBJ and giving up its header and trailing NOP line.
    chunks = (ENV_ARENA_SIZE - stc_arena_used) /
             (ENV_CHUNK_SIZE + (int)sizeof(POBJ) + ENV_DL_ALIGN);
    if (chunks < 0)
        chunks = 0;
    return need <= avail + chunks * (ENV_CHUNK_SIZE - ENV_DL_ALIGN - 3);
}

void EnvPatch_Mark(void)
{
    int i;

    for (i = 0; i < stc_patch_num; i++)
    {
        stc_patch[i].mark_pobj = stc_patch[i].pobj;
        stc_patch[i].mark_used = stc_patch[i].used;
    }
}

void EnvPatch_Rollback(void)
{
    int i;

    for (i = 0; i < stc_patch_num; i++)
    {
        EnvPatch *p = &stc_patch[i];

        if (p->mark_pobj == NULL)
            continue;

        // Chunks taken since the mark stay spent - the arena is a bump allocator
        // and rollback only happens when it is already exhausted.
        p->pobj = p->mark_pobj;
        p->pobj->next = NULL;
        p->chunk = p->pobj->display;
        p->used = p->mark_used;

        // n_display rounds up, so the GP reads past the last vertex to the end of
        // the line. Those bytes have to read as NOPs again.
        memset(p->chunk + p->used, 0, ENV_CHUNK_SIZE - p->used);
        WriteCount(p);
        p->dirty = 1;
    }
}

void EnvPatch_Flush(void)
{
    int i;

    for (i = 0; i < stc_patch_num; i++)
    {
        EnvPatch *p = &stc_patch[i];
        if (!p->dirty)
            continue;
        DCFlushRange(p->chunk, ENV_CHUNK_SIZE);
        p->dirty = 0;
    }
}

EnvPatch *EnvPatch_ForDobj(DOBJ *d)
{
    int i;

    for (i = 0; i < stc_patch_num; i++)
        if (stc_patch[i].dobj == d)
            return &stc_patch[i];
    return NULL;
}

int EnvPatch_IsFlat(const EnvPatch *p)
{
    return p != NULL && p->flat_fill;
}

void EnvPatch_WalkLimit(const EnvPatch *p, POBJ **pobj_out, int *used_out)
{
    // Only the tail chunk can still receive triangles. A chunk that has been
    // closed already carries its final n_display and reads as NOPs past its data.
    *pobj_out = (p != NULL) ? p->pobj : NULL;
    *used_out = (p != NULL) ? p->used : 0;
}

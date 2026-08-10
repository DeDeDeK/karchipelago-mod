#include <string.h>

#include "os.h"
#include "hsd.h"
#include "game.h"
#include "scene.h"
#include "stage.h"
#include "code_patch/code_patch.h"

#include "coll_pool.h"

// Cutting a building face frees one collision slot and its surviving parts want
// most of a dozen, so the mod needs triangles of its own. It takes them from the
// stage's own arrays rather than allocating: grColl_Alloc sizes every array from
// GrObj.coll_max and grColl_Free releases each one by field, so a mod-owned
// buffer swapped in would leak the original and hand a foreign pointer to
// OSFreeToHeap. Raising coll_max before the allocation instead means the game
// allocates the room, owns it, and frees it.
//
// Reaching those triangles is the other half. Every query runs two passes - a
// brute-force walk of GrCollParam.moving_record, then the KD-tree walk. The tree
// is baked into the stage archive and can only ever emit the indices it was
// built with, so an appended triangle is invisible to it. The moving pass is
// not: it reads coll.moving_record / coll.moving_record_num fresh every time,
// AABB-tests each record, and only then scans that record's slice.
//
// The per-frame rebake that re-transforms moving geometry from its joint would
// destroy hand-written triangles, but it does not walk that array. It walks the
// terrain window at GrObj+0x9C (and each prop's own). Both windows share the
// same allocation as coll, but their counts are separate words - so a record
// past coll_terrain.moving_record_num is queried by everything and rebaked by
// nothing. That is the whole trick.
//
// Slots are a ring: a carve takes one group, and reaching the end of the ring
// recycles the oldest groups in the way. Recycling restores the stage triangles
// that group's carve retired, so an old crater turns back into solid wall rather
// than a hole with nothing standing in it.

#define ENV_POOL_TRIS      1024
#define ENV_POOL_GROUPS    32
#define ENV_POOL_PER_GROUP 64   // most one carve may place
#define ENV_POOL_CUTS      24   // most one carve may retire, all restorable
#define ENV_POOL_VTX       (ENV_POOL_TRIS * 3)

static GrCollRecord stc_group[ENV_POOL_GROUPS];
static int stc_first[ENV_POOL_GROUPS];   // ring offset of the group's triangles
static int stc_num[ENV_POOL_GROUPS];
static int stc_cut[ENV_POOL_GROUPS][ENV_POOL_CUTS];
static int stc_cut_num[ENV_POOL_GROUPS];
static u8 stc_holds[ENV_POOL_GROUPS];    // owns a ring range right now

static GrCollTri *stc_tri;
static GrCollVtx *stc_vtx;
static u8 stc_is_lining[ENV_POOL_TRIS];
static int stc_ring_at;
static int stc_group_at;
static int stc_mr_base;
static int stc_installed;

int EnvPool_Owns(const GrCollTri *t)
{
    return stc_installed && t >= stc_tri && t < stc_tri + ENV_POOL_TRIS;
}

int EnvPool_IsLining(const GrCollTri *t)
{
    if (!EnvPool_Owns(t))
        return 0;
    return stc_is_lining[t - stc_tri];
}

int EnvPool_Installed(void)
{
    return stc_installed;
}

int EnvPool_Room(void)
{
    if (!stc_installed)
        return 0;
    return ENV_POOL_PER_GROUP - stc_num[stc_group_at];
}

int EnvPool_CutRoom(void)
{
    if (!stc_installed)
        return 0;
    return ENV_POOL_CUTS - stc_cut_num[stc_group_at];
}

int EnvPool_Free(void)
{
    int i, n = ENV_POOL_TRIS;

    if (!stc_installed)
        return 0;
    for (i = 0; i < ENV_POOL_GROUPS; i++)
        n -= stc_num[i];
    return n;
}

void EnvPool_Reset(void)
{
    stc_installed = 0;
    stc_tri = NULL;
    stc_vtx = NULL;
    stc_ring_at = 0;
    stc_group_at = 0;
    stc_mr_base = 0;
    memset(stc_group, 0, sizeof(stc_group));
    memset(stc_first, 0, sizeof(stc_first));
    memset(stc_num, 0, sizeof(stc_num));
    memset(stc_cut_num, 0, sizeof(stc_cut_num));
    memset(stc_holds, 0, sizeof(stc_holds));
    memset(stc_is_lining, 0, sizeof(stc_is_lining));
}

static void Identity(Mtx m)
{
    memset(m, 0, sizeof(Mtx));
    m[0][0] = 1.0f;
    m[1][1] = 1.0f;
    m[2][2] = 1.0f;
}

// grColl_Alloc, immediately past its zone-count assert and just before the nine
// allocations, each of which reads its size back out of coll_max. r29 is
// &GrObj.coll_max there; the fill pass that follows leaves whatever it does not
// use as slack at the tail of every array, which is what the pool moves into.
void EnvPool_GrowCaps(GrCollParam *max)
{
    if (Scene_GetCurrentMajor() != MJRKIND_CITY)
        return;
    max->tri_num += ENV_POOL_TRIS;
    max->vtx_num += ENV_POOL_VTX;
    max->moving_record_num += ENV_POOL_GROUPS;
    OSReport("[EnvPool] Reserved %d triangles in the stage's collision arrays\n", ENV_POOL_TRIS);
}

CODEPATCH_HOOKCREATE(0x800d6f74,
                     "mr 3, 29\n\t",
                     EnvPool_GrowCaps,
                     "",
                     0)

void EnvPool_OnBoot(void)
{
    CODEPATCH_HOOKAPPLY(0x800d6f74);
}

// Hand a group's ring range back: the stage triangles its carve retired go solid
// again, and its own triangles stop answering queries.
static void RetireGroup(int i)
{
    GrObj *g = *stc_grobj;
    int k;

    if (!stc_holds[i])
        return;

    if (g != NULL && g->coll.tri != NULL)
        for (k = 0; k < stc_cut_num[i]; k++)
        {
            int at = stc_cut[i][k];
            if (at >= 0 && at < g->coll.tri_num)
                g->coll.tri[at].state |= (u8)GRCOLL_STATE_COLLIDABLE;
        }

    memset(stc_tri + stc_first[i], 0, stc_num[i] * sizeof(GrCollTri));
    stc_group[i].tri_num = 0;
    stc_num[i] = 0;
    stc_cut_num[i] = 0;
    stc_holds[i] = 0;
}

static void PrepareGroup(int i, int first)
{
    GrCollRecord *r = &stc_group[i];

    stc_first[i] = first;
    stc_num[i] = 0;
    stc_cut_num[i] = 0;
    stc_holds[i] = 1;

    r->tri_begin = stc_tri + first;
    r->tri_num = 0;
    r->aabb_center.X = r->aabb_center.Y = r->aabb_center.Z = 0.0f;
    r->aabb_half.X = r->aabb_half.Y = r->aabb_half.Z = 0.0f;
}

int EnvPool_Install(void)
{
    GrObj *g = *stc_grobj;
    int tri_room, vtx_room, mr_room, i;

    EnvPool_Reset();

    if (g == NULL || g->coll.tri == NULL || g->coll.vtx == NULL)
        return 0;

    tri_room = g->coll_max.tri_num - g->coll.tri_num;
    vtx_room = g->coll_max.vtx_num - g->coll.vtx_num;
    mr_room = g->coll_max.moving_record_num - g->coll.moving_record_num;
    if (tri_room < ENV_POOL_TRIS || vtx_room < ENV_POOL_VTX || mr_room < ENV_POOL_GROUPS)
    {
        OSReport("[EnvPool] Stage left no room (tri %d vtx %d rec %d); destruction off\n",
                 tri_room, vtx_room, mr_room);
        return 0;
    }

    // The pool takes the bottom of the slack and the live counts move up over
    // it, because PointCollision_EnsureIDValid (0x800d1838) rejects any triangle
    // id outside [0, coll.tri_num) and every ground, landing and shadow query
    // runs the id through it. A pool above the count would still stop a rider as
    // a wall but would never register as a surface to stand on. What the stage
    // left unused ends up above the pool instead, still there for a prop
    // attaching mid-match, which bump-allocates upward from coll.tri_num.
    stc_tri = g->coll.tri + g->coll.tri_num;
    stc_vtx = g->coll.vtx + g->coll.vtx_num;
    memset(stc_tri, 0, ENV_POOL_TRIS * sizeof(GrCollTri));
    memset(stc_vtx, 0, ENV_POOL_VTX * sizeof(GrCollVtx));
    g->coll.tri_num += ENV_POOL_TRIS;
    g->coll.vtx_num += ENV_POOL_VTX;

    stc_mr_base = g->coll.moving_record_num;
    for (i = 0; i < ENV_POOL_GROUPS; i++)
    {
        GrCollRecord *r = &stc_group[i];

        memset(r, 0, sizeof(*r));
        // The swept-sphere query brings the collider through prev_inv and then
        // world, so both have to be real transforms. desc_kind stays off 3 and
        // yaku_gobj stays NULL to keep out of the prop break dispatch.
        Identity(r->world);
        Identity(r->prev_inv);
        r->tri_begin = stc_tri;
        g->coll.moving_record[stc_mr_base + i] = r;
    }
    g->coll.moving_record_num = stc_mr_base + ENV_POOL_GROUPS;

    stc_installed = 1;
    PrepareGroup(0, 0);
    stc_ring_at = 0;

    OSReport("[EnvPool] %d collision triangles in %d records (tri %d, ids %d-%d)\n",
             ENV_POOL_TRIS, ENV_POOL_GROUPS, g->coll.tri_num,
             (int)(stc_tri - g->coll.tri), (int)(stc_tri - g->coll.tri) + ENV_POOL_TRIS - 1);
    return 1;
}

// A prop attaching mid-stage appends to the same list. Put the records back if
// anything has moved them, rather than assuming they stayed put.
void EnvPool_Ensure(void)
{
    GrObj *g = *stc_grobj;
    int i;

    if (!stc_installed || g == NULL)
        return;
    if (g->coll.moving_record_num >= stc_mr_base + ENV_POOL_GROUPS &&
        g->coll.moving_record[stc_mr_base] == &stc_group[0])
        return;
    if (stc_mr_base + ENV_POOL_GROUPS > g->coll_max.moving_record_num)
        return;

    for (i = 0; i < ENV_POOL_GROUPS; i++)
        g->coll.moving_record[stc_mr_base + i] = &stc_group[i];
    if (g->coll.moving_record_num < stc_mr_base + ENV_POOL_GROUPS)
        g->coll.moving_record_num = stc_mr_base + ENV_POOL_GROUPS;
}

void EnvPool_BeginGroup(void)
{
    int at, first, i;

    if (!stc_installed)
        return;

    // A carve that placed nothing does not burn a group.
    if (stc_holds[stc_group_at] && stc_num[stc_group_at] == 0)
        return;

    first = stc_ring_at;
    if (first + ENV_POOL_PER_GROUP > ENV_POOL_TRIS)
        first = 0;

    // Everything the new range runs over goes first, oldest craters included.
    for (i = 0; i < ENV_POOL_GROUPS; i++)
        if (stc_holds[i] && stc_first[i] < first + ENV_POOL_PER_GROUP &&
            first < stc_first[i] + stc_num[i])
            RetireGroup(i);

    at = (stc_group_at + 1) % ENV_POOL_GROUPS;
    RetireGroup(at);
    stc_group_at = at;
    PrepareGroup(at, first);
    stc_ring_at = first;
}

GrCollTri *EnvPool_Alloc(int lining, GrCollVtx **vtx_out)
{
    GrCollRecord *r;
    GrCollTri *t;
    GrCollVtx *v;
    int at, slot;

    if (!stc_installed)
        return NULL;

    at = stc_group_at;
    if (stc_num[at] >= ENV_POOL_PER_GROUP)
        return NULL;

    r = &stc_group[at];
    slot = stc_first[at] + stc_num[at];
    t = stc_tri + slot;
    v = stc_vtx + slot * 3;
    stc_num[at]++;
    stc_ring_at = slot + 1;
    stc_is_lining[slot] = (u8)(lining != 0);
    r->tri_num = stc_num[at];

    memset(t, 0, sizeof(*t));
    t->record = r;
    t->v0 = &v[0].pos;
    t->v1 = &v[1].pos;
    t->v2 = &v[2].pos;
    *vtx_out = v;
    return t;
}

void EnvPool_NoteCut(int index)
{
    int at = stc_group_at;

    if (!stc_installed || stc_cut_num[at] >= ENV_POOL_CUTS)
        return;
    stc_cut[at][stc_cut_num[at]++] = index;
}

void EnvPool_NoteBounds(Vec3 *center, Vec3 *half)
{
    GrCollRecord *r;
    float lo[3], hi[3], c[3], h[3];
    int i;

    if (!stc_installed)
        return;
    r = &stc_group[stc_group_at];

    c[0] = center->X; c[1] = center->Y; c[2] = center->Z;
    h[0] = half->X;   h[1] = half->Y;   h[2] = half->Z;

    if (stc_num[stc_group_at] <= 1)
    {
        r->aabb_center = *center;
        r->aabb_half = *half;
        return;
    }

    lo[0] = r->aabb_center.X - r->aabb_half.X;
    lo[1] = r->aabb_center.Y - r->aabb_half.Y;
    lo[2] = r->aabb_center.Z - r->aabb_half.Z;
    hi[0] = r->aabb_center.X + r->aabb_half.X;
    hi[1] = r->aabb_center.Y + r->aabb_half.Y;
    hi[2] = r->aabb_center.Z + r->aabb_half.Z;

    for (i = 0; i < 3; i++)
    {
        if (c[i] - h[i] < lo[i]) lo[i] = c[i] - h[i];
        if (c[i] + h[i] > hi[i]) hi[i] = c[i] + h[i];
    }

    r->aabb_center.X = (lo[0] + hi[0]) * 0.5f;
    r->aabb_center.Y = (lo[1] + hi[1]) * 0.5f;
    r->aabb_center.Z = (lo[2] + hi[2]) * 0.5f;
    r->aabb_half.X = (hi[0] - lo[0]) * 0.5f;
    r->aabb_half.Y = (hi[1] - lo[1]) * 0.5f;
    r->aabb_half.Z = (hi[2] - lo[2]) * 0.5f;
}

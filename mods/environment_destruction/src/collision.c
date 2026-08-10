#include <string.h>

#include "os.h"
#include "game.h"
#include "stage.h"
#include "collision.h"

#include "environment_destruction.h"
#include "carve.h"
#include "coll_pool.h"

// Map collision is one triangle array (GrObj.coll.tri / .tri_num) shared by baked
// terrain and every placed prop. City Trial's carries ~14000 triangles, of which
// ~5900 are carveable walls, and building faces are among the largest: cutting
// one frees a single slot while its surviving parts want most of a dozen.
//
// Replacements all come from the mod's own pool (coll_pool.c). A slot freed by
// cutting a stage triangle is not reusable in its place: the baked KD-tree only
// ever offers that slot where its original triangle stood, so a replacement put
// there is reachable from nowhere else and silently does nothing.
//
// A triangle is cut only when every remnant large enough to matter has somewhere
// to live. Retiring one without replacing it is what turns a doorway-sized hole
// into a wall a rider drives straight through.
#define ENV_COLL_PEND_MAX 384    // replacements queued by a single carve
#define ENV_COLL_CAND_MAX 48     // source triangles a single carve may cut

// Largest remnant that may be left without a slot. This has to be an absolute
// area, not a fraction of the triangle: a building face is one enormous triangle,
// and a tenth of it is a gap a machine drives straight through. Anything above
// this must be placed or the cut is refused outright.
#define ENV_COLL_SLIVER_AREA 24.0f

// How far off a wall triangle's own box the projected impact may land, squared.
#define ENV_HIT_ON_TRI2 16.0f

typedef struct PendTri
{
    Vec3 v[3];
    Vec3 normal;
    GrCollTri *tmpl;   // triangle it inherits classification from
    float rank;        // larger is placed first, 0 once spent
    int lining;        // hole surface: placed only after the wall is made whole
} PendTri;

// A source triangle the volume cuts, with the run of replacements it owes.
typedef struct CandTri
{
    int index;
    int first;
    int num;        // remnants queued
    int need;       // of those, the ones that must land for the cut to be allowed
    float dist2;
    int taken;      // decided: 1 accepted, -1 rejected
    int own;        // one of the mod's own: costs no restore slot
} CandTri;

static PendTri stc_pend[ENV_COLL_PEND_MAX];
static int stc_pend_num;
static CandTri stc_cand[ENV_COLL_CAND_MAX];
static int stc_cand_num;

int env_coll_stat_cut;
int env_coll_stat_skip;

void EnvColl_Reset(void)
{
    EnvPool_Reset();
    stc_pend_num = 0;
    stc_cand_num = 0;
}

static float Absf(float x)
{
    return x < 0.0f ? -x : x;
}

static float Dot(Vec3 *a, Vec3 *b)
{
    return a->X * b->X + a->Y * b->Y + a->Z * b->Z;
}

static void Sub(Vec3 *a, Vec3 *b, Vec3 *out)
{
    out->X = a->X - b->X;
    out->Y = a->Y - b->Y;
    out->Z = a->Z - b->Z;
}

static void Cross(Vec3 *a, Vec3 *b, Vec3 *out)
{
    out->X = a->Y * b->Z - a->Z * b->Y;
    out->Y = a->Z * b->X - a->X * b->Z;
    out->Z = a->X * b->Y - a->Y * b->X;
}

static GrCollParam *Coll(void)
{
    GrObj *g = *stc_grobj;

    if (g == NULL)
        return NULL;
    if (g->coll.tri == NULL || g->coll.tri_num <= 0)
        return NULL;
    return &g->coll;
}

// Squared distance from a point to the closest point on an AABB.
static float AabbDist2(Vec3 *p, Vec3 *center, Vec3 *half)
{
    float qx = Absf(p->X - center->X) - half->X;
    float qy = Absf(p->Y - center->Y) - half->Y;
    float qz = Absf(p->Z - center->Z) - half->Z;

    if (qx < 0.0f) qx = 0.0f;
    if (qy < 0.0f) qy = 0.0f;
    if (qz < 0.0f) qz = 0.0f;
    return qx * qx + qy * qy + qz * qz;
}

// A triangle is a candidate when it is live, static, and not one the collision
// builder gave an extruded prism to - rewriting either kind leaves derived data
// (the per-frame re-bake source, or the prism's own bounds) pointing at geometry
// that no longer exists.
//
// The mod's own replacements are candidates like any other. Cutting a wall
// retires the stage triangles it went through and stands the mod's in their
// place, so the surface at a carved spot is made of pool triangles from then on -
// sparing them would mean a second chunk taken there finds nothing left to
// remove, and a hole could never be widened or driven deeper.
static int Carveable(GrCollTri *t)
{
    if ((t->state & GRCOLL_STATE_COLLIDABLE) == 0)
        return 0;
    if (t->state & GRCOLL_STATE_DEGENERATE)
        return 0;
    if (t->flags & (GRCOLL_FLAG_MOVING | GRCOLL_FLAG_ROUGH))
        return 0;
    return 1;
}

int EnvColl_FindSurface(Vec3 *pos, float radius, Vec3 *hit_out, Vec3 *nrm_out)
{
    GrCollParam *gcp = Coll();
    float best = radius * radius;
    int i, found = 0;

    if (gcp == NULL)
        return 0;

    for (i = 0; i < gcp->tri_num; i++)
    {
        GrCollTri *t = &gcp->tri[i];
        Vec3 d, hp;
        float dist2, along;

        if (!Carveable(t))
            continue;
        if (EnvIsHorizontalSurface(t->normal.X, t->normal.Y, t->normal.Z))
            continue; // walls only, same guard the carve itself uses
        if (AabbDist2(pos, &t->aabb_center, &t->aabb_half) > best)
            continue;

        // Only walls facing the impact: standing behind a surface should not
        // punch a hole out of its front.
        Sub(pos, t->v0, &d);
        along = Dot(&t->normal, &d);
        if (along <= 0.0f)
            continue;

        dist2 = along * along;
        if (dist2 >= best)
            continue;

        hp.X = pos->X - t->normal.X * along;
        hp.Y = pos->Y - t->normal.Y * along;
        hp.Z = pos->Z - t->normal.Z * along;

        // The projection lands on the triangle's whole plane, which for a
        // building face runs far past the face itself. Requiring it to land on
        // the triangle's own box keeps the chunk where there is geometry to cut,
        // instead of out on an extension of the plane where the collision carve
        // finds nothing to do.
        if (AabbDist2(&hp, &t->aabb_center, &t->aabb_half) > ENV_HIT_ON_TRI2)
            continue;

        best = dist2;
        *hit_out = hp;
        *nrm_out = t->normal;
        found = 1;
    }

    return found;
}

int EnvColl_WallAt(int tri_id, Vec3 *toward, Vec3 *nrm_out)
{
    GrCollParam *gcp = Coll();
    GrCollTri *t;

    // Nothing on the engine's contact path range checks a triangle id, so the
    // one it hands back is checked here before it indexes the array.
    if (gcp == NULL || tri_id < 0 || tri_id >= gcp->tri_num)
        return 0;

    t = &gcp->tri[tri_id];
    if (!Carveable(t))
        return 0;
    if (EnvIsHorizontalSurface(t->normal.X, t->normal.Y, t->normal.Z))
        return 0;

    *nrm_out = t->normal;
    if (Dot(nrm_out, toward) < 0.0f)
    {
        nrm_out->X = -nrm_out->X;
        nrm_out->Y = -nrm_out->Y;
        nrm_out->Z = -nrm_out->Z;
    }
    return 1;
}

int EnvColl_RayHit(Vec3 *from, Vec3 *dir, float len, Vec3 *hit_out, Vec3 *nrm_out)
{
    GrCollParam *gcp = Coll();
    Vec3 end, hp;
    GrCollTri *t;
    int id;

    if (gcp == NULL)
        return 0;

    end.X = from->X + dir->X * len;
    end.Y = from->Y + dir->Y * len;
    end.Z = from->Z + dir->Z * len;

    // Raycast_Any, not Raycast_Ground: the wrappers differ only in the kind mask
    // they hand grColl_RayVsTri, and a mask that names only GrCFK_Under cannot
    // return a wall at all, which is every surface a probe is fired to look for.
    // Mask 7 also means anything the mod may not cut still stops the probe rather
    // than being seen through - carving past geometry that is staying put would
    // open a hole with a wall still standing in front of it.
    //
    // grColl_RayVsTri gates on the same state bits the sweep does, so a triangle
    // an earlier carve retired is not returned and the probe sees straight
    // through the hole to whatever now stands at its surface.
    id = Raycast_Any(from, &end, &hp);
    if (id < 0 || id >= gcp->tri_num)
        return 0;

    // Whatever the ray stops on is the surface in front of the source, and that
    // is what gets cut. Carving past it would open a hole with a wall still
    // standing in front of it, so anything the mod may not cut blocks the probe
    // outright rather than being seen through.
    t = &gcp->tri[id];
    if (!Carveable(t))
        return 0;
    if (EnvIsHorizontalSurface(t->normal.X, t->normal.Y, t->normal.Z))
        return 0;

    *hit_out = hp;
    *nrm_out = t->normal;
    // Struck from behind, as when cutting a building's far wall from inside it.
    // The chunk is driven the way the probe was travelling either way.
    if (Dot(dir, nrm_out) > 0.0f)
    {
        nrm_out->X = -nrm_out->X;
        nrm_out->Y = -nrm_out->Y;
        nrm_out->Z = -nrm_out->Z;
    }
    return 1;
}

// Surface crossings a break-through test will follow before giving up.
#define ENV_BORE_STEPS 8
// Nudge past a hit so the next segment does not stop on the same triangle.
#define ENV_BORE_EPS 0.1f

int EnvColl_BoreExits(const EnvVolume *vol)
{
    GrCollParam *gcp = Coll();
    const Vec3 *axis = EnvVolume_Axis(vol);
    Vec3 from, end, dir, hp;
    int i, last = 0;

    if (gcp == NULL)
        return 1;

    dir.X = -axis->X;
    dir.Y = -axis->Y;
    dir.Z = -axis->Z;
    // Start just outside the surface so the face about to be cut counts as the
    // first crossing, and stop at the back face.
    from.X = vol->center.X + axis->X * ENV_BORE_EPS;
    from.Y = vol->center.Y + axis->Y * ENV_BORE_EPS;
    from.Z = vol->center.Z + axis->Z * ENV_BORE_EPS;
    end = vol->apex;

    for (i = 0; i < ENV_BORE_STEPS; i++)
    {
        Vec3 step;
        int id = Raycast_Any(&from, &end, &hp);

        if (id < 0 || id >= gcp->tri_num)
            break;

        // A face turned back toward the mouth is one the bore goes into; one
        // turned the way the bore travels is one it comes out of. A lining face
        // is neither - it stands in a hole the mod already took out, and reading
        // it as material is what lets the back face of one chunk pass for the
        // wall the next one is buried in. A remnant still counts: it lies in the
        // plane of the wall it came out of, so crossing it is crossing that wall.
        if (!EnvPool_IsLining(&gcp->tri[id]))
            last = (Dot(&gcp->tri[id].normal, &dir) < 0.0f) ? 1 : -1;

        from.X = hp.X + dir.X * ENV_BORE_EPS;
        from.Y = hp.Y + dir.Y * ENV_BORE_EPS;
        from.Z = hp.Z + dir.Z * ENV_BORE_EPS;
        Sub(&end, &from, &step);
        if (Dot(&step, &dir) <= 0.0f)
            break; // reached the back face
    }

    // Crossing nothing at all is a bore that reached its back face without
    // meeting one stage surface, which is not a bore buried in material: City
    // Trial's walls are mostly single planes with open space behind them - a
    // third of them have no collision of any kind within sixty units - so the
    // very first bite comes out the far side and there is nothing there to hold
    // a back face up.
    return last <= 0;
}

// Whether the volume reaches any material - anything that is not a face the mod
// invented to line a hole. A wall cut once is made of remnants from then on, and
// those are the wall: skipping them here is what would let a building be carved
// exactly once and then refuse every bite after it.
//
// The test is the one the collision carve itself applies, so a volume that
// passes here has something to cut when it gets there.
int EnvColl_HasStageMaterial(const EnvVolume *vol)
{
    GrCollParam *gcp = Coll();
    float reject2 = vol->radius * vol->radius;
    int i;

    if (gcp == NULL)
        return 0;

    for (i = 0; i < gcp->tri_num; i++)
    {
        static EnvPoly kept[ENV_KEPT_MAX], removed;
        GrCollTri *t = &gcp->tri[i];
        EnvVtx tri[3];

        if (!Carveable(t))
            continue;
        if (AabbDist2((Vec3 *)&vol->center, &t->aabb_center, &t->aabb_half) > reject2)
            continue;
        if (EnvPool_IsLining(t))
            continue;
        if (EnvIsHorizontalSurface(t->normal.X, t->normal.Y, t->normal.Z))
            continue;

        memset(tri, 0, sizeof(tri));
        tri[0].pos = *t->v0;
        tri[1].pos = *t->v1;
        tri[2].pos = *t->v2;

        EnvVolume_CarveTri(vol, tri, kept, &removed);
        if (removed.n >= 3)
            return 1;
    }

    return 0;
}

static int PendAdd(Vec3 *a, Vec3 *b, Vec3 *c, Vec3 *normal, GrCollTri *tmpl,
                   float rank, int lining)
{
    PendTri *p;

    if (stc_pend_num >= ENV_COLL_PEND_MAX)
        return 0;
    p = &stc_pend[stc_pend_num++];
    p->v[0] = *a;
    p->v[1] = *b;
    p->v[2] = *c;
    p->normal = *normal;
    p->tmpl = tmpl;
    p->rank = rank;
    p->lining = lining;
    return 1;
}

// Rank by area so that when slots run out it is the slivers that get dropped.
static float TriArea(Vec3 *a, Vec3 *b, Vec3 *c)
{
    Vec3 u, w, n;

    Sub(b, a, &u);
    Sub(c, a, &w);
    Cross(&u, &w, &n);
    return sqrtf(Dot(&n, &n)) * 0.5f;
}

// Order one candidate's remnants largest first and return how many are too big to
// leave out. Sorted descending, those are exactly a prefix, and placement is also
// largest-first globally - so the count returned here is the count that lands.
static int RequiredCount(int first, int num)
{
    int i, j, need = 0;

    for (i = 1; i < num; i++)
    {
        PendTri t = stc_pend[first + i];
        for (j = i; j > 0 && stc_pend[first + j - 1].rank < t.rank; j--)
            stc_pend[first + j] = stc_pend[first + j - 1];
        stc_pend[first + j] = t;
    }

    for (i = 0; i < num; i++)
        if (stc_pend[first + i].rank >= ENV_COLL_SLIVER_AREA)
            need = i + 1;
    return need;
}

// The category bits a surface with this normal belongs in, keeping everything
// above them - the ground type above all - from the wall the chunk was taken out
// of. Uses the same threshold as EnvIsHorizontalSurface, so a face the carve
// treats as floor is one the engine's ground queries will also find.
static u32 EnvCategoryForNormal(u32 base, Vec3 *n)
{
    u32 cat = GRCOLL_KIND_WALL;
    float len2 = n->X * n->X + n->Y * n->Y + n->Z * n->Z;

    if (len2 > 0.0f && (n->Y * n->Y) >= ENV_FLOOR_COS2 * len2)
        cat = (n->Y > 0.0f) ? GRCOLL_KIND_UNDER : GRCOLL_KIND_TOP;
    return (base & ~(u32)GRCOLL_KIND_CATEGORY) | cat;
}

// Write one replacement triangle into `slot`, using `v` for its three vertices.
// prev mirrors pos: nothing re-bakes these, so they must always report as having
// stood still.
static void FillTri(GrCollTri *slot, GrCollVtx *v, PendTri *p)
{
    Vec3 lo, hi;
    int i;

    for (i = 0; i < 3; i++)
    {
        v[i].pos = p->v[i];
        v[i].prev = p->v[i];
    }

    slot->v0 = &v[0].pos;
    slot->v1 = &v[1].pos;
    slot->v2 = &v[2].pos;
    slot->normal = p->normal;

    lo = p->v[0];
    hi = p->v[0];
    for (i = 1; i < 3; i++)
    {
        if (p->v[i].X < lo.X) lo.X = p->v[i].X;
        if (p->v[i].Y < lo.Y) lo.Y = p->v[i].Y;
        if (p->v[i].Z < lo.Z) lo.Z = p->v[i].Z;
        if (p->v[i].X > hi.X) hi.X = p->v[i].X;
        if (p->v[i].Y > hi.Y) hi.Y = p->v[i].Y;
        if (p->v[i].Z > hi.Z) hi.Z = p->v[i].Z;
    }
    slot->aabb_center.X = (lo.X + hi.X) * 0.5f;
    slot->aabb_center.Y = (lo.Y + hi.Y) * 0.5f;
    slot->aabb_center.Z = (lo.Z + hi.Z) * 0.5f;
    slot->aabb_half.X = (hi.X - lo.X) * 0.5f;
    slot->aabb_half.Y = (hi.Y - lo.Y) * 0.5f;
    slot->aabb_half.Z = (hi.Z - lo.Z) * 0.5f;

    // Classification is inherited whole from the surface being replaced, so a
    // replacement is indistinguishable from the wall it stands in for. Only the
    // bits that would claim membership of another array are dropped: the
    // rough-prism entries and the per-frame moving rebake both hold their own
    // lists, and a triangle in neither must not advertise itself as being in one.
    //
    // A remnant lies in the plane of the triangle it came out of and keeps its
    // category. A lining face does not: it is a surface the carve invented, and
    // the wall it borrows its ground type from faces a different way. Leaving it
    // the wall's category is what would make the floor of the hole a wall - a
    // surface every ground query filters out, so nothing would stand on it.
    slot->kind = p->lining ? EnvCategoryForNormal(p->tmpl->kind, &p->normal)
                           : p->tmpl->kind;
    slot->flags = p->tmpl->flags & ~(u32)(GRCOLL_FLAG_ROUGH | GRCOLL_FLAG_MOVING);
    slot->state = (u8)((p->tmpl->state | GRCOLL_STATE_COLLIDABLE) & ~GRCOLL_STATE_DEGENERATE);
}

static int WritePoolTri(PendTri *p)
{
    GrCollVtx *v;
    GrCollTri *slot = EnvPool_Alloc(p->lining, &v);

    if (slot == NULL)
        return 0;
    FillTri(slot, v, p);
    EnvPool_NoteBounds(&slot->aabb_center, &slot->aabb_half);
    return 1;
}

// The surface of the hole. Queued last and placed last, because a hole with no
// inner surface still plays - one whose wall lost its collision does not. Ranked
// so that the floor survives a short placement: a bore whose sides are missing is
// still drivable, one with no floor is a pit.
//
// The normals come off the volume's own planes and are already unit length,
// which the narrowphase requires - it uses GrCollTri.normal as the plane's.
static void QueueLining(const EnvLinTri *lin, int n, GrCollTri *tmpl)
{
    int i;

    for (i = 0; i < n; i++)
    {
        u32 cat = EnvCategoryForNormal(0, (Vec3 *)&lin[i].n) & GRCOLL_KIND_CATEGORY;
        float rank = (cat == GRCOLL_KIND_UNDER) ? 3.0f : (cat == GRCOLL_KIND_WALL ? 2.0f : 1.0f);

        PendAdd((Vec3 *)&lin[i].v[0], (Vec3 *)&lin[i].v[1], (Vec3 *)&lin[i].v[2],
                (Vec3 *)&lin[i].n, tmpl, rank, 1);
    }
}

// Clip every carveable triangle the volume reaches and queue what each one owes.
// Nothing is retired here: a cut is only worth making if its remnants can be put
// back, and that is not known until every candidate has been priced.
static void GatherCandidates(GrCollParam *gcp, const EnvVolume *vol,
                             GrCollTri **hit_tmpl)
{
    float reject2 = vol->radius * vol->radius;
    float best_tmpl = reject2;
    int i;

    for (i = 0; i < gcp->tri_num && stc_cand_num < ENV_COLL_CAND_MAX; i++)
    {
        // Static: the clip workspace runs to several kilobytes and this loop is
        // entered thousands of times. Only one carve runs at a time.
        static EnvPoly kept[ENV_KEPT_MAX], removed;
        GrCollTri *t = &gcp->tri[i];
        EnvVtx tri[3];
        CandTri *c;
        int n_kept, k, j, overflow = 0;
        float d2;

        if (!Carveable(t))
            continue;
        d2 = AabbDist2((Vec3 *)&vol->center, &t->aabb_center, &t->aabb_half);
        if (d2 > reject2)
            continue;
        if (EnvIsHorizontalSurface(t->normal.X, t->normal.Y, t->normal.Z))
            continue; // keep the ground the rider is standing on solid

        memset(tri, 0, sizeof(tri));
        tri[0].pos = *t->v0;
        tri[1].pos = *t->v1;
        tri[2].pos = *t->v2;

        n_kept = EnvVolume_CarveTri(vol, tri, kept, &removed);
        if (removed.n < 3)
            continue;

        // The wall closest to the impact lends its ground type to the lining,
        // which has no surface of its own to inherit from.
        if (*hit_tmpl == NULL || d2 < best_tmpl)
        {
            *hit_tmpl = t;
            best_tmpl = d2;
        }

        c = &stc_cand[stc_cand_num++];
        c->index = i;
        c->first = stc_pend_num;
        c->dist2 = d2;
        c->taken = 0;
        c->own = EnvPool_Owns(t);

        for (k = 0; k < n_kept && !overflow; k++)
            for (j = 1; j + 1 < kept[k].n; j++)
                if (!PendAdd(&kept[k].v[0].pos, &kept[k].v[j].pos, &kept[k].v[j + 1].pos,
                             &t->normal, t,
                             TriArea(&kept[k].v[0].pos, &kept[k].v[j].pos, &kept[k].v[j + 1].pos),
                             0))
                {
                    overflow = 1;
                    break;
                }

        // A remnant with nowhere to be queued would go uncounted, and the cut
        // would then be priced as owing less than it does - which is how a wall
        // loses its collision without getting it back. Drop the candidate
        // instead: it keeps its triangle, and the chunk is a hole there that
        // nobody can drive into.
        if (overflow)
        {
            stc_pend_num = c->first;
            stc_cand_num--;
            continue;
        }

        c->num = stc_pend_num - c->first;
        c->need = RequiredCount(c->first, c->num);
    }
}

// Decide which cuts this carve's slot budget can afford, nearest the impact
// first so the hole forms around where it was struck rather than at its edge.
// `avail` has already had the crater faces taken out of it. A stage cut costs
// one of the group's restore slots whether or not it owes any remnants; one of
// the mod's own costs none, because its group hands it back wholesale when the
// ring comes round to it.
static void ChooseCandidates(int avail, int cut_room)
{
    int i;

    // A triangle the chunk swallows outright owes nothing. These are the middle
    // of the hole, so they are taken before anything is priced.
    for (i = 0; i < stc_cand_num; i++)
        if (stc_cand[i].num == 0 && (stc_cand[i].own || cut_room > 0))
        {
            stc_cand[i].taken = 1;
            cut_room -= stc_cand[i].own ? 0 : 1;
        }

    for (;;)
    {
        int best = -1, cost;

        for (i = 0; i < stc_cand_num; i++)
            if (stc_cand[i].taken == 0 &&
                (best < 0 || stc_cand[i].dist2 < stc_cand[best].dist2))
                best = i;
        if (best < 0)
            break;

        cost = stc_cand[best].own ? 0 : 1;
        if (cut_room >= cost && avail >= stc_cand[best].need)
        {
            stc_cand[best].taken = 1;
            avail -= stc_cand[best].need;
            cut_room -= cost;
        }
        else
        {
            stc_cand[best].taken = -1;
        }
    }
}

// Place every queued replacement of the requested kind, highest ranked first.
// Remnants run before the lining so that if placement does run short it is the
// inside of the hole that is missing, not the wall around it.
// `reserve` is slots this pass must leave behind. Pricing the cuts against a
// budget that already had the lining taken out of it is not enough on its own:
// remnants too small to have been required are queued all the same, and without
// a floor here they would fill the group and leave the hole with no inside.
static int PlacePass(int lining, int reserve)
{
    int placed = 0;

    for (;;)
    {
        PendTri *p;
        int best = -1, i;

        if (EnvPool_Room() <= reserve)
            break;

        for (i = 0; i < stc_pend_num; i++)
            if (stc_pend[i].lining == lining && stc_pend[i].rank > 0.0f &&
                (best < 0 || stc_pend[i].rank > stc_pend[best].rank))
                best = i;
        if (best < 0)
            break;

        p = &stc_pend[best];
        p->rank = 0.0f;

        if (!WritePoolTri(p))
            break;
        placed++;
    }

    return placed;
}

int EnvColl_CarveVolume(const EnvVolume *vol, int with_back)
{
    static EnvLinTri lin[ENV_LINING_MAX];
    GrCollParam *gcp = Coll();
    GrCollTri *hit_tmpl = NULL;
    int i, cuts = 0, skipped = 0, placed, avail, n_lin;

    if (gcp == NULL || !EnvPool_Installed())
        return 0;

    stc_pend_num = 0;
    stc_cand_num = 0;
    env_coll_stat_cut = 0;
    env_coll_stat_skip = 0;

    EnvPool_Ensure();
    EnvPool_BeginGroup();

    GatherCandidates(gcp, vol, &hit_tmpl);
    if (hit_tmpl == NULL)
        return 0;

    // The lining is not optional - a hole with no inner surface is a hole a rider
    // falls into - so its slots come off the budget before any cut is priced
    // against what is left.
    n_lin = EnvVolume_Lining(vol, with_back, lin);
    avail = EnvPool_Room() - n_lin;
    ChooseCandidates(avail, EnvPool_CutRoom());

    for (i = 0; i < stc_cand_num; i++)
    {
        if (stc_cand[i].taken != 1)
        {
            // Not affordable: the wall keeps its collision and the chunk is a
            // hole the rider can see but not drive into.
            int j;
            for (j = stc_cand[i].first; j < stc_cand[i].first + stc_cand[i].num; j++)
                stc_pend[j].rank = 0.0f;
            skipped++;
            continue;
        }
        gcp->tri[stc_cand[i].index].state &= (u8)~GRCOLL_STATE_COLLIDABLE;
        // Only stage triangles are banked for restore. Handing one of the mod's
        // own back would set the live bit on a slot its group has since zeroed,
        // and the narrowphase reads that triangle's vertex pointers.
        if (!stc_cand[i].own)
            EnvPool_NoteCut(stc_cand[i].index);
        cuts++;
    }

    env_coll_stat_cut = cuts;
    env_coll_stat_skip = skipped;
    if (cuts == 0)
        return 0;

    QueueLining(lin, n_lin, hit_tmpl);

    placed = PlacePass(0, n_lin);
    placed += PlacePass(1, 0);
    return placed;
}

#include <string.h>

#include "os.h"
#include "game.h"
#include "obj.h"
#include "rider.h"
#include "machine.h"
#include "item.h"
#include "yakumono.h"
#include "collision.h"

#include "hypernova.h"

#define ITEM_GOBJ_KIND 22  // gobj->entity_class for a City Trial item

// True if `target` is inside RANGE and within the half-angle of unit `aim_unit`.
static int Hypernova_InCone(Vec3 *origin, Vec3 *aim_unit, Vec3 *target)
{
    Vec3 d;
    VECSubtract(target, origin, &d);
    float dist2 = VECSquareMag(&d);
    if (dist2 > HYPERNOVA_RANGE * HYPERNOVA_RANGE)
        return 0;
    if (dist2 < 0.0001f)
        return 1; // essentially on top of the rider

    float proj = VECDotProduct(&d, aim_unit); // |d| * cos(angle)
    if (proj < 0.0f)
        return 0; // behind the rider
    return proj * proj >= (HYPERNOVA_HALF_ANGLE_COS * HYPERNOVA_HALF_ANGLE_COS) * dist2;
}

// Shared pull step for items, props and machines: advance `p` by max(SPEED*dist, MIN) toward a
// point lifted above the rider by a parabolic hump of horizontal distance, so the target arcs up
// and over with the lift vanishing at the rider.
static void Hypernova_StepToward(Vec3 *p, Vec3 *rider)
{
    Vec3 to = *rider;
    float dx = rider->X - p->X;
    float dz = rider->Z - p->Z;
    float hdist2 = dx * dx + dz * dz;
    float u = hdist2 / (HYPERNOVA_RANGE * HYPERNOVA_RANGE);
    if (u > 1.0f)
        u = 1.0f;
    to.Y += HYPERNOVA_ARC_HEIGHT * 4.0f * u * (1.0f - u);

    Vec3 gap;
    VECSubtract(&to, p, &gap);
    float dist2 = VECSquareMag(&gap);
    if (dist2 < 1.0e-6f)
    {
        *p = to; // arrived; snap to avoid a divide-by-zero normalize
        return;
    }

    Vec3 step;
    float speed = HYPERNOVA_PULL_SPEED;
    if (speed * speed * dist2 >= HYPERNOVA_PULL_MIN * HYPERNOVA_PULL_MIN)
    {
        VECScale(&gap, &step, speed);
    }
    else
    {
        Vec3 dir;
        VECNormalize(&gap, &dir);
        VECScale(&dir, &step, HYPERNOVA_PULL_MIN);
    }
    p->X += step.X;
    p->Y += step.Y;
    p->Z += step.Z;
}

// Spin direction hashed from the target pointer: stable per-target, and xor-folded so heap
// alignment can't bias it.
static float Hypernova_SpinSign(void *key)
{
    u32 h = (u32)key;
    h ^= h >> 7;
    h ^= h >> 13;
    return (h & 1) ? 1.0f : -1.0f;
}

// Spin axis perpendicular to travel (rider - pos) and horizontal, so the target pitches
// end-over-end. Must be unit - Vec3_RotateAboutUnitAxis assumes it. Returns 0 if none usable.
static int Hypernova_SpinAxis(Vec3 *pos, Vec3 *rider, Vec3 *out)
{
    Vec3 dir;
    VECSubtract(rider, pos, &dir);
    if (VECSquareMag(&dir) < 1.0e-6f)
        return 0;

    Vec3 up;
    up.X = 0.0f; up.Y = 1.0f; up.Z = 0.0f;
    Vec3 cross;
    VECCrossProduct(&dir, &up, &cross);
    if (VECSquareMag(&cross) < 1.0e-6f)
    {
        cross.X = 1.0f; cross.Y = 0.0f; cross.Z = 0.0f; // travel near-vertical
    }
    VECNormalize(&cross, out);
    return 1;
}

// CityItem_UpdatePosition rebuilds the render matrix from forward/up, so rotate both by the
// same rotation to keep them orthonormal.
static void Hypernova_SpinItem(ItemData *id, Vec3 *rider)
{
    Vec3 axis;
    if (!Hypernova_SpinAxis(&id->pos, rider, &axis))
        return;
    float ang = HYPERNOVA_SPIN_RATE * Hypernova_SpinSign(id);
    Vec3_RotateAboutUnitAxis(&id->forward, &axis, ang);
    Vec3_RotateAboutUnitAxis(&id->up, &axis, ang);
}

// Rotates the three basis columns of a 3x4 row-major world matrix, leaving translation alone.
// Length-preserving, so it doesn't perturb the shrink.
static void Hypernova_SpinMtx3x3(float *mtx, Vec3 *axis, float ang)
{
    Vec3 c;
    c.X = mtx[0]; c.Y = mtx[4]; c.Z = mtx[8];
    Vec3_RotateAboutUnitAxis(&c, axis, ang);
    mtx[0] = c.X; mtx[4] = c.Y; mtx[8] = c.Z;

    c.X = mtx[1]; c.Y = mtx[5]; c.Z = mtx[9];
    Vec3_RotateAboutUnitAxis(&c, axis, ang);
    mtx[1] = c.X; mtx[5] = c.Y; mtx[9] = c.Z;

    c.X = mtx[2]; c.Y = mtx[6]; c.Z = mtx[10];
    Vec3_RotateAboutUnitAxis(&c, axis, ang);
    mtx[2] = c.X; mtx[6] = c.Y; mtx[10] = c.Z;
}

static void Hypernova_PullItem(RiderData *rd, ItemData *id)
{
    Hypernova_StepToward(&id->pos, &rd->pos);
    Hypernova_SpinItem(id, &rd->pos);

    // Stop physics + ground-snap from fighting the position override.
    id->vel.X = 0.0f;
    id->vel.Y = 0.0f;
    id->vel.Z = 0.0f;
    id->is_airborne = -1; // skip the per-frame ground raycast
    id->x35a &= ~0x10;    // clear grounded flag (bit 4)
}

// Breakable City Trial props by desc_id (YakumonoData+0x04): 29 star pole, 32 forest pitfall,
// 33 coral, 34 trees, 35 rocks, 36 volcano rock walls, 37 volcano-base holes, 38 houses.
static int Hypernova_IsBreakableYaku(int desc)
{
    switch (desc)
    {
    case 29: case 32: case 33: case 34:
    case 35: case 36: case 37: case 38:
        return 1;
    default:
        return 0;
    }
}

// Write the translation column of a 3x4 row-major world matrix (float indices 3,7,11).
static void Hypernova_SetMtxTranslation(float *mtx, Vec3 *t)
{
    mtx[3]  = t->X;
    mtx[7]  = t->Y;
    mtx[11] = t->Z;
}

// Scale the 3x3 rotation/scale block of a 3x4 row-major matrix in place (translation untouched).
static void Hypernova_ScaleMtx3x3(float *mtx, float s)
{
    mtx[0] *= s; mtx[1] *= s; mtx[2]  *= s;
    mtx[4] *= s; mtx[5] *= s; mtx[6]  *= s;
    mtx[8] *= s; mtx[9] *= s; mtx[10] *= s;
}

// Squared magnitude of row 0 - a sqrt-free proxy for the matrix's uniform scale.
static float Hypernova_Mtx3x3Row0Mag2(float *mtx)
{
    return mtx[0] * mtx[0] + mtx[1] * mtx[1] + mtx[2] * mtx[2];
}

// Global index of a record's first region within Yaku_GetRegionArray() - what
// collideWithObject takes as regionIdx. -1 if the regions aren't a clean strided slice.
static int Hypernova_RecordRegionIndex(void *record)
{
    YakuCollRegion *base    = Yaku_GetRegionArray();
    YakuCollRegion *regions = Yaku_InstanceRegions(record);
    if (base == NULL || regions < base)
        return -1;
    u32 off = (u32)((char *)regions - (char *)base);
    if ((off % YAKU_REGION_SIZE) != 0)
        return -1;
    return (int)(off / YAKU_REGION_SIZE);
}

// True if this prop's family breaks through hitWeakObject (coral 33 / trees 34 / rocks 35),
// which neither hides the dragged intact mesh nor moves its debris off the prop's baked spot.
static int Hypernova_IsWeakBreakFamily(GOBJ *yaku_gobj)
{
    YakumonoData *yd = Yaku_GetData(yaku_gobj);
    if (yd == NULL)
        return 0;
    return Yaku_GetDescCollFunc(yd->desc_id) == (void *)hitWeakObject;
}

// Resolve the weak break's debris-anchor JObj, mirroring hitWeakObject's own lookup: family
// break data (yd->data_ptr) -> per-instance entry table (stride 0x10) -> node id at entry+0x08
// -> grobj node registry. NULL if any link can't be resolved.
static void *Hypernova_WeakDebrisNode(GOBJ *yaku_gobj, void *record)
{
    YakumonoData *yd = Yaku_GetData(yaku_gobj);
    if (yd == NULL)
        return NULL;
    void *bc = yd->data_ptr;                    // family break-coll data
    if (bc == NULL)
        return NULL;
    void *desc = *(void **)bc;                  // -> {entry table, instance count}
    if (desc == NULL)
        return NULL;
    char *entry_base = *(char **)desc;          // *desc -> per-instance entry table
    int   count = *(int *)((char *)desc + 4);   // desc[1] = instance count
    void **rec_arr = (void **)yd->region_audio_arr; // family's per-prop record array
    if (entry_base == NULL || rec_arr == NULL || count <= 0)
        return NULL;

    int inst = -1;
    for (int k = 0; k < count; k++)
    {
        if (rec_arr[k] == record)
        {
            inst = k;
            break;
        }
    }
    if (inst < 0)
        return NULL;

    int node_id = *(int *)(entry_base + inst * 0x10 + 0x08);
    void *node = Yaku_GetSceneNodeJObj(node_id);
    if (node == NULL)
        return NULL;

    // The node's matrix gets written, so reject a garbage entry: require a 4-aligned MEM1 ptr.
    u32 a = (u32)node;
    if (a < 0x80000000u || a >= 0x81800000u || (a & 3) != 0)
        return NULL;
    return node;
}

// Break a drawn-in prop by handing collideWithObject a fabricated high-force collider, so it
// breaks in one hit through the genuine family path (retire, hide/debris, item drops, SFX,
// break-count credit). Returns 1 if the break fired.
static int Hypernova_BreakInstanceNative(GOBJ *rider_gobj, void *record)
{
    void *holder = Yaku_GetCollHolder();
    if (holder == NULL)
        return 0;

    GOBJ *yaku_gobj = Yaku_InstanceParent(record);
    if (yaku_gobj == NULL)
        return 0;

    int base_idx = Hypernova_RecordRegionIndex(record);
    if (base_idx < 0)
        return 0;

    YakuCollRegion *regions = Yaku_GetRegionArray();
    int region_count = Yaku_InstanceRegionCount(record);
    if (region_count <= 0)
        region_count = 1;

    // The impact-speed calc projects the delta onto the region's outward normal, so a real
    // normal is needed to aim the delta against.
    int region_idx = -1;
    Vec3 n_unit;
    n_unit.X = 0.0f; n_unit.Y = 0.0f; n_unit.Z = 0.0f;
    for (int k = 0; k < region_count; k++)
    {
        Vec3 n = regions[base_idx + k].normal;
        if (VECSquareMag(&n) > 1.0e-6f)
        {
            VECNormalize(&n, &n_unit);
            region_idx = base_idx + k;
            break;
        }
    }
    if (region_idx < 0)
        return 0; // degenerate prop (no usable normal)

    // Re-arm the collision retired for the flight so the family coll_func's "still collidable?"
    // guard passes; the tail retires it (and every region) again on a successful break.
    grScene_SetInstanceColl(record, 1);

    // mpCollInfo+0x1d0 = -1 marks "no BigStar region", so destroyBigStar returns 0 and the
    // break proceeds.
    u8 coll_info[0x200];
    CollData coll;
    memset(coll_info, 0, sizeof(coll_info));
    memset(&coll, 0, sizeof(coll));
    *(int *)(coll_info + 0x1d0) = -1;
    coll.g         = rider_gobj;               // break credited to this rider
    coll.coll_info = (mpCollInfo *)coll_info;
    coll.radius    = HYPERNOVA_BREAK_FORCE_RADIUS;

    // The delta must point INTO the surface: the engine negates the normalized delta before
    // projecting it onto the outward normal, and clamps a non-positive result to zero impact.
    VECScale(&n_unit, &coll.pos_delta, -HYPERNOVA_BREAK_FORCE_DELTA);

    // Skip the geometry-refined impact path; it can rewrite the delta from the prop's matrices.
    YakuCollRegion *region = &regions[region_idx];
    u32 saved = region->refine_flags;
    region->refine_flags = saved & ~(u32)YAKU_REGION_REFINE;

    // The prop's current (pulled-in) world position.
    Vec3 contact;
    Yaku_InstanceCachedPos(record, &contact);

    // Weak families pin break debris to a separate grobj node at the prop's baked spot, so it is
    // relocated onto the contact point (USER_DEF_MTX makes Gr_GetNodeWorldPos read the written
    // matrix). The effects spawn synchronously, so they capture it before the restore below.
    int   weak = Hypernova_IsWeakBreakFamily(yaku_gobj);
    void *jobj = Yaku_InstanceJObj(record);
    void *dnode = weak ? Hypernova_WeakDebrisNode(yaku_gobj, record) : NULL;
    float dsave[12];
    u32   dflags = 0;
    if (dnode != NULL)
    {
        float *nm = (float *)((char *)dnode + 0x44);
        for (int i = 0; i < 12; i++)
            dsave[i] = nm[i];
        dflags = *(u32 *)((char *)dnode + 0x14);
        nm[3]  = contact.X;
        nm[7]  = contact.Y;
        nm[11] = contact.Z;
        JObj_SetFlags((JOBJ *)dnode, JOBJ_USER_DEFINED_MTX);
    }

    collideWithObject(yaku_gobj, &coll, holder, region_idx, &contact);

    region->refine_flags = saved;

    if (dnode != NULL)
    {
        float *nm = (float *)((char *)dnode + 0x44);
        for (int i = 0; i < 12; i++)
            nm[i] = dsave[i];
        *(u32 *)((char *)dnode + 0x14) = dflags;
    }

    // The break tail retires collision on success.
    if (!grScene_IsInstanceCollAll(record, 1))
    {
        // The weak break never hides the dragged intact mesh; clearing USER_DEF_MTX collapses
        // the joint to its degenerate SRT.
        if (weak && jobj != NULL)
            JObj_ClearFlags((JOBJ *)jobj, JOBJ_USER_DEFINED_MTX);
        return 1;
    }

    // It did not fire - keep the prop's collision retired for the rest of the flight.
    grScene_SetInstanceColl(record, 0);
    return 0;
}

// Advance one claimed prop a frame: pull it toward the rider, shrink it once close, and break it
// on arrival or once shrunk enough. Returns 1 once it is destroyed.
static int Hypernova_PullInstance(GOBJ *rider_gobj, RiderData *rd, void *record)
{
    void *jobj = Yaku_InstanceJObj(record);
    if (jobj == NULL)
        return 1; // nothing to drive; drop the claim

    // Retire the baked collision for the whole flight; it can't follow the model, and would
    // otherwise leave an invisible wall at the origin.
    grScene_SetInstanceColl(record, 0);

    // USER_DEF_MTX makes the per-frame SRT rebuild honor the matrix written below (weak families
    // are JOBJ_SKELETON joints; idempotent for the static ones).
    JObj_SetFlags((JOBJ *)jobj, JOBJ_USER_DEFINED_MTX);

    float *jmtx = (float *)((char *)jobj + 0x44);                 // 3x4 row-major world matrix
    float *cached = (float *)((char *)record + YAKU_INST_MATRIX); // load-time copy (orig 3x3 scale)
    Vec3 pos;
    pos.X = jmtx[3];
    pos.Y = jmtx[7];
    pos.Z = jmtx[11];

    float dist2 = VECSquareDistance(&rd->pos, &pos);

    // Arrived: snap onto the rider and break.
    if (dist2 <= HYPERNOVA_YAKU_BREAK_RADIUS * HYPERNOVA_YAKU_BREAK_RADIUS)
    {
        Hypernova_SetMtxTranslation(jmtx, &rd->pos);
        Hypernova_SetMtxTranslation(cached, &rd->pos);
        return Hypernova_BreakInstanceNative(rider_gobj, record);
    }

    Hypernova_StepToward(&pos, &rd->pos);
    Hypernova_SetMtxTranslation(jmtx, &pos);
    Hypernova_SetMtxTranslation(cached, &pos);

    // Tumble the live matrix only; the cached copy keeps the load-time scale as the shrink
    // baseline.
    Vec3 spin_axis;
    if (Hypernova_SpinAxis(&pos, &rd->pos, &spin_axis))
        Hypernova_SpinMtx3x3(jmtx, &spin_axis, HYPERNOVA_SPIN_RATE * Hypernova_SpinSign(record));

    if (dist2 <= HYPERNOVA_YAKU_SHRINK_RADIUS * HYPERNOVA_YAKU_SHRINK_RADIUS)
    {
        Hypernova_ScaleMtx3x3(jmtx, HYPERNOVA_YAKU_SHRINK);
        float orig2 = Hypernova_Mtx3x3Row0Mag2(cached); // load-time scale
        float cur2  = Hypernova_Mtx3x3Row0Mag2(jmtx);
        if (cur2 < (HYPERNOVA_YAKU_BREAK_SCALE * HYPERNOVA_YAKU_BREAK_SCALE) * orig2)
            return Hypernova_BreakInstanceNative(rider_gobj, record);
    }

    return 0; // still in flight
}

// A swept powerup is claimed here and pulled every frame regardless of cone membership.
// Collection stays with the vanilla pickup trigger; a claim is dropped the frame its item leaves
// the bucket, so a reused pointer self-heals.
#define HYPERNOVA_MAX_ITEM_CLAIMS 128

typedef struct
{
    void *item;  // claimed ItemData
    int   owner; // player slot that claimed it
} HnItemClaim;

static HnItemClaim hn_item_claims[HYPERNOVA_MAX_ITEM_CLAIMS];
static int         hn_item_claim_count;

static int Hypernova_FindItemClaim(void *item)
{
    for (int k = 0; k < hn_item_claim_count; k++)
        if (hn_item_claims[k].item == item)
            return k;
    return -1;
}

static void Hypernova_AddItemClaim(void *item, int owner)
{
    if (hn_item_claim_count >= HYPERNOVA_MAX_ITEM_CLAIMS)
        return;
    if (Hypernova_FindItemClaim(item) >= 0)
        return; // already in flight
    hn_item_claims[hn_item_claim_count].item  = item;
    hn_item_claims[hn_item_claim_count].owner = owner;
    hn_item_claim_count++;
}

// Swap-remove; callers iterate backward so this stays index-safe.
static void Hypernova_RemoveItemClaimAt(int k)
{
    hn_item_claim_count--;
    hn_item_claims[k] = hn_item_claims[hn_item_claim_count];
}

// Walks the bucket so a dangling claim is never dereferenced: item_category is read only after
// the pointer is confirmed present.
static int Hypernova_ItemIsLivePowerup(ItemData *id)
{
    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_ITEM]; g != NULL; g = g->next)
    {
        if (g->entity_class != ITEM_GOBJ_KIND)
            continue;
        if ((ItemData *)g->userdata == id)
            return id->item_category != 0; // reject if the slot is now a box
    }
    return 0; // collected or despawned
}

static void Hypernova_ClaimItems(int player, RiderData *rd, Vec3 *aim)
{
    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_ITEM]; g != NULL; g = g->next)
    {
        if (g->entity_class != ITEM_GOBJ_KIND)
            continue;
        ItemData *id = (ItemData *)g->userdata;
        if (id == NULL)
            continue;
        if (id->item_category == 0) // skip boxes (0 = box, non-0 = powerup)
            continue;
        if (!Hypernova_InCone(&rd->pos, aim, &id->pos))
            continue;
        Hypernova_AddItemClaim(id, player);
    }
}

void Hypernova_VacuumProcessClaimedItems(void)
{
    for (int k = hn_item_claim_count - 1; k >= 0; k--)
    {
        ItemData *id = (ItemData *)hn_item_claims[k].item;
        if (!Hypernova_ItemIsLivePowerup(id))
        {
            Hypernova_RemoveItemClaimAt(k); // collected or despawned
            continue;
        }
        GOBJ *rg = Ply_GetRiderGObj(hn_item_claims[k].owner);
        if (rg == NULL)
        {
            Hypernova_RemoveItemClaimAt(k); // owner gone
            continue;
        }
        Hypernova_PullItem((RiderData *)rg->userdata, id);
    }
}

#define HYPERNOVA_MAX_BREAK_PARENTS 32

// Gathers the breakable-yakumono parent GObjs, used only as owner keys (pointer match) when
// scanning the instance pool.
static int Hypernova_CollectBreakParents(GOBJ **out)
{
    int n = 0;
    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_YAKUMONO];
         g != NULL && n < HYPERNOVA_MAX_BREAK_PARENTS; g = g->next)
    {
        if (g->entity_class != YAKUMONO_GOBJ_KIND)
            continue;
        YakumonoData *yd = (YakumonoData *)g->userdata;
        if (yd != NULL && Hypernova_IsBreakableYaku(yd->desc_id))
            out[n++] = g;
    }
    return n;
}

// A swept breakable is claimed here and pulled to destruction every frame regardless of cone
// membership, keyed by scene-instance record. Sized above CT's ~130 breakables so a wide cone
// can't starve later props.
#define HYPERNOVA_MAX_CLAIMS 200

typedef struct
{
    void *record; // scene-instance record being drawn in
    int   owner;  // player slot that claimed it (pull target / break attribution)
    int   age;    // frames since claimed
} HnClaim;

static HnClaim hn_claims[HYPERNOVA_MAX_CLAIMS];
static int     hn_claim_count;

static int Hypernova_IsClaimed(void *record)
{
    for (int k = 0; k < hn_claim_count; k++)
        if (hn_claims[k].record == record)
            return 1;
    return 0;
}

// 1 if newly claimed, 0 if already claimed or the claim set is full.
static int Hypernova_AddClaim(void *record, int owner)
{
    if (hn_claim_count >= HYPERNOVA_MAX_CLAIMS || Hypernova_IsClaimed(record))
        return 0;
    hn_claims[hn_claim_count].record = record;
    hn_claims[hn_claim_count].owner  = owner;
    hn_claims[hn_claim_count].age    = 0;
    hn_claim_count++;
    return 1;
}

// Swap-remove; callers iterate backward so this stays index-safe.
static void Hypernova_RemoveClaimAt(int k)
{
    hn_claim_count--;
    hn_claims[k] = hn_claims[hn_claim_count];
}

static void Hypernova_ClaimYakumono(int player, RiderData *rd, Vec3 *aim)
{
    int count;
    void *pool = Yaku_GetInstancePool(&count);
    if (pool == NULL || count <= 0)
        return;

    GOBJ *parents[HYPERNOVA_MAX_BREAK_PARENTS];
    int nparents = Hypernova_CollectBreakParents(parents);
    if (nparents == 0)
        return;

    for (int i = 0; i < count; i++)
    {
        void *record = Yaku_GetInstance(pool, i);
        GOBJ *owner = Yaku_InstanceParent(record);
        if (owner == NULL)
            continue;

        // Pointer match only - the stored owner is meaningless for non-break records.
        int breakable = 0;
        for (int p = 0; p < nparents; p++)
        {
            if (parents[p] == owner)
            {
                breakable = 1;
                break;
            }
        }
        if (!breakable)
            continue;

        if (Hypernova_IsClaimed(record))
            continue; // already in flight
        if (!grScene_IsInstanceCollAll(record, 1))
            continue; // already broken / retired
        void *jobj = Yaku_InstanceJObj(record);
        if (jobj == NULL)
            continue;
        float *jmtx = (float *)((char *)jobj + 0x44);
        Vec3 ppos;
        ppos.X = jmtx[3];
        ppos.Y = jmtx[7];
        ppos.Z = jmtx[11];
        if (!Hypernova_InCone(&rd->pos, aim, &ppos))
            continue;

        // Retire collision at claim time so the player can't run into a swept-up prop in
        // flight; the break re-arms it only for the dispatch instant.
        if (Hypernova_AddClaim(record, player))
            grScene_SetInstanceColl(record, 0);
    }
}

void Hypernova_VacuumProcessClaimed(void)
{
    for (int k = hn_claim_count - 1; k >= 0; k--)
    {
        void *record = hn_claims[k].record;
        if (record == NULL)
        {
            Hypernova_RemoveClaimAt(k);
            continue;
        }

        GOBJ *rg = Ply_GetRiderGObj(hn_claims[k].owner);
        if (rg == NULL)
        {
            grScene_SetInstanceColl(record, 1); // owner gone - restore the retired collision
            Hypernova_RemoveClaimAt(k);
            continue;
        }
        RiderData *rd = (RiderData *)rg->userdata;

        if (Hypernova_PullInstance(rg, rd, record))
        {
            Hypernova_RemoveClaimAt(k); // destroyed
            continue;
        }

        // A prop that never breaks is released instead of gluing to the rider.
        if (++hn_claims[k].age >= HYPERNOVA_YAKU_CLAIM_TTL)
        {
            grScene_SetInstanceColl(record, 1);
            Hypernova_RemoveClaimAt(k);
        }
    }
}

// Claimed unridden machines, keyed by MachineData and re-validated against the live bucket each
// frame, so one that despawns, gets mounted, or dies self-heals out of the set.
#define MACHINE_GOBJ_KIND         GAMEENTITY_MACHINE  // gobj->entity_class for a machine (16)
#define HYPERNOVA_MAX_MACHINE_CLAIMS 32

typedef struct
{
    void *machine; // claimed MachineData
    int   owner;   // player slot that claimed it
} HnMachineClaim;

static HnMachineClaim hn_machine_claims[HYPERNOVA_MAX_MACHINE_CLAIMS];
static int            hn_machine_claim_count;

static int Hypernova_FindMachineClaim(void *machine)
{
    for (int k = 0; k < hn_machine_claim_count; k++)
        if (hn_machine_claims[k].machine == machine)
            return k;
    return -1;
}

static void Hypernova_AddMachineClaim(void *machine, int owner)
{
    if (hn_machine_claim_count >= HYPERNOVA_MAX_MACHINE_CLAIMS)
        return;
    if (Hypernova_FindMachineClaim(machine) >= 0)
        return; // already in flight
    hn_machine_claims[hn_machine_claim_count].machine = machine;
    hn_machine_claims[hn_machine_claim_count].owner   = owner;
    hn_machine_claim_count++;
}

// Swap-remove; callers iterate backward so this stays index-safe.
static void Hypernova_RemoveMachineClaimAt(int k)
{
    hn_machine_claim_count--;
    hn_machine_claims[k] = hn_machine_claims[hn_machine_claim_count];
}

// A target only while present in the bucket, unridden, and not already dying. Walks the bucket
// so a dangling claim is never dereferenced.
static int Hypernova_MachineIsLiveTarget(MachineData *md)
{
    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_MACHINE]; g != NULL; g = g->next)
    {
        if (g->entity_class != MACHINE_GOBJ_KIND)
            continue;
        if ((MachineData *)g->userdata == md)
            return md->rider_gobj == NULL && !md->is_dead && !md->is_fall_dead;
    }
    return 0; // despawned
}

static void Hypernova_ClaimMachines(int player, RiderData *rd, Vec3 *aim)
{
    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_MACHINE]; g != NULL; g = g->next)
    {
        if (g->entity_class != MACHINE_GOBJ_KIND)
            continue;
        MachineData *md = (MachineData *)g->userdata;
        if (md == NULL)
            continue;
        if (md->rider_gobj != NULL) // only parked machines (skips human + CPU riders)
            continue;
        if (md->is_dead || md->is_fall_dead)
            continue;
        if (!Hypernova_InCone(&rd->pos, aim, &md->pos))
            continue;
        Hypernova_AddMachineClaim(md, player);
    }
}

// Machine_PhysicsThink integrates accel and velocity into pos every frame, so both are zeroed
// to keep the pos override from being fought.
static void Hypernova_PullMachine(RiderData *rd, MachineData *md)
{
    Hypernova_StepToward(&md->pos, &rd->pos);

    float *accel = (float *)((char *)md + HYPERNOVA_MACHINE_ACCEL_OFF);
    accel[0] = 0.0f;
    accel[1] = 0.0f;
    accel[2] = 0.0f;
    md->velocity.X = 0.0f;
    md->velocity.Y = 0.0f;
    md->velocity.Z = 0.0f;
}

// The BreakDown explosion + GObj_Destroy in Machine_OnKO's tail are gated by md[0x78] bit 0x40,
// so arm that first.
static void Hypernova_KOMachine(MachineData *md)
{
    ((u8 *)md)[HYPERNOVA_MACHINE_KO_GATE_OFF] |= HYPERNOVA_MACHINE_KO_GATE_BIT;
    Machine_OnKO(md);
}

void Hypernova_VacuumProcessClaimedMachines(void)
{
    for (int k = hn_machine_claim_count - 1; k >= 0; k--)
    {
        MachineData *md = (MachineData *)hn_machine_claims[k].machine;
        if (!Hypernova_MachineIsLiveTarget(md))
        {
            Hypernova_RemoveMachineClaimAt(k); // mounted, despawned, or already dying
            continue;
        }
        GOBJ *rg = Ply_GetRiderGObj(hn_machine_claims[k].owner);
        if (rg == NULL)
        {
            Hypernova_RemoveMachineClaimAt(k); // owner gone
            continue;
        }
        RiderData *rd = (RiderData *)rg->userdata;

        if (VECSquareDistance(&rd->pos, &md->pos)
            <= HYPERNOVA_MACHINE_BREAK_RADIUS * HYPERNOVA_MACHINE_BREAK_RADIUS)
        {
            Hypernova_KOMachine(md);
            Hypernova_RemoveMachineClaimAt(k); // KO'd - the machine tears itself down
            continue;
        }

        Hypernova_PullMachine(rd, md);
    }
}

void Hypernova_VacuumFinishClaimedPlayer(int player)
{
    GOBJ *rg = Ply_GetRiderGObj(player);

    // Break this player's in-flight props, or restore collision if unbreakable.
    for (int k = hn_claim_count - 1; k >= 0; k--)
    {
        if (hn_claims[k].owner != player)
            continue;
        void *record = hn_claims[k].record;
        if (record != NULL)
        {
            if (rg == NULL || !Hypernova_BreakInstanceNative(rg, record))
                grScene_SetInstanceColl(record, 1); // restore the retired collision
        }
        Hypernova_RemoveClaimAt(k);
    }

    // Release this player's in-flight items back to vanilla physics.
    for (int k = hn_item_claim_count - 1; k >= 0; k--)
        if (hn_item_claims[k].owner == player)
            Hypernova_RemoveItemClaimAt(k);

    // Machines need nothing restored - the pull only zeroed velocity, which vanilla physics
    // rebuilds, so a dropped machine resumes sitting where it is.
    for (int k = hn_machine_claim_count - 1; k >= 0; k--)
        if (hn_machine_claims[k].owner == player)
            Hypernova_RemoveMachineClaimAt(k);
}

void Hypernova_VacuumReset(void)
{
    hn_claim_count         = 0;
    hn_item_claim_count    = 0;
    hn_machine_claim_count = 0;
}

void Hypernova_VacuumPlayer(int player, RiderData *rd)
{
    Vec3 fwd = rd->forward;
    Vec3 aim;
    if (VEC_NormalizeAndSnap(&fwd, &aim) < 0.01f) // returns |fwd|
        return; // no usable facing this frame

    Hypernova_ClaimItems(player, rd, &aim);
    if (hypernova_suck_yaku)
        Hypernova_ClaimYakumono(player, rd, &aim);
    if (hypernova_suck_machines)
        Hypernova_ClaimMachines(player, rd, &aim);
}

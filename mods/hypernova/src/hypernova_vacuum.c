#include <string.h>

#include "os.h"
#include "game.h"
#include "obj.h"
#include "rider.h"
#include "item.h"
#include "yakumono.h"
#include "collision.h"

#include "hypernova.h"

#define ITEM_GOBJ_KIND 22  // gobj->entity_class for a City Trial item

// True if `target` lies within the suction cone (inside RANGE and within the half-angle of unit
// `aim_unit`). Sqrt-free: compares squared distances and tests dot(d,aim) >= cos*|d| squared.
static int Hypernova_InCone(Vec3 *origin, Vec3 *aim_unit, Vec3 *target)
{
    Vec3 d;
    VECSubtract(target, origin, &d); // d = target - origin
    float dist2 = VECSquareMag(&d);
    if (dist2 > HYPERNOVA_RANGE * HYPERNOVA_RANGE)
        return 0;
    if (dist2 < 0.0001f)
        return 1; // essentially on top of the rider

    float proj = VECDotProduct(&d, aim_unit); // = |d| * cos(angle)
    if (proj < 0.0f)
        return 0; // target is behind the rider
    return proj * proj >= (HYPERNOVA_HALF_ANGLE_COS * HYPERNOVA_HALF_ANGLE_COS) * dist2;
}

// The shared committed-pull step (items AND props): advance `p` toward the rider by
// max(SPEED*dist, MIN) world units this frame. The target is not the rider itself but a point
// lifted above it by a parabolic hump of the horizontal distance (ARC_HEIGHT * 4u(1-u),
// u = hdist^2/RANGE^2), so the object arcs up and over and the lift vanishes right at the rider.
// Sqrt-free except for the one normalize on the final-approach floor branch.
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
    VECSubtract(&to, p, &gap); // gap = lifted target - p
    float dist2 = VECSquareMag(&gap);
    if (dist2 < 1.0e-6f)
    {
        *p = to; // essentially arrived; snap to avoid a divide-by-zero normalize
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

// Pseudo-random spin direction for one target, hashed from its pointer: stable for the whole
// flight but mixed across targets (xor-folds high bits down so heap alignment can't bias it).
static float Hypernova_SpinSign(void *key)
{
    u32 h = (u32)key;
    h ^= h >> 7;
    h ^= h >> 13;
    return (h & 1) ? 1.0f : -1.0f;
}

// Build the (unit) spin axis: perpendicular to travel (rider - pos) and horizontal, so the
// target pitches end-over-end. Falls back to world X if travel is near-vertical; returns 0 if
// there's no usable direction. The axis MUST be unit (Vec3_RotateAboutUnitAxis assumes it).
static int Hypernova_SpinAxis(Vec3 *pos, Vec3 *rider, Vec3 *out)
{
    Vec3 dir;
    VECSubtract(rider, pos, &dir);
    if (VECSquareMag(&dir) < 1.0e-6f)
        return 0;

    Vec3 up;
    up.X = 0.0f; up.Y = 1.0f; up.Z = 0.0f;
    Vec3 cross;
    VECCrossProduct(&dir, &up, &cross); // perpendicular to travel, horizontal
    if (VECSquareMag(&cross) < 1.0e-6f)
    {
        cross.X = 1.0f; cross.Y = 0.0f; cross.Z = 0.0f; // travel near-vertical
    }
    VECNormalize(&cross, out);
    return 1;
}

// Spin an item: rotate its forward/up orientation vectors (CityItem_UpdatePosition rebuilds the
// render matrix from them). Rotating both by the same rotation keeps them orthonormal.
static void Hypernova_SpinItem(ItemData *id, Vec3 *rider)
{
    Vec3 axis;
    if (!Hypernova_SpinAxis(&id->pos, rider, &axis))
        return;
    float ang = HYPERNOVA_SPIN_RATE * Hypernova_SpinSign(id);
    Vec3_RotateAboutUnitAxis(&id->forward, &axis, ang);
    Vec3_RotateAboutUnitAxis(&id->up, &axis, ang);
}

// Spin a prop: rotate the three basis columns of its 3x4 row-major world matrix, leaving the
// translation column untouched. Length-preserving, so it doesn't perturb the shrink.
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

// Hypernova targets only the breakable City Trial props. desc_id (YakumonoData+0x04) is also
// the break-count stat-index: 29 star pole, 32 forest pitfall, 33 coral, 34 trees, 35 rocks,
// 36 volcano rock walls, 37 volcano-base holes, 38 dilapidated houses.
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

// Squared magnitude of row 0 - a sqrt-free proxy for the matrix's uniform scale. Comparing two
// row-0 magnitudes gives the shrink ratio squared.
static float Hypernova_Mtx3x3Row0Mag2(float *mtx)
{
    return mtx[0] * mtx[0] + mtx[1] * mtx[1] + mtx[2] * mtx[2];
}

// Global index of a record's first region within Yaku_GetRegionArray() - the value
// collideWithObject takes as regionIdx. Returns -1 if the regions aren't a clean
// YAKU_REGION_SIZE-strided slice (defensive; always holds for break-family props).
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

// True if this prop's family breaks through hitWeakObject (coral 33 / trees 34 / rocks 35). The
// weak break doesn't hide the dragged intact mesh, so it needs special handling at the rider.
static int Hypernova_IsWeakBreakFamily(GOBJ *yaku_gobj)
{
    YakumonoData *yd = Yaku_GetData(yaku_gobj);
    if (yd == NULL)
        return 0;
    return Yaku_GetDescCollFunc(yd->desc_id) == (void *)hitWeakObject;
}

// Resolve the weak break's debris-anchor JObj so the break can relocate the rubble onto the
// rider (the weak rubble is debris pinned to a separate grobj node at the prop's baked spot, so
// without relocation it erupts where the prop started). Mirrors hitWeakObject's own lookup:
// family break data (yd->data_ptr) -> per-instance entry table (stride 0x10) -> node id at
// entry+0x08 -> grobj node registry. NULL if any link can't be resolved.
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

    // We WRITE this node's matrix, so guard against a garbage entry: require a 4-aligned MEM1 ptr.
    u32 a = (u32)node;
    if (a < 0x80000000u || a >= 0x81800000u || (a & 3) != 0)
        return NULL;
    return node;
}

// Break a fully-drawn-in prop by synthesizing the collision its family breaks on. Hands
// collideWithObject a fabricated collider whose force is cranked far above any prop HP, so the
// prop breaks in one hit through the genuine family path (collision retire, mesh hide, debris +
// item drops, SFX, break-count credit). The collision is re-armed here (it's retired for the
// flight) so the break tail's "still collidable?" guard passes; the tail retires it again.
// Returns 1 if the break fired (detected: collision no longer all-on after the call).
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

    // The impact-speed calc projects our delta onto the region's outward normal and clamps a
    // non-positive projection to zero, so we need a real normal to aim the delta against.
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

    // Re-arm collision so the family coll_func's "still collidable?" guard passes; the tail
    // retires it (and every region) on a successful break.
    grScene_SetInstanceColl(record, 1);

    // Synthetic collider, zeroed. mpCollInfo+0x1d0 = -1 marks "no BigStar region" so
    // destroyBigStar returns 0 and the break proceeds. radius is the force lever.
    u8 coll_info[0x200];
    CollData coll;
    memset(coll_info, 0, sizeof(coll_info));
    memset(&coll, 0, sizeof(coll));
    *(int *)(coll_info + 0x1d0) = -1;
    coll.g         = rider_gobj;               // attribution: human rider GObj
    coll.coll_info = (mpCollInfo *)coll_info;   // empty mpCollInfo (no live hits)
    coll.radius    = HYPERNOVA_BREAK_FORCE_RADIUS;

    // Delta points INTO the surface (against the outward normal): the engine negates the
    // normalized delta before projecting it, so this reads as a positive impact speed.
    VECScale(&n_unit, &coll.pos_delta, -HYPERNOVA_BREAK_FORCE_DELTA);

    // Skip the geometry-refined impact path (it can rewrite our delta from the prop's matrices).
    YakuCollRegion *region = &regions[region_idx];
    u32 saved = region->refine_flags;
    region->refine_flags = saved & ~(u32)YAKU_REGION_REFINE;

    // Contact point: the prop's current (pulled-in) world position.
    Vec3 contact;
    Yaku_InstanceCachedPos(record, &contact);

    // Weak families (trees/rocks/coral) show their break as debris effects the engine pins to a
    // separate grobj node at the prop's BAKED spot, so relocate that node onto the rider's
    // contact point for the break instant (USER_DEF_MTX makes Gr_GetNodeWorldPos read our matrix),
    // then restore it. The effects spawn synchronously inside collideWithObject, so they capture
    // the relocated position and outlive the restore.
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

    // The break tail retires collision on success; if it did, we're done.
    if (!grScene_IsInstanceCollAll(record, 1))
    {
        // Collapse the dragged intact mesh so it doesn't linger frozen at the rider (the weak
        // break never hides it). Clearing USER_DEF_MTX drops the joint to its degenerate SRT.
        if (weak && jobj != NULL)
            JObj_ClearFlags((JOBJ *)jobj, JOBJ_USER_DEFINED_MTX);
        return 1;
    }

    // It did not fire - keep the prop's collision retired for the rest of the flight.
    grScene_SetInstanceColl(record, 0);
    return 0;
}

// Advance one claimed prop a frame: retire its baked collision (no stranded wall), pull it
// toward the rider, shrink it once close, and break it on arrival or once shrunk enough.
// Returns 1 once it is destroyed.
static int Hypernova_PullInstance(GOBJ *rider_gobj, RiderData *rd, void *record)
{
    void *jobj = Yaku_InstanceJObj(record);
    if (jobj == NULL)
        return 1; // nothing to drive; drop the claim

    // Retire the baked collision for the whole flight so the moved prop leaves no invisible wall.
    grScene_SetInstanceColl(record, 0);

    // Take over the joint's world matrix. The weak families (trees 34, rocks 35, coral 33) are
    // JOBJ_SKELETON joints whose world matrix is rebuilt from the SRT every frame; USER_DEF_MTX
    // makes that setup honor our write (idempotent for the already-static families).
    JObj_SetFlags((JOBJ *)jobj, JOBJ_USER_DEFINED_MTX);

    float *jmtx = (float *)((char *)jobj + 0x44);                 // 3x4 row-major world matrix
    float *cached = (float *)((char *)record + YAKU_INST_MATRIX); // load-time copy (orig 3x3 scale)
    Vec3 pos;
    pos.X = jmtx[3];
    pos.Y = jmtx[7];
    pos.Z = jmtx[11];

    float dist2 = VECSquareDistance(&rd->pos, &pos);

    // Arrived: snap onto the rider and break now.
    if (dist2 <= HYPERNOVA_YAKU_BREAK_RADIUS * HYPERNOVA_YAKU_BREAK_RADIUS)
    {
        Hypernova_SetMtxTranslation(jmtx, &rd->pos);
        Hypernova_SetMtxTranslation(cached, &rd->pos);
        return Hypernova_BreakInstanceNative(rider_gobj, record);
    }

    // Close on the rider via the shared pull step, then write the stepped position into both
    // the live and cached matrices.
    Hypernova_StepToward(&pos, &rd->pos);
    Hypernova_SetMtxTranslation(jmtx, &pos);
    Hypernova_SetMtxTranslation(cached, &pos);

    // Tumble while flying in (live matrix only; the cached copy keeps the load-time orientation
    // as the shrink baseline). Rotation preserves row magnitude, so the shrink below is unaffected.
    Vec3 spin_axis;
    if (Hypernova_SpinAxis(&pos, &rd->pos, &spin_axis))
        Hypernova_SpinMtx3x3(jmtx, &spin_axis, HYPERNOVA_SPIN_RATE * Hypernova_SpinSign(record));

    // Shrink only once much closer than the cone reach.
    if (dist2 <= HYPERNOVA_YAKU_SHRINK_RADIUS * HYPERNOVA_YAKU_SHRINK_RADIUS)
    {
        Hypernova_ScaleMtx3x3(jmtx, HYPERNOVA_YAKU_SHRINK);
        float orig2 = Hypernova_Mtx3x3Row0Mag2(cached); // cached 3x3 keeps the load-time scale
        float cur2  = Hypernova_Mtx3x3Row0Mag2(jmtx);
        if (cur2 < (HYPERNOVA_YAKU_BREAK_SCALE * HYPERNOVA_YAKU_BREAK_SCALE) * orig2)
            return Hypernova_BreakInstanceNative(rider_gobj, record);
    }

    return 0; // still in flight
}

// Once the cone sweeps a powerup item it is recorded here and pulled toward its claiming player
// every frame in Hypernova_VacuumProcessClaimedItems (cone membership / trigger no longer matter),
// so suction can't leave a swept item behind. Collection stays with the vanilla pickup trigger;
// the claim is dropped the frame its item leaves the bucket, so a reused pointer self-heals.
#define HYPERNOVA_MAX_ITEM_CLAIMS 128

typedef struct
{
    void *item;  // claimed ItemData pointer (pull target)
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
        return; // already in flight (re-swept by the cone)
    hn_item_claims[hn_item_claim_count].item  = item;
    hn_item_claims[hn_item_claim_count].owner = owner;
    hn_item_claim_count++;
}

// Swap-remove (callers iterate the array backward so this is index-safe).
static void Hypernova_RemoveItemClaimAt(int k)
{
    hn_item_claim_count--;
    hn_item_claims[k] = hn_item_claims[hn_item_claim_count];
}

// True iff `id` is still a live powerup in the item bucket. Walks the bucket so we never deref a
// dangling claim, and reads item_category only after confirming the pointer is still present.
static int Hypernova_ItemIsLivePowerup(ItemData *id)
{
    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_ITEM]; g != NULL; g = g->next)
    {
        if (g->entity_class != ITEM_GOBJ_KIND)
            continue;
        if ((ItemData *)g->userdata == id)
            return id->item_category != 0; // present; reject if the slot is now a box
    }
    return 0; // collected or despawned
}

// Claim every in-cone powerup item for `player` (moves nothing).
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
    // Backward walk so swap-removal stays index-safe.
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

// Gather the breakable-yakumono parent GObjs on the yakumono p_link list. Used only as owner
// keys (pointer match) when scanning the instance pool below. Returns the count.
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

// Once the cone touches a breakable prop it is recorded here and pulled to destruction every
// frame in Hypernova_VacuumProcessClaimed regardless of cone membership, so suction can't strand
// a half-shrunk prop. Keyed by the scene-instance record pointer (cleared on scene change).
// Sized well above CT's breakable-prop count (~130) so a wide cone can claim - and retire the
// collision of - every prop it sweeps in one frame rather than starving later props.
#define HYPERNOVA_MAX_CLAIMS 200

typedef struct
{
    void *record; // scene-instance record being drawn in
    int   owner;  // player slot that claimed it (pull target / break attribution)
    int   age;    // frames since claimed (safety TTL)
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

// Returns 1 if the record was newly claimed (so the caller can retire its collision), 0 if it
// was already claimed or the claim set is full.
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

// Swap-remove (callers iterate the array backward so this is index-safe).
static void Hypernova_RemoveClaimAt(int k)
{
    hn_claim_count--;
    hn_claims[k] = hn_claims[hn_claim_count];
}

// Claim every in-cone breakable prop for `player` (moves nothing).
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

        // Pointer match only - never deref the stored owner (meaningless for non-break records).
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

        // Retire collision the instant the cone claims it, so the player can't run into a
        // swept-up prop in flight. The break re-arms it only for the dispatch instant.
        if (Hypernova_AddClaim(record, player))
            grScene_SetInstanceColl(record, 0);
    }
}

void Hypernova_VacuumProcessClaimed(void)
{
    // Backward walk so swap-removal stays index-safe.
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
            grScene_SetInstanceColl(record, 1); // owner gone - restore the collision we retired
            Hypernova_RemoveClaimAt(k);
            continue;
        }
        RiderData *rd = (RiderData *)rg->userdata;

        if (Hypernova_PullInstance(rg, rd, record))
        {
            Hypernova_RemoveClaimAt(k); // destroyed
            continue;
        }

        // Safety net: a prop that somehow never breaks is released instead of gluing to the rider.
        if (++hn_claims[k].age >= HYPERNOVA_YAKU_CLAIM_TTL)
        {
            grScene_SetInstanceColl(record, 1);
            Hypernova_RemoveClaimAt(k);
        }
    }
}

void Hypernova_VacuumFinishClaimedPlayer(int player)
{
    GOBJ *rg = Ply_GetRiderGObj(player);

    // Break this player's in-flight props (or restore collision if unbreakable).
    for (int k = hn_claim_count - 1; k >= 0; k--)
    {
        if (hn_claims[k].owner != player)
            continue;
        void *record = hn_claims[k].record;
        if (record != NULL)
        {
            if (rg == NULL || !Hypernova_BreakInstanceNative(rg, record))
                grScene_SetInstanceColl(record, 1); // restore the collision we retired
        }
        Hypernova_RemoveClaimAt(k);
    }

    // Release this player's in-flight items back to vanilla physics (no forced finish).
    for (int k = hn_item_claim_count - 1; k >= 0; k--)
        if (hn_item_claims[k].owner == player)
            Hypernova_RemoveItemClaimAt(k);
}

void Hypernova_VacuumReset(void)
{
    hn_claim_count      = 0;
    hn_item_claim_count = 0;
}

void Hypernova_VacuumPlayer(int player, RiderData *rd)
{
    Vec3 fwd = rd->forward;
    Vec3 aim;
    if (VEC_NormalizeAndSnap(&fwd, &aim) < 0.01f) // unit forward; returns |fwd|
        return; // no usable facing direction this frame

    Hypernova_ClaimItems(player, rd, &aim);
    if (hypernova_suck_yaku)
        Hypernova_ClaimYakumono(player, rd, &aim);
}

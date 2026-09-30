#include <string.h>

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "obj.h"
#include "rider.h"
#include "machine.h"
#include "collision.h"
#include "weapon.h"
#include "code_patch/code_patch.h"

#include "ap_star.h"
#include "ap_star_shot.h"
#include "ap_star_shot_fx.h"

#define AP_STAR_POD_NUM   APSTARPIECE_NUM // one per sphere color, in the same order
#define AP_STAR_POD_JOINT 9 // first pod; the six are consecutive in the archive's joint tree

// Machines tracked at once; past this a machine has no ring, and no shot, until one frees.
#define AP_STAR_RING_MAX 32

// The shot is a projectile kind of its own, appended after the vanilla ones. Its
// WeaponKindVTable goes in a relocated copy of the engine's table; its WeaponKindData goes
// in the padding word after wp_kind_data's slots, which nothing clears or fills.
#define AP_STAR_SHOT_KIND WPKIND_NUM

#define AP_STAR_SHOT_ATTACK (WP_ATTACK_ACTIVE | AP_STAR_SHOT_ATTACK_CAUSE)

// The shot model's joint count, which has to match what the archive holds.
#define AP_STAR_SHOT_JOINTS 2

#define SHOT_RADIUS       4.5f  // the sphere's radius at full size, drawn and hit alike
#define SHOT_MODEL_RADIUS 3.0f  // the sphere's radius as ApStarShot.dat authors it
#define SHOT_COLL_RADIUS  2.7f  // environment collider; smaller, so a ground shot clears a curb
#define SHOT_SPEED        5.0f  // relative to the machine, so this is also its on-screen speed
#define SHOT_LIFETIME     180   // frames
#define SHOT_GROW_FRAMES  30    // the shot swells to full size over the first of those
#define SHOT_FADE_FRAMES  30    // and shrinks out over the last

// The size the grow starts from and the fade ends at. Not zero: the same field
// sizes the model, the hitbox and the render cull, and a shot with no extent at all
// is a degenerate one for a frame.
#define SHOT_SEED_SCALE 0.05f
#define SHOT_PROBE_UP   12.0f // ground probe starts this far above the shot
#define SHOT_PROBE_DOWN 40.0f // and this far below it
#define SHOT_HOVER      (SHOT_RADIUS + 0.5f) // center height over the surface in ground-follow mode

#define HOMING_DELAY       8      // frames a shot flies straight off the nose first
#define HOMING_RANGE       320.0f
#define HOMING_CONE_COS    0.766f // 40 degrees either side of the heading
#define HOMING_TURN_RADIUS 240.0f // the arc a turn follows, the same at any speed
#define HOMING_TURN_MAX    0.07f  // radians a frame

#define POD_SHRINK_FRAMES  10
#define RING_REGROW_FRAMES 60
#define RESPREAD_RATE      0.18f // per-frame fraction of the way to the even ring
#define RESPREAD_SNAP      0.0005f

#define TWO_PI 6.28318531f

// charge_value is clamped to 1.0 as it fills; the margin is for the float.
#define FULL_CHARGE 0.99f

typedef struct RingState
{
    MachineData *md;   // owner, NULL while the slot is free
    JOBJ *root;        // the model the pods were resolved against
    JOBJ *pod[AP_STAR_POD_NUM];
    Vec3 pod_trans[AP_STAR_POD_NUM]; // authored ring positions
    float pod_rot_y[AP_STAR_POD_NUM];
    Vec3 pod_scale;    // the authored scale a full-size pod returns to
    float offset[AP_STAR_POD_NUM];   // current swing around the ring, radians
    float target[AP_STAR_POD_NUM];
    u8 alive_mask;
    u8 spread_mask;    // the alive_mask the targets were solved for
    u8 shrink[AP_STAR_POD_NUM]; // frames left of a fired pod's collapse
    u8 regrowing;
    u8 regrow_timer;
    u32 frame;         // last stc_frame this machine was seen
} RingState;

static RingState stc_rings[AP_STAR_RING_MAX];
static u32 stc_frame;   // counts only frames in which some star ran its Think
static int stc_thought;

static int stc_star_slot = -1;    // class slot, which is what MachineData.kind holds

static RingState *FindRing(MachineData *md)
{
    for (int i = 0; i < AP_STAR_RING_MAX; i++)
    {
        if (stc_rings[i].md == md)
            return &stc_rings[i];
    }
    return NULL;
}

// A slot is reclaimed once its machine has gone a whole frame unseen, so a machine
// destroyed without warning leaves nothing to clean up. A live slot is never taken:
// its owner would take another in turn, and the cascade leaves the machines first in
// the proc list without a ring every frame.
static RingState *ClaimRing(MachineData *md)
{
    RingState *free_slot = NULL;
    RingState *stale = NULL;

    stc_thought = 1;
    for (int i = 0; i < AP_STAR_RING_MAX; i++)
    {
        RingState *r = &stc_rings[i];
        if (r->md == md)
        {
            r->frame = stc_frame;
            return r;
        }
        if (r->md == NULL)
        {
            if (free_slot == NULL)
                free_slot = r;
        }
        else if (stale == NULL && stc_frame - r->frame > 1)
        {
            stale = r;
        }
    }

    RingState *r = free_slot != NULL ? free_slot : stale;
    if (r == NULL)
        return NULL;
    memset(r, 0, sizeof(*r));
    r->md = md;
    r->alive_mask = (1 << AP_STAR_POD_NUM) - 1;
    r->frame = stc_frame;
    return r;
}

// The pods carry no animation tracks, so the authored pose is still readable the
// first time a fresh model is walked.
static int ResolvePods(RingState *r, MachineData *md)
{
    JOBJ *root = (JOBJ *)md->gobj->hsd_object;
    if (root != NULL && root == r->root)
        return 1;

    r->root = NULL;
    if (root == NULL)
        return 0;

    for (int i = 0; i < AP_STAR_POD_NUM; i++)
    {
        r->pod[i] = cm_api->GetMachineJoint(md, AP_STAR_POD_JOINT + i);
        if (r->pod[i] == NULL)
            return 0;
        r->pod_trans[i] = r->pod[i]->trans;
        r->pod_rot_y[i] = r->pod[i]->rot.Y;
        r->offset[i] = 0.0f;
        r->target[i] = 0.0f;
    }
    r->pod_scale = r->pod[0]->scale;
    r->spread_mask = 0;
    r->root = root;
    return 1;
}

static float WrapTurns(float t)
{
    while (t > 0.5f)
        t -= 1.0f;
    while (t <= -0.5f)
        t += 1.0f;
    return t;
}

// Even angles for however many pods are left. The ring never stops spinning, so
// only the transient matters: the phase chosen is the one that moves the pods least.
static void SolveSpread(RingState *r)
{
    int alive[AP_STAR_POD_NUM];
    int count = 0;

    for (int i = 0; i < AP_STAR_POD_NUM; i++)
    {
        if (r->alive_mask & (1 << i))
            alive[count++] = i;
    }

    r->spread_mask = r->alive_mask;
    if (count == 0)
        return;

    // How far each survivor already sits from an even count-ring, in turns.
    float residual[AP_STAR_POD_NUM];
    float drift = 0.0f;

    for (int j = 0; j < count; j++)
    {
        residual[j] = (float)alive[j] / (float)AP_STAR_POD_NUM - (float)j / (float)count;
        drift += WrapTurns(residual[j] - residual[0]);
    }

    float base = residual[0] + drift / (float)count;
    for (int j = 0; j < count; j++)
        r->target[alive[j]] = (base - residual[j]) * TWO_PI;
}

// Overshoots a little before settling, so the ring snaps back rather than
// creeping up to size.
static float GrowCurve(float t)
{
    float u = t - 1.0f;
    return 1.0f + 2.70158f * u * u * u + 1.70158f * u * u;
}

// Swinging a pod around the ring is a rotation of its authored position about
// the pivot's Y, and the same delta on its own yaw so it keeps facing outward.
static void SetPodPose(RingState *r, int i, float f)
{
    JOBJ *j = r->pod[i];
    float d = r->offset[i];

    j->scale.X = r->pod_scale.X * f;
    j->scale.Y = r->pod_scale.Y * f;
    j->scale.Z = r->pod_scale.Z * f;

    if (d != 0.0f)
    {
        float s = sinf(d);
        float c = cosf(d);
        j->trans.X = r->pod_trans[i].X * c + r->pod_trans[i].Z * s;
        j->trans.Y = r->pod_trans[i].Y;
        j->trans.Z = r->pod_trans[i].Z * c - r->pod_trans[i].X * s;
        j->rot.Y = r->pod_rot_y[i] + d;
    }
    else
    {
        j->trans = r->pod_trans[i];
        j->rot.Y = r->pod_rot_y[i];
    }

    JObj_SetMtxDirtySub(j);
}

// Tail of the star class's per-kind Think slot, once per frame per machine.
static void OnStarThink(MachineData *md)
{
    RingState *r = ClaimRing(md);
    if (r == NULL || !ResolvePods(r, md))
        return;

    float grow = 0.0f;
    if (r->regrowing)
    {
        grow = GrowCurve((float)r->regrow_timer / (float)RING_REGROW_FRAMES);
        if (++r->regrow_timer > RING_REGROW_FRAMES)
        {
            r->regrowing = 0;
            r->alive_mask = (1 << AP_STAR_POD_NUM) - 1;
        }
    }
    else if (r->spread_mask != r->alive_mask)
    {
        SolveSpread(r);
    }

    for (int i = 0; i < AP_STAR_POD_NUM; i++)
    {
        float f;
        if (r->regrowing)
        {
            f = grow;
        }
        else if (r->alive_mask & (1 << i))
        {
            // A spent pod holds where it died while it collapses; only the
            // survivors slide, easing in so the ring settles rather than snaps.
            float gap = r->target[i] - r->offset[i];
            r->offset[i] = (gap < RESPREAD_SNAP && gap > -RESPREAD_SNAP)
                               ? r->target[i]
                               : r->offset[i] + gap * RESPREAD_RATE;
            f = 1.0f;
        }
        else if (r->shrink[i] != 0)
        {
            f = (float)(--r->shrink[i]) / (float)POD_SHRINK_FRAMES;
        }
        else
        {
            f = 0.0f;
        }
        SetPodPose(r, i, f);
    }
}

// Tail of the star class's per-kind Init slot. The next Think rebuilds the ring
// against whatever model the new machine loaded.
static void OnStarInit(MachineData *md)
{
    RingState *r = FindRing(md);
    if (r != NULL)
        r->md = NULL;
}

// The remaining pod closest to the machine's heading, compared in the horizontal
// plane so the ring's tilt does not decide it.
static int NearestPod(RingState *r, MachineData *md)
{
    int best = -1;
    float best_dot = -2.0f;
    int have_heading = 0;

    Vec3 fwd = { md->forward.X, 0.0f, md->forward.Z };
    if (VECSquareMag(&fwd) >= 0.0001f)
    {
        VECNormalize(&fwd, &fwd);
        have_heading = 1;
    }

    // No horizontal heading to pick by, so the lowest-index remaining pod stands in.
    if (!have_heading)
    {
        for (int i = 0; i < AP_STAR_POD_NUM; i++)
        {
            if (r->alive_mask & (1 << i))
                return i;
        }
        return -1;
    }

    for (int i = 0; i < AP_STAR_POD_NUM; i++)
    {
        if (!(r->alive_mask & (1 << i)))
            continue;

        Vec3 p;
        JObj_GetWorldPosition(r->pod[i], NULL, &p);

        Vec3 d = { p.X - md->pos.X, 0.0f, p.Z - md->pos.Z };
        if (VECSquareMag(&d) < 0.0001f)
            continue;
        VECNormalize(&d, &d);

        float dot = VECDotProduct(&d, &fwd);
        if (dot > best_dot)
        {
            best_dot = dot;
            best = i;
        }
    }
    return best;
}

// Per-shot state, in the kind's own scratch, which no shared projectile code touches.
typedef struct ShotState
{
    s8 owner_ply;
    s8 target;      // player being steered at, -1 while there is none
    u8 grounded;    // fired over the ground, so it follows it
    u8 homing_done; // it has hit something
    int fx;         // ApStarShotFx handle, 0 for none
} ShotState;

_Static_assert(sizeof(ShotState) <= sizeof(((WeaponData *)0)->kind_scratch),
               "ShotState must fit the projectile's kind scratch");

static ShotState *ShotStateOf(void *proj)
{
    return (ShotState *)((WeaponData *)proj)->kind_scratch;
}

// Where a player can be hit: their machine while they ride it, else the rider.
static int PlayerTarget(int ply, Vec3 *out)
{
    if (Ply_GetPKind(ply) == PKIND_NONE)
        return 0;
    GOBJ *rg = Ply_GetRiderGObj(ply);
    RiderData *rd = rg != NULL ? (RiderData *)rg->userdata : NULL;
    if (rd == NULL)
        return 0;

    GOBJ *mg = rd->machine_gobj;
    MachineData *md = mg != NULL ? (MachineData *)mg->userdata : NULL;
    if (md != NULL && Rider_IsOnMachine(rd))
    {
        if (md->is_dead)
            return 0;
        *out = md->pos;
    }
    else
    {
        *out = rd->pos;
    }
    return 1;
}

// The cosine of the angle between `dir` and the way to `target`, which goes in `to`;
// -2 out of range. A flat shot compares in the horizontal plane.
static float Alignment(const Vec3 *pos, const Vec3 *dir, const Vec3 *target, int flat, Vec3 *to)
{
    to->X = target->X - pos->X;
    to->Y = flat ? 0.0f : target->Y - pos->Y;
    to->Z = target->Z - pos->Z;

    float d2 = VECSquareMag(to);
    if (d2 > HOMING_RANGE * HOMING_RANGE || d2 < 1.0f)
        return -2.0f;

    float inv = 1.0f / sqrtf(d2);
    to->X *= inv;
    to->Y *= inv;
    to->Z *= inv;
    return VECDotProduct(to, (Vec3 *)dir);
}

// Swells in over the first frames and shrinks out over the last, since lifetime ends
// in a plain GObj_Destroy. The weapon's scale sizes the model, the hitbox and the render cull
// together. Runs at prio 1, ahead of the lifetime decrement, so a shot with one frame
// left is already down.
static void ShotThink(void *p)
{
    WeaponData *proj = p;

    if (proj->lifetime <= SHOT_FADE_FRAMES)
    {
        float t = (float)(proj->lifetime - 1) / (float)SHOT_FADE_FRAMES;
        if (t < 0.0f)
            t = 0.0f;
        proj->scale = SHOT_SEED_SCALE + (1.0f - SHOT_SEED_SCALE) * t;
    }
    else if (proj->frame_counter <= SHOT_GROW_FRAMES)
    {
        float t = (float)proj->frame_counter / (float)SHOT_GROW_FRAMES;
        proj->scale = SHOT_SEED_SCALE + (1.0f - SHOT_SEED_SCALE) * t;
    }
}

// Prio 4, ahead of integration. Holds a lock while its player stays in the cone and
// otherwise takes whoever in it is closest to straight ahead, then bends toward them on
// an arc of fixed radius. Speed is kept, so the carry from the machine stays.
static void ShotSteer(void *p)
{
    WeaponData *proj = p;
    ShotState *st = ShotStateOf(proj);
    if (st->homing_done || proj->frame_counter < HOMING_DELAY)
        return;

    int flat = st->grounded;
    Vec3 dir = { proj->vel.X, flat ? 0.0f : proj->vel.Y, proj->vel.Z };
    float speed2 = VECSquareMag(&dir);
    if (speed2 < 0.0001f)
        return;
    float speed = sqrtf(speed2);
    dir.X /= speed;
    dir.Y /= speed;
    dir.Z /= speed;

    Vec3 tp, to;
    Vec3 aim = dir;
    float best = -2.0f;

    if (st->target >= 0)
    {
        if (PlayerTarget(st->target, &tp))
            best = Alignment(&proj->pos, &dir, &tp, flat, &aim);
        if (best < HOMING_CONE_COS)
            st->target = -1;
    }
    if (st->target < 0)
    {
        best = HOMING_CONE_COS;
        for (int ply = 0; ply < PLY_NUM; ply++)
        {
            if (ply == st->owner_ply || !PlayerTarget(ply, &tp))
                continue;
            float c = Alignment(&proj->pos, &dir, &tp, flat, &to);
            if (c >= best)
            {
                best = c;
                aim = to;
                st->target = (s8)ply;
            }
        }
        if (st->target < 0)
            return;
    }

    float turn = speed / HOMING_TURN_RADIUS;
    if (turn > HOMING_TURN_MAX)
        turn = HOMING_TURN_MAX;

    float c = cosf(turn);
    Vec3 nd;
    if (best >= c)
    {
        nd = aim;
    }
    else
    {
        // Rotate by `turn` in the plane of the heading and the aim.
        Vec3 perp = { aim.X - dir.X * best, aim.Y - dir.Y * best, aim.Z - dir.Z * best };
        float pm2 = VECSquareMag(&perp);
        if (pm2 < 0.000001f)
            return;
        float k = sinf(turn) / sqrtf(pm2);
        nd.X = dir.X * c + perp.X * k;
        nd.Y = dir.Y * c + perp.Y * k;
        nd.Z = dir.Z * c + perp.Z * k;
    }

    proj->vel.X = nd.X * speed;
    proj->vel.Z = nd.Z * speed;
    if (!flat)
        proj->vel.Y = nd.Y * speed;
}

// Prio 5. Weapon_UpdateEnvColl pushes a shot out of whatever it touched and leaves
// its velocity alone, so a shot left running would slide along the wall; it ends
// instead. Any contact ends an air shot. A ground shot rides the floor on purpose, so
// only a wall or a ceiling ends it.
static void ShotEnvCollide(void *p)
{
    WeaponData *proj = p;

    Weapon_UpdateEnvColl(proj);
    if (!(proj->flag_b & WP_FLAGB_ENV_CONTACT))
        return;

    mpCollInfo *ci = proj->coll_data->coll_info;
    if (!ShotStateOf(proj)->grounded || ci->wall_rec_num != 0 || ci->top_rec_num != 0)
        GObj_Destroy(proj->gobj);
}

// Prio 6, after the environment pushback and before the root matrix and the HurtData
// pick up the position. Over a gap the probe misses and the shot holds its altitude.
static void ShotFollowGround(void *p)
{
    WeaponData *proj = p;
    if (!ShotStateOf(proj)->grounded)
        return;

    Vec3 from = { proj->pos.X, proj->pos.Y + SHOT_PROBE_UP, proj->pos.Z };
    Vec3 to = { proj->pos.X, proj->pos.Y - SHOT_PROBE_DOWN, proj->pos.Z };
    Vec3 hit;

    if (Raycast_Ground(&from, &to, &hit) < 0)
        return;

    proj->pos.Y = hit.Y + SHOT_HOVER;
    proj->vel.Y = 0.0f;
}

// Flies at the spawn velocity, with no muzzle kick of its own.
static void ShotPostInit(void *p)
{
    WeaponData *proj = p;
    Weapon_StateChange(proj, 0, 0.0f, 1.0f, 0);
    proj->vel = proj->spawn_vel;
}

// Once a shot has hit something it stops steering, so it cannot wheel back through a
// target it has passed. It flies on either way.
static int ShotOnHit(void *p, void *hit)
{
    (void)hit;
    ShotStateOf(p)->homing_done = 1;
    return 0;
}

// Every way a shot ends runs through the dtor, which calls this.
static void ShotTeardown(void *p)
{
    ApStarShotFx_Detach(ShotStateOf(p)->fx);
}

static const WeaponStateEntry stc_shot_states[] = {
    { 0, AP_STAR_SHOT_ATTACK, ShotThink, ShotSteer, ShotEnvCollide, ShotFollowGround },
};

static const WeaponKindVTable stc_shot_vtable = {
    .state_table = stc_shot_states,
    .aux_a = ShotTeardown,
    .post_init = ShotPostInit,
    .on_hit = ShotOnHit,
};

// Plasma spread's hitbox command with only its size changed - the high half of the
// second word, in 1/250 units - so the shot deals the Plasma ability's damage and
// knockback.
static const u32 stc_shot_script[] = {
    0x300000CB,
    (u32)(SHOT_RADIUS * 250.0f) << 16,
    0x00000000,
    0x7D7D7530,
    0x000000FA,
    0x120027FC,
    0x00000000, // end
};

// No animation: the hitbox script still runs, and nothing moves the sphere joint.
static const WeaponStateAnimSpec stc_shot_anim = {
    .script = stc_shot_script,
};

static const WeaponKindParams stc_shot_params = {
    .model_scale = SHOT_RADIUS / SHOT_MODEL_RADIUS,
    .cull_scale = SHOT_RADIUS,
    .lifetime = SHOT_LIFETIME,
};

static const WeaponCollDesc stc_shot_coll = {
    .radius = SHOT_COLL_RADIUS,
};

static WeaponModelBlock stc_shot_model_block = {
    .joint_num = AP_STAR_SHOT_JOINTS,
};

static WeaponKindData stc_shot_kind_data = {
    .params = &stc_shot_params,
    .model_desc = &stc_shot_model_block,
    .state_anim_spec_array = &stc_shot_anim,
    .mpcoll_desc = &stc_shot_coll,
};

// The vanilla table's 17 entries plus the shot's.
static const WeaponKindVTable *stc_vtables[AP_STAR_SHOT_KIND + 1];

// Every lis / addi pair that forms the vtable table's address but Weapon_SystemInit's,
// whose loop runs the vanilla kinds' system_init and has nothing to run for the shot.
static const u32 stc_vtable_sites[][2] = {
    { 0x8021f4b4, 0x8021f4c4 }, // Weapon_Create, state table
    { 0x8021f64c, 0x8021f650 }, // Weapon_Create, init
    { 0x8021f790, 0x8021f794 }, // Weapon_Create, post_init
    { 0x8021fde0, 0x8021fde4 }, // Weapon_Proc10_HitReact
    { 0x8021ff94, 0x8021ff98 }, // Weapon_UserDataDtor
    { 0x8022036c, 0x80220374 }, // Weapon_Despawn
    { 0x802205f0, 0x802205f8 }, // Weapon_LoadKindParams
    { 0x8022065c, 0x80220664 }, // Weapon_ReloadKindParams, load_render_state
    { 0x802206bc, 0x802206c0 }, // Weapon_ReloadKindParams, reset_render_state
};

// addi sign-extends its immediate, so the high half carries the borrow.
static void RepointTable(u32 lis_addr, u32 addi_addr, const void *table)
{
    u32 addr = (u32)table;
    u32 lo = addr & 0xFFFF;
    u32 hi = ((addr >> 16) + ((lo & 0x8000) ? 1 : 0)) & 0xFFFF;

    CODEPATCH_REPLACEINSTRUCTION(lis_addr, (*(u32 *)lis_addr & 0xFFFF0000) | hi);
    CODEPATCH_REPLACEINSTRUCTION(addi_addr, (*(u32 *)addi_addr & 0xFFFF0000) | lo);
}

static void RegisterShotKind(void)
{
    for (int k = 0; k < WPKIND_NUM; k++)
        stc_vtables[k] = wp_kind_vtables[k];
    stc_vtables[AP_STAR_SHOT_KIND] = &stc_shot_vtable;

    for (u32 i = 0; i < sizeof(stc_vtable_sites) / sizeof(stc_vtable_sites[0]); i++)
        RepointTable(stc_vtable_sites[i][0], stc_vtable_sites[i][1], stc_vtables);

    wp_kind_data[AP_STAR_SHOT_KIND] = &stc_shot_kind_data;
}

static void PaintShot(GOBJ *handle, GXColor diffuse)
{
    // Darkened, so a face turned away from the light keeps its hue.
    GXColor ambient;
    ambient.r = (u8)(diffuse.r * 55 / 100);
    ambient.g = (u8)(diffuse.g * 55 / 100);
    ambient.b = (u8)(diffuse.b * 55 / 100);
    ambient.a = 0xFF;

    for (JOBJ *j = (JOBJ *)handle->hsd_object; j != NULL; j = j->child)
    {
        for (DOBJ *d = j->dobj; d != NULL; d = d->next)
        {
            if (d->mobj != NULL && d->mobj->mat != NULL)
            {
                d->mobj->mat->diffuse = diffuse;
                d->mobj->mat->ambient = ambient;
            }
        }
    }
}

static void Fire(RiderData *rd, MachineData *md, RingState *r, int pod)
{
    Vec3 muzzle;
    JObj_GetWorldPosition(r->pod[pod], NULL, &muzzle);

    Vec3 from = { muzzle.X, muzzle.Y + SHOT_PROBE_UP, muzzle.Z };
    Vec3 to = { muzzle.X, muzzle.Y - SHOT_PROBE_DOWN, muzzle.Z };
    Vec3 ground;
    int grounded = Raycast_Ground(&from, &to, &ground) >= 0;

    Vec3 dir, up;
    if (grounded)
    {
        dir.X = md->forward.X;
        dir.Y = 0.0f;
        dir.Z = md->forward.Z;
        up.X = 0.0f;
        up.Y = 1.0f;
        up.Z = 0.0f;
        if (VECSquareMag(&dir) < 0.0001f)
            return;
        VECNormalize(&dir, &dir);
    }
    else
    {
        dir = md->forward;
        up = md->up;
    }

    // A boosting machine would otherwise catch up with its own shot.
    float carry = VECDotProduct(&md->velocity, &dir);
    float speed = SHOT_SPEED + (carry > 0.0f ? carry : 0.0f);

    WeaponDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.kind = AP_STAR_SHOT_KIND;
    desc.owner_gobj = rd->gobj;
    desc.owner_gobj2 = rd->gobj;
    desc.pos = muzzle;
    desc.forward = dir;
    desc.up = up;
    desc.scale = SHOT_SEED_SCALE; // scale starts here and ShotThink grows it
    desc.vel.X = dir.X * speed;
    desc.vel.Y = dir.Y * speed;
    desc.vel.Z = dir.Z * speed;
    desc.type_flag = 1;
    desc.charge = 1.0f;

    GOBJ *handle = Weapon_Create(&desc);
    if (handle == NULL)
        return;

    // Written before any of the shot's procs run.
    WeaponData *proj = (WeaponData *)handle->userdata;
    ShotState *st = ShotStateOf(proj);
    st->owner_ply = (s8)RiderGObj_GetPly(rd->gobj);
    st->target = -1;
    st->grounded = (u8)grounded;

    GXColor color = ap_star_piece_colors[pod];
    PaintShot(handle, color);
    st->fx = ApStarShotFx_Attach(proj, color, SHOT_RADIUS);

    r->alive_mask &= (u8)~(1 << pod);
    if (r->alive_mask == 0)
    {
        r->regrowing = 1;
        r->regrow_timer = 0;
        // Every pod is at zero scale on this frame, so putting the ring back to
        // its authored spacing here is invisible.
        for (int i = 0; i < AP_STAR_POD_NUM; i++)
        {
            r->shrink[i] = 0;
            r->offset[i] = 0.0f;
            r->target[i] = 0.0f;
        }
        r->spread_mask = 0;
    }
    else
    {
        r->shrink[pod] = POD_SHRINK_FRAMES;
    }
}

static void TryFire(RiderData *rd)
{
    if (!ap_star_settings.shot_enabled || stc_shot_model_block.tree == NULL)
        return;

    GOBJ *mg = rd->machine_gobj;
    if (mg == NULL)
        return;

    MachineData *md = (MachineData *)mg->userdata;
    if (md == NULL || md->is_bike || md->kind != stc_star_slot)
        return;
    if (md->charge_value < FULL_CHARGE)
        return;

    RingState *r = FindRing(md);
    if (r == NULL || r->root == NULL || r->regrowing || r->alive_mask == 0)
        return;

    int pod = NearestPod(r, md);
    if (pod >= 0)
        Fire(rd, md, r, pod);
}

// Both callers of RiderState_StarChargeReleaseEnter (0x801abc64). Call replacements rather than
// a hook on the entry, which has no instruction to displace without losing LR.
static void ApStarShot_ChargeRelease(RiderData *rd)
{
    TryFire(rd);
    RiderState_StarChargeReleaseEnter(rd);
}

void ApStarShot_OnBoot(void)
{
    RegisterShotKind();
    CODEPATCH_REPLACECALL(0x801abc44, ApStarShot_ChargeRelease);
    CODEPATCH_REPLACECALL(0x801abecc, ApStarShot_ChargeRelease);
    ApStarShotFx_OnBoot();
    OSReport("[ApStarShot] Projectile kind %d and charge release hooks installed\n",
             AP_STAR_SHOT_KIND);
}

// A pause runs no Think, so it does not age every ring at once.
void ApStarShot_OnFrameStart(void)
{
    if (stc_thought)
    {
        stc_thought = 0;
        stc_frame++;
    }
}

void ApStarShot_Bind(int kind)
{
    int is_bike;
    stc_star_slot = cm_api->ClassIndexFromKind(kind, &is_bike);
    cm_api->SetInitHandler(kind, OnStarInit);
    cm_api->SetThinkHandler(kind, OnStarThink);
    OSReport("[ApStarShot] Init and Think handlers installed on star slot %d\n", stc_star_slot);
}

// Every ring's joints, the shot model and the FX GObj belong to the scene heap that
// was just reset.
void ApStarShot_OnSceneChange(void)
{
    memset(stc_rings, 0, sizeof(stc_rings));
    stc_shot_model_block.tree = NULL;
    ApStarShotFx_OnSceneChange();
}

void ApStarShot_On3DLoadEnd(void)
{
    if (stc_star_slot < 0)
        return;

    HSD_Archive *arc = NULL;
    Gm_LoadGameFile(&arc, "ApStarShot");
    if (arc != NULL)
        stc_shot_model_block.tree = Archive_GetPublicAddress(arc, "apStarShot_model");

    static int missing_reported;
    if (stc_shot_model_block.tree == NULL && !missing_reported)
    {
        missing_reported = 1;
        OSReport("[ApStarShot] ApStarShot.dat has no apStarShot_model, star shot is off\n");
    }
}

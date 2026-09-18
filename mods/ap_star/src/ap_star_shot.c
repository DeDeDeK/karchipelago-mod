#include <string.h>

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "obj.h"
#include "rider.h"
#include "machine.h"
#include "collision.h"
#include "projectile.h"
#include "code_patch/code_patch.h"

#include "ap_star.h"
#include "ap_star_shot.h"

// PROJKIND_PLASMA_SPREAD_MID with its model swapped for a sphere across the
// synchronous Projectile_Create call: its init and three of its four state
// callbacks are blr, and it carries no per-kind scratch a custom spawn must seed.

#define AP_STAR_POD_NUM   APSTARPIECE_NUM // one per sphere color, in the same order
#define AP_STAR_POD_JOINT 9 // first pod; the six are consecutive in the archive's joint tree

// Machines tracked at once; past this a machine has no ring, and no shot, until one frees.
#define AP_STAR_RING_MAX 32

// The shot model's joint count, which has to match what the archive holds.
#define AP_STAR_SHOT_JOINTS 2

#define SHOT_SPEED      6.3f  // relative to the machine, so this is also its on-screen speed
#define SHOT_LIFETIME   233   // frames; the kind's own default is 120
#define SHOT_GROW_FRAMES 30   // the shot swells to full size over the first of those
#define SHOT_FADE_FRAMES 30   // and shrinks out over the last

// The size the grow starts from and the fade ends at. Not zero: the same field
// drives the hitbox and the render cull, and a shot with no extent at all is a
// degenerate one for a frame.
#define SHOT_SEED_SCALE 0.05f
#define SHOT_PROBE_UP   12.0f // ground probe starts this far above the shot
#define SHOT_PROBE_DOWN 40.0f // and this far below it
#define SHOT_HOVER      3.5f  // ride height over the surface in ground-follow mode

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

static JOBJDesc *stc_shot_model;  // this scene's ApStarShot.dat root
static ProjModelBlock stc_model_block;  // stands in for the kind's own model block

static int stc_star_slot = -1;    // class slot, which is what MachineData.kind holds
static int stc_handlers_installed;

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
    if (md->gobj == NULL)
        return;

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

// Head of the star class's per-kind Init slot. The next Think rebuilds the ring
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

// Both fields are 1 on a fresh shot, so a size goes straight into each. cur_scale
// is re-read every frame for the prio-7 hitbox, the env sweep and the render cull,
// so the damage tracks the sphere.
static void SetShotScale(ProjectileData *proj, float f)
{
    proj->cur_scale = f;

    if (proj->gobj == NULL)
        return;
    JOBJ *root = (JOBJ *)((GOBJ *)proj->gobj)->hsd_object;
    if (root == NULL || root->child == NULL)
        return;

    root->child->scale.X = f;
    root->child->scale.Y = f;
    root->child->scale.Z = f;
    JObj_SetMtxDirtySub(root->child);
}

// The kind's despawn handler destroys the GObj outright, so the shot has to shrink
// out. Prio 0 runs ahead of the lifetime decrement, so a shot with one frame left
// is already down.
static void ShotScaleThink(void *p)
{
    ProjectileData *proj = (ProjectileData *)p;

    if (proj->lifetime <= SHOT_FADE_FRAMES)
    {
        float t = (float)(proj->lifetime - 1) / (float)SHOT_FADE_FRAMES;
        if (t < 0.0f)
            t = 0.0f;
        SetShotScale(proj, SHOT_SEED_SCALE + (1.0f - SHOT_SEED_SCALE) * t);
    }
    else if (proj->frame_counter <= SHOT_GROW_FRAMES)
    {
        float t = (float)proj->frame_counter / (float)SHOT_GROW_FRAMES;
        SetShotScale(proj, SHOT_SEED_SCALE + (1.0f - SHOT_SEED_SCALE) * t);
    }
}

// Prio 7, after the frame's integration and before the HurtData position refresh.
// Over a gap the probe misses and the shot holds its altitude.
static void ShotFollowGround(void *p)
{
    ProjectileData *proj = (ProjectileData *)p;

    Vec3 from = { proj->position.X, proj->position.Y + SHOT_PROBE_UP, proj->position.Z };
    Vec3 to = { proj->position.X, proj->position.Y - SHOT_PROBE_DOWN, proj->position.Z };
    Vec3 hit;

    if (Raycast_Ground(&from, &to, &hit) < 0)
        return;

    proj->position.Y = hit.Y + SHOT_HOVER;
    proj->velocity.Y = 0.0f;
}

// Stands in for the kind's own state fn2 in ground-follow mode. That one bursts the
// shot on a steep environment contact, which a projectile riding the floor trips on
// every rise; only the burst decision is replaced, so the env collision that drives
// coll_data - and with it the yakumono break dispatch - still runs.
static void ShotWallCheck(void *p)
{
    ProjectileData *proj = (ProjectileData *)p;
    Vec3 hit;

    Projectile_UpdateEnvColl(proj);
    if (Raycast_Wall(&proj->position_prev, &proj->position, &hit) >= 0)
        GObj_Destroy(proj->gobj);
}

static void PaintShot(GOBJ *handle, u32 color)
{
    GXColor diffuse;
    diffuse.r = (u8)(color >> 16);
    diffuse.g = (u8)(color >> 8);
    diffuse.b = (u8)color;
    diffuse.a = 0xFF;

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

static GOBJ *CreateShot(ProjectileDesc *desc)
{
    ProjKindData *kd = proj_kind_data[PROJKIND_PLASMA_SPREAD_MID];
    if (kd == NULL || kd->model_desc == NULL)
        return NULL;

    ProjModelBlock *orig = kd->model_desc;
    stc_model_block.tree = stc_shot_model;
    stc_model_block.flags = (orig->flags & 0x00FFFFFF) | (AP_STAR_SHOT_JOINTS << 24);

    kd->model_desc = &stc_model_block;
    GOBJ *handle = Projectile_Create(desc);
    kd->model_desc = orig;
    return handle;
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

    Vec3 vel = { dir.X * speed, dir.Y * speed, dir.Z * speed };

    ProjectileDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.kind = PROJKIND_PLASMA_SPREAD_MID;
    desc.owner_gobj = rd->gobj;
    desc.owner_unk2 = (int)rd->gobj;
    desc.position = muzzle;
    desc.forward = dir;
    desc.up = up;
    desc.velocity_scale = 1.0f;
    desc.velocity = vel;
    desc.type_flag = 1;
    desc.charge = 1.0f;

    GOBJ *handle = CreateShot(&desc);
    if (handle == NULL)
        return;

    ProjectileData *proj = (ProjectileData *)handle->userdata;
    if (proj == NULL)
        return;

    // The kind's post_init has already run its own SetState, which zeroes the
    // hook slots, so these stick. It also rewrites velocity with a muzzle kick of
    // its own; this puts the intended speed back.
    proj->velocity = vel;
    proj->lifetime = SHOT_LIFETIME;
    proj->user_hook_0 = ShotScaleThink;

    // The first prio-0 pass may already have gone by, so the seed size is
    // written here rather than left to the ramp.
    SetShotScale(proj, SHOT_SEED_SCALE);

    if (grounded)
    {
        // The wall sweep runs over last frame's travel, so the first one has to
        // start from the muzzle rather than wherever create left the field.
        proj->position_prev = muzzle;
        proj->state_fn2 = ShotWallCheck;
        proj->user_hook_1 = ShotFollowGround;
    }

    PaintShot(handle, ap_star_piece_colors[pod]);

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
    if (!ap_star_settings.shot_enabled || stc_shot_model == NULL || stc_star_slot < 0)
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

// Both callers of AS_StarChargeRelease (0x801abc64). Call replacements rather than
// a hook on the entry, which has no instruction to displace without losing LR.
static void ApStarShot_ChargeRelease(RiderData *rd)
{
    TryFire(rd);
    AS_StarChargeRelease(rd);
}

void ApStarShot_OnBoot(void)
{
    CODEPATCH_REPLACECALL(0x801abc44, ApStarShot_ChargeRelease);
    CODEPATCH_REPLACECALL(0x801abecc, ApStarShot_ChargeRelease);
    OSReport("[ApStarShot] Charge release hooks installed\n");
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

void ApStarShot_On3DLoadEnd(void)
{
    // Every ring's joints belong to the scene heap that was just torn down.
    memset(stc_rings, 0, sizeof(stc_rings));
    stc_shot_model = NULL;
    stc_star_slot = -1;

    // Both paths repeat every round while they keep failing, so each says it once.
    static int missing_reported;
    static int failure_reported;

    int kind = ApStar_MachineKind();
    if (kind < 0)
    {
        if (!missing_reported)
        {
            missing_reported = 1;
            OSReport("[ApStarShot] %s is not registered, star shot is off\n",
                     AP_STAR_MACHINE_NAME);
        }
        return;
    }

    int is_bike = 0;
    stc_star_slot = ApStar_ClassIndex(&is_bike);

    HSD_Archive *arc = NULL;
    Gm_LoadGameFile(&arc, "ApStarShot");
    if (arc != NULL)
        stc_shot_model = Archive_GetPublicAddress(arc, "apStarShot_model");

    if (!stc_handlers_installed)
    {
        stc_handlers_installed = cm_api->SetInitHandler(kind, OnStarInit) &&
                                 cm_api->SetThinkHandler(kind, OnStarThink);
        // Reported on the install that succeeds, whichever round that is; the
        // failure line latches so a permanent failure says so once.
        if (stc_handlers_installed)
        {
            OSReport("[ApStarShot] %s star slot %d, model %s, handlers installed\n",
                     AP_STAR_MACHINE_NAME, stc_star_slot,
                     stc_shot_model != NULL ? "ready" : "unavailable");
        }
        else if (!failure_reported)
        {
            failure_reported = 1;
            OSReport("[ApStarShot] %s handler slots unavailable, star shot is off\n",
                     AP_STAR_MACHINE_NAME);
        }
    }
}

#include <string.h>

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "obj.h"
#include "rider.h"
#include "machine.h"
#include "collision.h"
#include "weapon.h"
#include "inline.h"
#include "code_patch/code_patch.h"

#include "ap_star.h"
#include "ap_star_ring.h"
#include "ap_star_shot.h"
#include "ap_star_shot_fx.h"

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

// charge_value is clamped to 1.0 as it fills; the margin is for the float.
#define FULL_CHARGE 0.99f

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

// Down from just above `at` to well below it. 0 over a gap.
static int ProbeGround(const Vec3 *at, Vec3 *hit)
{
    Vec3 from = { at->X, at->Y + SHOT_PROBE_UP, at->Z };
    Vec3 to = { at->X, at->Y - SHOT_PROBE_DOWN, at->Z };
    return Raycast_Ground(&from, &to, hit) >= 0;
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

    VECNormalize(to, to);
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
        proj->scale = lerp(SHOT_SEED_SCALE, 1.0f, t);
    }
    else if (proj->frame_counter <= SHOT_GROW_FRAMES)
    {
        float t = (float)proj->frame_counter / (float)SHOT_GROW_FRAMES;
        proj->scale = lerp(SHOT_SEED_SCALE, 1.0f, t);
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

    Vec3 hit;
    if (!ProbeGround(&proj->pos, &hit))
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
    { 0x8021f4b4, 0x8021f4c4 }, // Weapon_Create (0x8021f428), state table
    { 0x8021f64c, 0x8021f650 }, // Weapon_Create, init
    { 0x8021f790, 0x8021f794 }, // Weapon_Create, post_init
    { 0x8021fde0, 0x8021fde4 }, // Weapon_Proc10_HitReact (0x8021fcd4)
    { 0x8021ff94, 0x8021ff98 }, // Weapon_UserDataDtor (0x8021ff54)
    { 0x8022036c, 0x80220374 }, // Weapon_Despawn (0x80220364)
    { 0x802205f0, 0x802205f8 }, // Weapon_LoadKindParams (0x802205e8)
    { 0x8022065c, 0x80220664 }, // Weapon_ReloadKindParams (0x80220654), load_render_state
    { 0x802206bc, 0x802206c0 }, // Weapon_ReloadKindParams, reset_render_state
};

static void RegisterShotKind(void)
{
    memcpy(stc_vtables, wp_kind_vtables, WPKIND_NUM * sizeof(stc_vtables[0]));
    stc_vtables[AP_STAR_SHOT_KIND] = &stc_shot_vtable;

    for (u32 i = 0; i < GetElementsIn(stc_vtable_sites); i++)
        CODEPATCH_REPLACEADDRESS(stc_vtable_sites[i][0], stc_vtable_sites[i][1], stc_vtables);

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

// Returns 0 with no shot made, so the pod stays on the ring.
static int Fire(RiderData *rd, MachineData *md, int pod, const Vec3 *muzzle)
{
    Vec3 ground;
    int grounded = ProbeGround(muzzle, &ground);

    Vec3 dir, up;
    if (grounded)
    {
        dir = (Vec3){ md->forward.X, 0.0f, md->forward.Z };
        up = (Vec3){ 0.0f, 1.0f, 0.0f };
        if (VECSquareMag(&dir) < 0.0001f)
            return 0;
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

    WeaponDesc desc = {
        .kind = AP_STAR_SHOT_KIND,
        .owner_gobj = rd->gobj,
        .owner_gobj2 = rd->gobj,
        .pos = *muzzle,
        .forward = dir,
        .up = up,
        .scale = SHOT_SEED_SCALE, // scale starts here and ShotThink grows it
        .vel = { dir.X * speed, dir.Y * speed, dir.Z * speed },
        .type_flag = 1,
        .charge = 1.0f,
    };
    GOBJ *handle = Weapon_Create(&desc);
    if (handle == NULL)
        return 0;

    // Written before any of the shot's procs run.
    WeaponData *proj = (WeaponData *)handle->userdata;
    ShotState *st = ShotStateOf(proj);
    st->owner_ply = (s8)rd->ply;
    st->target = -1;
    st->grounded = (u8)grounded;

    GXColor color = ap_star_piece_colors[pod];
    PaintShot(handle, color);
    st->fx = ApStarShotFx_Attach(proj, color, SHOT_RADIUS);
    return 1;
}

static void TryFire(RiderData *rd)
{
    if (!ap_star_settings.shot_enabled || stc_shot_model_block.tree == NULL)
        return;

    GOBJ *mg = rd->machine_gobj;
    MachineData *md = mg != NULL ? (MachineData *)mg->userdata : NULL;
    if (md == NULL || md->charge_value < FULL_CHARGE)
        return;

    Vec3 muzzle;
    int pod = ApStarRing_AimPod(md, &muzzle);
    if (pod >= 0 && Fire(rd, md, pod, &muzzle))
        ApStarRing_Spend(md, pod);
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
    CODEPATCH_REPLACECALL(0x801abc44, ApStarShot_ChargeRelease); // in Rider_IASACheck_ChargeRelease (0x801abc2c)
    CODEPATCH_REPLACECALL(0x801abecc, ApStarShot_ChargeRelease); // in AS_StarChargeFullThink (0x801abea0)
    ApStarShotFx_OnBoot();
    OSReport("[ApStarShot] Projectile kind %d and charge release hooks installed\n",
             AP_STAR_SHOT_KIND);
}

// The shot model and the FX GObj belong to the scene heap that was just reset.
void ApStarShot_OnSceneChange(void)
{
    stc_shot_model_block.tree = NULL;
    ApStarShotFx_OnSceneChange();
}

void ApStarShot_On3DLoadEnd(void)
{
    if (ApStar_MachineKind() < 0)
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

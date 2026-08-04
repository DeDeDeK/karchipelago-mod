#include "game.h"
#include "os.h"
#include "machine.h"
#include "rider.h"
#include "collision.h"
#include "obj.h"
#include "code_patch/code_patch.h"

#include "event_scale_change.h"

// How small players shrink to. 0.5 = half size -> the world feels ~2x bigger.
#define SCALE_TARGET_FACTOR 0.5f

// Per-frame ease step; 0.02 covers the 0.5 swing in ~25 frames.
#define SCALE_EASE_STEP 0.02f

// City Trial player slots.
#define SCALE_MAX_PLAYERS 4

// A single-frame position jump above this many times top_speed_current is a
// teleport (respawn / warp) and passes through unscaled, so the machine lands on
// its target instead of being clamped halfway there.
#define SCALE_TELEPORT_SPEED_MULT 5.0f

// Each lever is independently toggleable so one can be isolated when tuning live.
#define SCALE_AFFECTS_RIDER_MODEL   1
#define SCALE_AFFECTS_MACHINE_MODEL 1
#define SCALE_AFFECTS_COLLISION     1
#define SCALE_AFFECTS_SPEED         1
#define SCALE_AFFECTS_CAMERA        1

// The bl CObj_SetEyePosition inside PlyCam_Think. The instruction before it is
// bl CObj_SetInterest (0x800b38f4) on the same COBJ, so by the time the shim
// runs the COBJ already holds this frame's interest.
#define SCALE_PLYCAM_SETEYE_CALL 0x800b3900

// Captured on first touch of a slot's machine GObj, re-captured when that GObj
// changes, so restore is exact regardless of vehicle type.
typedef struct SlotScale
{
    GOBJ *machine;     // machine GObj these originals belong to (NULL = empty)
    float cd_radius;   // CollData.radius (+0x344)
    float sd_radius;   // CollShapeData.radius (+0x30)
    float sd_radius2;  // CollShapeData.radius2 (+0x34) - lerp endpoint
    Vec3 last_pos;     // machine pos we left it at last frame (for the speed clamp)
    int tracking;      // last_pos is seeded (0 = re-seed this frame, no clamp)
    int captured;
} SlotScale;

static SlotScale slot[SCALE_MAX_PLAYERS];
static int scale_active;
static float cur_factor;

// Dolly the eye along the eye->interest line by cur_factor so the follow
// distance shrinks with the players; view direction, up and FOV are untouched.
// A pure passthrough while the event is idle.
static void ScaleChange_CObjSetEyePosition(COBJ *cobj, Vec3 *eye)
{
#if SCALE_AFFECTS_CAMERA
    if (scale_active && cur_factor < 1.0f && cobj && cobj->interest)
    {
        Vec3 interest = cobj->interest->pos;
        Vec3 scaled;
        scaled.X = interest.X + (eye->X - interest.X) * cur_factor;
        scaled.Y = interest.Y + (eye->Y - interest.Y) * cur_factor;
        scaled.Z = interest.Z + (eye->Z - interest.Z) * cur_factor;
        CObj_SetEyePosition(cobj, &scaled);
        return;
    }
#endif
    CObj_SetEyePosition(cobj, eye);
}

static float ApproachFactor(float cur, float target, float step)
{
    if (cur < target)
    {
        cur += step;
        if (cur > target)
            cur = target;
    }
    else if (cur > target)
    {
        cur -= step;
        if (cur < target)
            cur = target;
    }
    return cur;
}

// Keep only `factor` of the machine's per-frame world displacement by pulling
// its position back after the engine has integrated velocity and resolved
// collision, so no frame ends in a penetrating state. factor 1.0 is a no-op.
static void ApplyMachineSpeed(SlotScale *s, MachineData *md, float factor)
{
    Vec3 cur = md->pos;

    if (!s->tracking)
    {
        s->last_pos = cur;
        s->tracking = 1;
        return;
    }

    float dx = cur.X - s->last_pos.X;
    float dy = cur.Y - s->last_pos.Y;
    float dz = cur.Z - s->last_pos.Z;

    float maxstep = md->top_speed_current * SCALE_TELEPORT_SPEED_MULT;
    if (maxstep > 1.0f && dx * dx + dy * dy + dz * dz > maxstep * maxstep)
    {
        // Teleport: let it through, re-seed from the destination.
        s->last_pos = cur;
        return;
    }

    Vec3 clamped;
    clamped.X = s->last_pos.X + dx * factor;
    clamped.Y = s->last_pos.Y + dy * factor;
    clamped.Z = s->last_pos.Z + dz * factor;
    md->pos = clamped;
    s->last_pos = clamped;
}

// factor 1.0 restores the captured collision originals exactly. model_scale
// needs no capture - it is a "1.0 = normal" multiplier, with the vehicle's
// intrinsic size held in model_scale_base.
static void ApplyMachineScale(int ply, GOBJ *mg, MachineData *md, float factor)
{
    SlotScale *s = &slot[ply];
    CollData *cd = md->coll_data;
    struct CollShapeData *sd = cd ? cd->shape_data : NULL;

    if (s->machine != mg || !s->captured)
    {
        s->machine = mg;
        s->cd_radius = cd ? cd->radius : 0.0f;
        s->sd_radius = sd ? sd->radius : 0.0f;
        s->sd_radius2 = sd ? sd->radius2 : 0.0f;
        s->tracking = 0; // re-seed the speed clamp from this machine's position
        s->captured = 1;
    }

#if SCALE_AFFECTS_COLLISION
    if (cd)
        cd->radius = s->cd_radius * factor;
    if (sd)
    {
        sd->radius = s->sd_radius * factor;
        sd->radius2 = s->sd_radius2 * factor;
    }
#endif

#if SCALE_AFFECTS_MACHINE_MODEL
    md->model_scale = factor;
#endif

#if SCALE_AFFECTS_SPEED
    ApplyMachineSpeed(s, md, factor);
#endif
}

// Drive every active player to the given factor (1.0 = normal size/speed).
static void ApplyScale(float factor)
{
    for (int ply = 0; ply < SCALE_MAX_PLAYERS; ply++)
    {
#if SCALE_AFFECTS_RIDER_MODEL
        GOBJ *rg = Ply_GetRiderGObj(ply);
        if (rg)
        {
            RiderData *rd = rg->userdata;
            rd->model_scale = factor;
        }
#endif

        GOBJ *mg = Ply_GetMachineGObj(ply);
        if (!mg)
        {
            // Drop the capture so a future mount re-captures fresh originals.
            slot[ply].machine = NULL;
            slot[ply].captured = 0;
            continue;
        }

        ApplyMachineScale(ply, mg, mg->userdata, factor);
    }
}

void ScaleChange_Start(EventCheckData *ev_chk)
{
    scale_active = 1;
    cur_factor = 1.0f;

    for (int i = 0; i < SCALE_MAX_PLAYERS; i++)
    {
        slot[i].machine = NULL;
        slot[i].captured = 0;
    }

    OSReport("[ScaleChange] start: shrinking players to %d/1000\n",
             (int)(SCALE_TARGET_FACTOR * 1000.0f));
}

void ScaleChange_Active(EventCheckData *ev_chk)
{
    if (!scale_active)
        return;

    cur_factor = ApproachFactor(cur_factor, SCALE_TARGET_FACTOR, SCALE_EASE_STEP);
    ApplyScale(cur_factor);
}

void ScaleChange_End(EventCheckData *ev_chk)
{
    if (!scale_active)
        return;

    // Ease back to normal during the cleanup phase.
    cur_factor = ApproachFactor(cur_factor, 1.0f, SCALE_EASE_STEP);
    ApplyScale(cur_factor);
}

void ScaleChange_End2(EventCheckData *ev_chk)
{
    if (!scale_active)
        return;

    // Exact restore: writes the captured collision originals back and makes the
    // speed clamp a no-op.
    ApplyScale(1.0f);

    scale_active = 0;
    cur_factor = 1.0f;
    OSReport("[ScaleChange] restored players to normal scale\n");
}

void ScaleChange_InstallHooks(void)
{
#if SCALE_AFFECTS_CAMERA
    CODEPATCH_REPLACECALL(SCALE_PLYCAM_SETEYE_CALL, ScaleChange_CObjSetEyePosition);
#endif
}

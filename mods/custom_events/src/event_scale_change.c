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

// Per-frame ease step. (1.0 - target) / step frames to fully ease; 0.02 over a
// 0.5 swing is ~25 frames (~0.4s at 60fps), applied at both start and end.
#define SCALE_EASE_STEP 0.02f

// City Trial player slots.
#define SCALE_MAX_PLAYERS 4

// A single-frame position jump larger than this many times the machine's own top
// speed is a teleport (respawn / warp), not driving - we pass it straight through
// unscaled so the machine lands on its target instead of being clamped to half
// way there. Keyed off top_speed_current so it is unit-agnostic; 5x leaves plenty
// of headroom for fast falls / knockback while staying far below any real warp.
#define SCALE_TELEPORT_SPEED_MULT 5.0f

// Levers, each independently toggleable so a single one can be isolated when
// tuning live. The rider- and machine-model levers both write a per-object
// `model_scale` float the engine bakes into the model's user matrix every frame
// (Rider_ApplyModelMatrix / the machine model appliers around 0x801c9074), the
// collision lever writes the mpColl sphere radius, and the speed lever scales the
// machine's per-frame world displacement.
#define SCALE_AFFECTS_RIDER_MODEL   1
#define SCALE_AFFECTS_MACHINE_MODEL 1
#define SCALE_AFFECTS_COLLISION     1
#define SCALE_AFFECTS_SPEED         1
#define SCALE_AFFECTS_CAMERA        1

// The bl CObj_SetEyePosition inside PlyCam_Think we replace with our distance
// shim. PlyCam_Think sets the interest on the same COBJ the instruction before
// (bl CObj_SetInterest @ 0x800b38f4), so by the time our shim runs the COBJ's
// interest is already this frame's value and we read it straight back.
#define SCALE_PLYCAM_SETEYE_CALL 0x800b3900

// Per-slot captured originals, so restore is exact regardless of vehicle type
// and survives machine swaps. Captured the first frame we touch a given machine
// GObj; re-captured when the slot's machine changes.
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

// Replacement for the bl CObj_SetEyePosition inside PlyCam_Think: pull the final
// camera eye toward its interest by cur_factor, so the follow distance shrinks in
// lockstep with the players. The interest was set on this same COBJ one call
// earlier, so we read it back from the COBJ and move the eye along the
// eye->interest line - distance changes, view direction / up / FOV do not. When
// the event is idle (the overwhelmingly common case) this is a pure passthrough,
// so every non-event camera in every mode is unaffected. Runs for all player
// cameras while active, matching the world-wide (all-players) nature of the event.
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

// Ease cur toward target by at most `step`.
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

// Keep only `factor` of the machine's per-frame world displacement, so the player
// travels across the unchanged world at a tiny-appropriate pace. We pull the
// machine's position back by (1 - factor) of the distance it moved since our last
// pass; the engine's velocity integration and collision resolution run normally
// around this, so the machine never ends a frame in a penetrating state and the
// rider/camera follow the machine position with no desync. factor 1.0 is an exact
// no-op (clamped == cur).
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
        // Teleport (respawn / warp): let it through, re-seed from the destination.
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

// Capture this slot's collision originals the first time we see its current
// machine, then write the scaled values. factor 1.0 restores them exactly. The
// model scale needs no capture: model_scale is a "1.0 = normal" multiplier (the
// vehicle's intrinsic size lives in model_scale_base), so writing the factor
// directly is the machine analogue of rd->model_scale = factor for the rider.
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
            // No machine in this slot - drop its capture so a future mount
            // re-captures fresh originals.
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

    // Final exact restore: factor 1.0 writes the captured collision originals back
    // and makes the speed clamp a no-op.
    ApplyScale(1.0f);

    scale_active = 0;
    cur_factor = 1.0f;
    OSReport("[ScaleChange] restored players to normal scale\n");
}

// Install the persistent code patches this event needs. Called once at boot.
void ScaleChange_InstallHooks(void)
{
#if SCALE_AFFECTS_CAMERA
    // Route the player camera's per-frame eye write through our distance shim.
    CODEPATCH_REPLACECALL(SCALE_PLYCAM_SETEYE_CALL, ScaleChange_CObjSetEyePosition);
#endif
}

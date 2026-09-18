#include "game.h"
#include "os.h"
#include "machine.h"
#include "rider.h"
#include "collision.h"
#include "obj.h"
#include "code_patch/code_patch.h"

#include "event_scale_change.h"

// Player size, and with it the world-speed and camera-distance factor.
#define SCALE_TARGET_FACTOR 0.5f
#define SCALE_EASE_STEP 0.02f
#define SCALE_MAX_PLAYERS 4

// A one-frame jump above this many times top_speed_current is a respawn or warp,
// passed through unscaled so the machine lands on its target.
#define SCALE_TELEPORT_SPEED_MULT 5.0f

// bl CObj_SetEyePosition in PlyCam_Think (0x800b3540), one call after
// CObj_SetInterest on the same COBJ.
#define SCALE_PLYCAM_SETEYE_CALL 0x800b3900

// The machine a player slot is shrinking, with its unscaled collision radii.
typedef struct SlotScale
{
    GOBJ *machine; // NULL = nothing shrunk
    float radius;  // CollShapeData.radius
    float radius2; // CollShapeData.radius2
    Vec3 last_pos; // position left last frame, for the speed clamp
    int tracking;  // last_pos is seeded
} SlotScale;

static SlotScale slot[SCALE_MAX_PLAYERS];
static float cur_factor = 1.0f;

// Dollies the eye toward the interest by cur_factor so the follow distance shrinks
// with the players; view direction, up and FOV are untouched.
static void ScaleChange_CObjSetEyePosition(COBJ *cobj, Vec3 *eye)
{
    if (cur_factor < 1.0f)
    {
        Vec3 interest = cobj->interest->pos;
        Vec3 scaled;
        scaled.X = interest.X + (eye->X - interest.X) * cur_factor;
        scaled.Y = interest.Y + (eye->Y - interest.Y) * cur_factor;
        scaled.Z = interest.Z + (eye->Z - interest.Z) * cur_factor;
        CObj_SetEyePosition(cobj, &scaled);
        return;
    }
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

static int IsMachineLive(GOBJ *mg)
{
    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_MACHINE]; g; g = g->next)
    {
        if (g == mg)
            return 1;
    }
    return 0;
}

// Returns the slot's machine to full size, unless it was destroyed in the meantime.
static void RestoreMachine(SlotScale *s)
{
    if (s->machine && IsMachineLive(s->machine))
    {
        MachineData *md = s->machine->userdata;
        struct CollShapeData *sd = md->coll_data->shape_data;
        md->model_scale = md->model_scale_default;
        sd->radius = s->radius;
        sd->radius2 = s->radius2;
    }
    s->machine = NULL;
}

// Keeps only `factor` of the machine's displacement since the last pass by pulling
// its position back toward last_pos. The event proc runs before machine physics, so
// the engine integrates and resolves collision from the pulled-back position.
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

static void ApplyMachineScale(SlotScale *s, GOBJ *mg, float factor)
{
    MachineData *md = mg->userdata;
    struct CollShapeData *sd = md->coll_data->shape_data;

    // A machine the player left is restored on the spot, so a later mount captures
    // true originals.
    if (s->machine != mg)
    {
        RestoreMachine(s);
        s->machine = mg;
        s->radius = sd->radius;
        s->radius2 = sd->radius2;
        s->tracking = 0;
    }

    // mpColl_Update rewrites CollData.radius every frame, and radius2 in ground
    // states, from the machine's base values, so only these two writes hold.
    sd->radius = s->radius * factor;
    sd->radius2 = s->radius2 * factor;
    md->model_scale = md->model_scale_default * factor;
    ApplyMachineSpeed(s, md, factor);
}

static void ApplyScale(float factor)
{
    for (int ply = 0; ply < SCALE_MAX_PLAYERS; ply++)
    {
        GOBJ *rg = Ply_GetRiderGObj(ply);
        if (rg)
            ((RiderData *)rg->userdata)->model_scale = factor;

        GOBJ *mg = Ply_GetMachineGObj(ply);
        if (mg)
            ApplyMachineScale(&slot[ply], mg, factor);
        else
            RestoreMachine(&slot[ply]);
    }
}

void ScaleChange_Start(void)
{
    cur_factor = 1.0f;
    OSReport("[ScaleChange] Started, players shrink to %d%%\n", (int)(SCALE_TARGET_FACTOR * 100.0f));
}

void ScaleChange_Active(void)
{
    cur_factor = ApproachFactor(cur_factor, SCALE_TARGET_FACTOR, SCALE_EASE_STEP);
    ApplyScale(cur_factor);
}

void ScaleChange_End(void)
{
    cur_factor = ApproachFactor(cur_factor, 1.0f, SCALE_EASE_STEP);
    ApplyScale(cur_factor);
}

void ScaleChange_End2(void)
{
    cur_factor = 1.0f;
    for (int ply = 0; ply < SCALE_MAX_PLAYERS; ply++)
    {
        GOBJ *rg = Ply_GetRiderGObj(ply);
        if (rg)
            ((RiderData *)rg->userdata)->model_scale = 1.0f;
        RestoreMachine(&slot[ply]);
    }
    OSReport("[ScaleChange] Players restored to normal scale\n");
}

// Riders and machines go away with the scene; only the camera shim outlives it.
void ScaleChange_Abort(void)
{
    cur_factor = 1.0f;
    for (int ply = 0; ply < SCALE_MAX_PLAYERS; ply++)
        slot[ply].machine = NULL;
}

void ScaleChange_InstallHooks(void)
{
    CODEPATCH_REPLACECALL(SCALE_PLYCAM_SETEYE_CALL, ScaleChange_CObjSetEyePosition);
}

#include <string.h>

#include "game.h"
#include "machine.h"
#include "rider.h"
#include "collision.h"
#include "obj.h"
#include "camera.h"
#include "code_patch/code_patch.h"

#include "custom_events.h"
#include "player_scale.h"

// A collision sphere being scaled, with the unscaled shape the game last gave it.
typedef struct CollScale
{
    CollData *cd; // NULL = nothing scaled
    float radius;
    float radius2;
    Vec3 offset;
    Vec3 probe[COLL_PROBE_NUM];
    float factor;  // factor last written
    float written; // radius last written; any other value there means mpColl_Init ran since
} CollScale;

typedef struct SlotScale
{
    GOBJ *machine; // NULL = no machine scaled
    CollScale machine_coll;
    CollScale rider_coll;
} SlotScale;

static SlotScale slot[PLY_NUM];
static float cur_camera_factor = 1.0f;

float PlayerScale_Ease(float cur, float target, float step)
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

static void CollScale_Capture(CollScale *s, CollData *cd)
{
    struct CollShapeData *sd = cd->shape_data;

    s->cd = cd;
    s->radius = sd->radius;
    s->radius2 = sd->radius2;
    s->offset = sd->offset;
    if (sd->probes)
    {
        for (int i = 0; i < COLL_PROBE_NUM; i++)
            s->probe[i] = sd->probes[i].offset;
    }
}

// The offset and probes scale with the radius, so the whole body scales about the
// owner's position, the point its model scales about.
static void CollScale_Write(CollScale *s, float factor)
{
    struct CollShapeData *sd = s->cd->shape_data;

    sd->radius = s->radius * factor;
    sd->radius2 = s->radius2 * factor;
    VECScale(&s->offset, &sd->offset, factor);
    if (sd->probes)
    {
        for (int i = 0; i < COLL_PROBE_NUM; i++)
            VECScale(&s->probe[i], &sd->probes[i].offset, factor);
    }
    s->factor = factor;
    s->written = sd->radius;
}

static int CollScale_IsCurrent(CollScale *s)
{
    return s->cd->shape_data->radius == s->written;
}

static void CollScale_Apply(CollScale *s, CollData *cd, float factor)
{
    if (s->cd != cd || !CollScale_IsCurrent(s))
        CollScale_Capture(s, cd);
    CollScale_Write(s, factor);
}

// A sphere mpColl_Init has rewritten since is already unscaled.
static void CollScale_Restore(CollScale *s)
{
    if (s->cd && CollScale_IsCurrent(s))
        CollScale_Write(s, 1.0f);
    s->cd = NULL;
}

static CollScale *FindCollScale(CollData *cd)
{
    for (int ply = 0; ply < PLY_NUM; ply++)
    {
        if (slot[ply].machine_coll.cd == cd)
            return &slot[ply].machine_coll;
        if (slot[ply].rider_coll.cd == cd)
            return &slot[ply].rider_coll;
    }
    return NULL;
}

// Hook at 0x802460a0 in mpColl_Update (0x80245f70), the store of a radius2 other than
// -1; r28 = CollData, f31 = radius2. Grounded states pass their unscaled radius here
// every frame, after the event proc has scaled the shape.
static float PlayerScale_ScaleRadius2(CollData *cd, float radius2)
{
    CollScale *s = FindCollScale(cd);
    if (!s || !CollScale_IsCurrent(s))
        return radius2;

    s->radius2 = radius2;
    return radius2 * s->factor;
}

CODEPATCH_HOOKCREATE(0x802460a0,
                     "mr 3, 28\n\t"
                     "fmr 1, 31\n\t",
                     PlayerScale_ScaleRadius2,
                     "fmr 31, 1\n\t",
                     0)

static Vec3 ScaleAbout(Vec3 v, Vec3 center, float f)
{
    return (Vec3){
        center.X + (v.X - center.X) * f,
        center.Y + (v.Y - center.Y) * f,
        center.Z + (v.Z - center.Z) * f,
    };
}

// Hook at 0x800b38e8 in PlyCamGObj_Think (0x800b3540), after the whole camera pipeline
// and just before the interest and eye go to CObj_SetInterest and CObj_SetEyePosition;
// r28 = PlayerCamData, r1+0x38 = interest, r31 = eye. Scaling both about the tracked
// body keeps it where it normally sits in frame; view direction, up and FOV are
// untouched.
static void PlayerScale_ScaleCamera(PlayerCamData *pcd, Vec3 *interest, Vec3 *eye)
{
    CamInterest *target = pcd->cam_data->target;
    if (cur_camera_factor == 1.0f || !target)
        return;

    *interest = ScaleAbout(*interest, target->pos, cur_camera_factor);
    *eye = ScaleAbout(*eye, target->pos, cur_camera_factor);
}

CODEPATCH_HOOKCREATE(0x800b38e8,
                     "mr 3, 28\n\t"
                     "addi 4, 1, 0x38\n\t"
                     "mr 5, 31\n\t",
                     PlayerScale_ScaleCamera,
                     "",
                     0)

// Returns the slot's machine to full size, unless it was destroyed in the meantime.
static void RestoreMachine(SlotScale *s)
{
    if (s->machine && CustomEvents_IsOnPLink(s->machine, GAMEPLINK_MACHINE))
    {
        MachineData *md = s->machine->userdata;
        md->model_scale = md->model_scale_default;
        CollScale_Restore(&s->machine_coll);
    }
    s->machine = NULL;
    s->machine_coll.cd = NULL;
}

void PlayerScale_Apply(float factor, float camera_factor)
{
    cur_camera_factor = camera_factor;

    for (int ply = 0; ply < PLY_NUM; ply++)
    {
        SlotScale *s = &slot[ply];

        GOBJ *rg = Ply_GetRiderGObj(ply);
        if (rg)
        {
            RiderData *rd = rg->userdata;
            rd->model_scale = factor;
            if (rd->coll_data)
                CollScale_Apply(&s->rider_coll, rd->coll_data, factor);
        }

        // A machine the player left is restored on the spot, so a later mount captures
        // its true size.
        GOBJ *mg = Ply_GetMachineGObj(ply);
        if (mg != s->machine)
        {
            RestoreMachine(s);
            s->machine = mg;
        }
        if (mg)
        {
            MachineData *md = mg->userdata;
            md->model_scale = md->model_scale_default * factor;
            CollScale_Apply(&s->machine_coll, md->coll_data, factor);
        }
    }
}

void PlayerScale_Restore(void)
{
    cur_camera_factor = 1.0f;

    for (int ply = 0; ply < PLY_NUM; ply++)
    {
        GOBJ *rg = Ply_GetRiderGObj(ply);
        if (rg)
            ((RiderData *)rg->userdata)->model_scale = 1.0f;
        CollScale_Restore(&slot[ply].rider_coll);
        RestoreMachine(&slot[ply]);
    }
}

// The next scene's pooled CollData can reuse a dropped pointer, and PlyCamGObj_Think
// runs in every minor-18 scene, so both hooks have to see nothing scaled.
void PlayerScale_Forget(void)
{
    cur_camera_factor = 1.0f;
    memset(slot, 0, sizeof(slot));
}

void PlayerScale_InstallHooks(void)
{
    CODEPATCH_HOOKAPPLY(0x802460a0);
    CODEPATCH_HOOKAPPLY(0x800b38e8);
}

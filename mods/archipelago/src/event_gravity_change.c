// Gravity Change — custom City Trial event
//
// Modifies the stage gravity vector for the duration of the event.
// The gravity force vector lives in GrData.stage_node->gravity_force
// and is read by the machine physics system each frame.
//
// On start: save original gravity, apply scaled gravity.
// Active:   re-apply each frame (insurance against anything restoring it).
// End:      restore original gravity.

#include "game.h"
#include "os.h"
#include "stage.h"

#include "event_gravity_change.h"

// Gravity multiplier: <1.0 = low gravity, >1.0 = high gravity
#define GRAVITY_MULTIPLIER 0.2f

static Vec3 original_gravity;
static int gravity_modified;

static Vec3 *GetStageGravity(void)
{
    GrObj *grobj = *stc_grobj;
    if (!grobj || !grobj->gr_data || !grobj->gr_data->stage_node)
        return NULL;
    return &grobj->gr_data->stage_node->gravity_force;
}

void GravityChange_Start(EventCheckData *ev_chk)
{
    Vec3 *grav = GetStageGravity();
    if (!grav)
    {
        OSReport("[GravityChange] stage_node gravity not available\n");
        gravity_modified = 0;
        return;
    }

    // Save original
    original_gravity = *grav;

    // Apply scaled gravity
    grav->X *= GRAVITY_MULTIPLIER;
    grav->Y *= GRAVITY_MULTIPLIER;
    grav->Z *= GRAVITY_MULTIPLIER;
    gravity_modified = 1;

    OSReport("[GravityChange] start: original=(%.4f, %.4f, %.4f) scaled by %.2f\n",
             original_gravity.X, original_gravity.Y, original_gravity.Z,
             GRAVITY_MULTIPLIER);
}

void GravityChange_Active(EventCheckData *ev_chk)
{
    if (!gravity_modified)
        return;

    // Re-apply each frame in case something overwrites it
    Vec3 *grav = GetStageGravity();
    if (!grav)
        return;

    grav->X = original_gravity.X * GRAVITY_MULTIPLIER;
    grav->Y = original_gravity.Y * GRAVITY_MULTIPLIER;
    grav->Z = original_gravity.Z * GRAVITY_MULTIPLIER;
}

void GravityChange_End2(EventCheckData *ev_chk)
{
    if (!gravity_modified)
        return;

    Vec3 *grav = GetStageGravity();
    if (grav)
    {
        *grav = original_gravity;
        OSReport("[GravityChange] restored original gravity\n");
    }

    gravity_modified = 0;
}

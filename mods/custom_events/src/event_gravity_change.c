#include "game.h"
#include "os.h"
#include "stage.h"

#include "event_gravity_change.h"

// Gravity multipliers applied to the stage's base strength (City Trial = 0.025).
// One is chosen at random each trigger: low = floaty/long air time, high = heavy.
#define GRAVITY_MULT_LOW   0.5f
#define GRAVITY_MULT_HIGH  2.0f

static float original_strength;
static float active_mult;
static int gravity_modified;

static float *GetStageGravityStrength(void)
{
    GrObj *grobj = *stc_grobj;
    if (!grobj || !grobj->gr_data || !grobj->gr_data->stage_node)
        return NULL;
    return &grobj->gr_data->stage_node->gravity_strength;
}

void GravityChange_Start(EventCheckData *ev_chk)
{
    float *strength = GetStageGravityStrength();
    if (!strength)
    {
        OSReport("[GravityChange] stage_node gravity not available\n");
        gravity_modified = 0;
        return;
    }

    // Randomly pick low or high gravity for this trigger.
    active_mult = HSD_Randi(2) ? GRAVITY_MULT_HIGH : GRAVITY_MULT_LOW;

    original_strength = *strength;
    *strength = original_strength * active_mult;
    gravity_modified = 1;

    OSReport("[GravityChange] start: strength %.4f -> %.4f (x%.2f)\n",
             original_strength, *strength, active_mult);
}

void GravityChange_Active(EventCheckData *ev_chk)
{
    if (!gravity_modified)
        return;

    // Re-apply each frame in case something overwrites it.
    float *strength = GetStageGravityStrength();
    if (strength)
        *strength = original_strength * active_mult;
}

void GravityChange_End2(EventCheckData *ev_chk)
{
    if (!gravity_modified)
        return;

    float *strength = GetStageGravityStrength();
    if (strength)
    {
        *strength = original_strength;
        OSReport("[GravityChange] restored gravity strength %.4f\n", original_strength);
    }

    gravity_modified = 0;
}

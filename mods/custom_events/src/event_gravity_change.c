#include "game.h"
#include "os.h"
#include "hsd.h"
#include "stage.h"

#include "event_gravity_change.h"

// Multipliers on the stage's base strength (City Trial = 0.025), one picked per trigger.
#define GRAVITY_MULT_LOW  0.5f
#define GRAVITY_MULT_HIGH 2.0f

// The scaled StageNode field while the event runs, NULL otherwise. Nothing in the
// game writes it at runtime, so a single write holds for the whole event.
static float *scaled_strength;
static float original_strength;

void GravityChange_Start(void)
{
    float *strength = &(*stc_grobj)->gr_data->stage_node->gravity_strength;
    float mult = HSD_Randi(2) ? GRAVITY_MULT_HIGH : GRAVITY_MULT_LOW;

    original_strength = *strength;
    *strength = original_strength * mult;
    scaled_strength = strength;

    OSReport("[GravityChange] Strength scaled %.4f -> %.4f\n", original_strength, *strength);
}

void GravityChange_End2(void)
{
    if (!scaled_strength)
        return;

    *scaled_strength = original_strength;
    scaled_strength = NULL;

    OSReport("[GravityChange] Strength restored to %.4f\n", original_strength);
}

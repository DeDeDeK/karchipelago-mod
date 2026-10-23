#include "game.h"
#include "rider.h"
#include "machine.h"
#include "collision.h"
#include "os.h"
#include "inline.h"

#include "cpu_glide.h"

// The predictor steps the glide this many frames at a time, up to the limit.
#define GLIDE_STEP 5.0f
#define GLIDE_PREDICT_FRAMES 600.0f
// The vertical acceleration estimate follows the measured one at this rate, within the
// bounds (gravity is about 0.025 a frame).
#define GLIDE_AY_RATE 0.2f
#define GLIDE_AY_MIN -0.06f
#define GLIDE_AY_MAX 0.02f

static float stc_prev_vy[PLY_NUM];
static float stc_ay[PLY_NUM];

// Once per CPU update: the measured vertical acceleration while airborne.
void CpuGlide_Track(RiderData *rd)
{
    MachineData *md;
    float ay;
    int ply = rd->ply;

    if (!rd->machine_gobj)
        return;
    md = rd->machine_gobj->userdata;
    if (md->is_airborne || md->is_flying)
    {
        ay = md->world_velocity.Y - stc_prev_vy[ply];
        stc_ay[ply] += (ay - stc_ay[ply]) * GLIDE_AY_RATE;
    }
    else
    {
        stc_ay[ply] = 0;
    }
    stc_prev_vy[ply] = md->world_velocity.Y;
}

// Where the current glide comes down: the velocity carried forward under the measured
// vertical acceleration, against the ground. 0 when it does not land in time.
int CpuGlide_Predict(RiderData *rd, Vec3 *land)
{
    float ay = stc_ay[rd->ply];
    Vec3 pos = rd->pos;
    Vec3 vel, next;
    float t;

    if (!rd->machine_gobj)
        return 0;
    vel = ((MachineData *)rd->machine_gobj->userdata)->world_velocity;
    if (ay < GLIDE_AY_MIN)
        ay = GLIDE_AY_MIN;
    if (ay > GLIDE_AY_MAX)
        ay = GLIDE_AY_MAX;

    for (t = 0; t < GLIDE_PREDICT_FRAMES; t += GLIDE_STEP)
    {
        next.X = pos.X + vel.X * GLIDE_STEP;
        next.Y = pos.Y + vel.Y * GLIDE_STEP + 0.5f * ay * GLIDE_STEP * GLIDE_STEP;
        next.Z = pos.Z + vel.Z * GLIDE_STEP;
        vel.Y += ay * GLIDE_STEP;
        if (Raycast_Ground(&pos, &next, land))
            return 1;
        pos = next;
    }
    return 0;
}

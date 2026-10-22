#include <string.h>

#include "game.h"
#include "os.h"
#include "machine.h"

#include "player_scale.h"
#include "event_scale_change.h"

// Player size, and with it the world-speed and camera-distance factor.
#define SCALE_TARGET_FACTOR 0.5f
#define SCALE_EASE_STEP 0.02f

// A one-frame jump above this many times top_speed_current is a respawn or warp,
// passed through unscaled so the machine lands on its target.
#define SCALE_TELEPORT_SPEED_MULT 5.0f

// The machine a player slot's speed lever follows.
typedef struct SlotSpeed
{
    GOBJ *machine; // NULL = last_pos not seeded
    Vec3 last_pos; // position left last frame
} SlotSpeed;

static SlotSpeed slot[PLY_NUM];
static float cur_factor = 1.0f;

// Keeps only `factor` of the machine's displacement since the last pass by pulling
// its position back toward last_pos. The event proc runs before machine physics, so
// the engine integrates and resolves collision from the pulled-back position.
static void ApplyMachineSpeed(SlotSpeed *s, GOBJ *mg, float factor)
{
    MachineData *md = mg->userdata;

    if (s->machine != mg)
    {
        s->machine = mg;
        s->last_pos = md->pos;
        return;
    }

    Vec3 step;
    VECSubtract(&md->pos, &s->last_pos, &step);

    float maxstep = md->top_speed_current * SCALE_TELEPORT_SPEED_MULT;
    if (maxstep > 1.0f && VECSquareMag(&step) > maxstep * maxstep)
    {
        s->last_pos = md->pos;
        return;
    }

    VECScale(&step, &step, factor);
    VECAdd(&s->last_pos, &step, &md->pos);
    s->last_pos = md->pos;
}

static void ApplyScale(float factor)
{
    PlayerScale_Apply(factor, factor);

    for (int ply = 0; ply < PLY_NUM; ply++)
    {
        GOBJ *mg = Ply_GetMachineGObj(ply);
        if (mg)
            ApplyMachineSpeed(&slot[ply], mg, factor);
        else
            slot[ply].machine = NULL;
    }
}

void ScaleChange_Start(void)
{
    cur_factor = 1.0f;
    memset(slot, 0, sizeof(slot));
    OSReport("[ScaleChange] Started, players shrink to %d%%\n", (int)(SCALE_TARGET_FACTOR * 100.0f));
}

void ScaleChange_Active(void)
{
    cur_factor = PlayerScale_Ease(cur_factor, SCALE_TARGET_FACTOR, SCALE_EASE_STEP);
    ApplyScale(cur_factor);
}

void ScaleChange_End(void)
{
    cur_factor = PlayerScale_Ease(cur_factor, 1.0f, SCALE_EASE_STEP);
    ApplyScale(cur_factor);
}

void ScaleChange_End2(void)
{
    PlayerScale_Restore();
    OSReport("[ScaleChange] Players restored to normal scale\n");
}

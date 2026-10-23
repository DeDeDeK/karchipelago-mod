#include "game.h"
#include "os.h"

#include "player_scale.h"
#include "event_giant_tiny.h"

#define GIANT_FACTOR 6.0f
#define TINY_FACTOR 0.25f
#define GIANT_TINY_EASE_STEP 0.03f

static float target_factor = 1.0f;
static float cur_factor = 1.0f;

// The camera follows at sqrt(factor), so a giant's camera clears its own model and the
// player still reads as giant or tiny on screen.
static void ApplyScale(void)
{
    PlayerScale_Apply(cur_factor, sqrtf(cur_factor));
}

static void Start(float target)
{
    cur_factor = 1.0f;
    target_factor = target;
    OSReport("[GiantTiny] Started, players scale to %d%%\n", (int)(target * 100.0f));
}

void GiantTiny_StartGiant(void)
{
    Start(GIANT_FACTOR);
}

void GiantTiny_StartTiny(void)
{
    Start(TINY_FACTOR);
}

void GiantTiny_Active(void)
{
    cur_factor = PlayerScale_Ease(cur_factor, target_factor, GIANT_TINY_EASE_STEP);
    ApplyScale();
}

void GiantTiny_End(void)
{
    cur_factor = PlayerScale_Ease(cur_factor, 1.0f, GIANT_TINY_EASE_STEP);
    ApplyScale();
}

void GiantTiny_End2(void)
{
    PlayerScale_Restore();
    OSReport("[GiantTiny] Players restored to normal scale\n");
}

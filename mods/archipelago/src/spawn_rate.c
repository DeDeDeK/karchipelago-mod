#include "game.h"
#include "hsd.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "spawn_rate.h"
#include "os.h"
#include "textbox.h"

// Returns the spawn rate scale factor: 1.0 + level * 0.1
static float SpawnRate_GetScale()
{
    return 1.0f + (float)ap_save->spawn_rate_level * 0.1f;
}

// -------------------------------------------------------------------------
// City Trial: Scale the spawn timer down when it is reset.
//
// The vanilla spawn system picks a random timer in [spawn_time_min, spawn_time_max]
// and counts it down by 1 each frame. When it reaches 0, an item spawns.
// We hook right before the timer is stored and divide it by the scale factor,
// making items spawn faster.
//
// TODO: Also scale cur_max_items (item_max in ItemFallDesc) so faster spawning
// isn't throttled by the simultaneous-item cap.
// -------------------------------------------------------------------------

// Called from HOOKCREATE prologue with the timer value.
// Returns the scaled timer value.
int SpawnRate_ScaleCTTimer(int timer)
{
    int level = ap_save->spawn_rate_level;
    if (level == 0)
        return timer;
    float scaled = (float)timer / SpawnRate_GetScale();
    int result = (int)scaled;
    if (result < 4)
        result = 4;
    return result;
}

// Hook at 0x800ea8b0: first timer store site in CityItemSpawn_UpdateAndCheckToSpawn.
// At this point r0 = new timer value, next instruction is stw r0, 44(r3).
// Clobbered: lwz r3, 1552(r13)   (reloads grBoxGeneInfo* — exactly what we need)
CODEPATCH_HOOKCREATE(0x800ea8b0,
    "mr 3, 0\n\t",
    SpawnRate_ScaleCTTimer,
    "mr 0, 3\n\t",
    0
)

// Hook at 0x800ea990: second timer store site (same function, different path).
// Same register state as above.
CODEPATCH_HOOKCREATE(0x800ea990,
    "mr 3, 0\n\t",
    SpawnRate_ScaleCTTimer,
    "mr 0, 3\n\t",
    0
)

// -------------------------------------------------------------------------
// Top Ride: Scale the per-frame spawn probability up.
//
// TopRideItem_SpawnTimed computes a spawn probability in f30, then calls
// HSD_Randf() and spawns if random < probability. We replace the HSD_Randf
// call with our wrapper that divides the random result by the scale factor,
// effectively increasing the spawn probability.
// -------------------------------------------------------------------------

float SpawnRate_ScaledRandf()
{
    float r = HSD_Randf();
    int level = ap_save->spawn_rate_level;
    if (level == 0)
        return r;
    return r / SpawnRate_GetScale();
}

void SpawnRate_Increment()
{
    ap_save->spawn_rate_level++;
    float pct = (float)ap_save->spawn_rate_level * 10.0f;
    OSReport("[SpawnRate] Spawn rate level increased to %d (+%.0f%%).\n",
             ap_save->spawn_rate_level, pct);
    TextBox_Enqueue("Spawn rate up! (+%.0f%%)", pct);
}

void SpawnRate_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x800ea8b0);
    CODEPATCH_HOOKAPPLY(0x800ea990);
    CODEPATCH_REPLACECALL(0x8034bae0, SpawnRate_ScaledRandf);
    OSReport("[SpawnRate] Spawn rate hooks installed\n");
}

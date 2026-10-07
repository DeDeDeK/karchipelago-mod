#include <stdio.h>

#include "game.h"
#include "hsd.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "spawn_rate.h"
#include "os.h"
#include "textbox_api.h"
#include "ap_announce.h"

// Where a full Spawn Rate Up collection lands; it keeps Top Ride's spawn roll short of
// saturation.
#define SPAWN_RATE_SCALE_MAX 3.0f

float SpawnRate_GetScale(void)
{
    if (!ap_save->options_received)
        return 1.0f;

    // The 10% floor keeps the timer divisions off zero.
    u32 min_pct = ap_save->options.spawn_rate_min;
    if (min_pct < 10)
        min_pct = 10;
    float scale = (float)min_pct / 100.0f + (float)ap_save->spawn_rate_level * 0.1f;
    if (scale > SPAWN_RATE_SCALE_MAX)
        scale = SPAWN_RATE_SCALE_MAX;
    return scale;
}

// City Trial counts a random timer down to 0 to spawn an item; this is its new value,
// floored at 4 frames.
static int SpawnRate_ScaleCTTimer(int timer)
{
    int result = (int)((float)timer / SpawnRate_GetScale());
    return result < 4 ? 4 : result;
}

// The two timer stores in CityItemSpawn_UpdateAndCheckToSpawn (0x800ea6e0). r0 = the new
// timer, stored by the next stw r0, 44(r3). Clobbered: lwz r3, 1552(r13), reloading
// grBoxGeneInfo.
CODEPATCH_HOOKCREATE(0x800ea8b0,
    "mr 3, 0\n\t",
    SpawnRate_ScaleCTTimer,
    "mr 0, 3\n\t",
    0
)

CODEPATCH_HOOKCREATE(0x800ea990,
    "mr 3, 0\n\t",
    SpawnRate_ScaleCTTimer,
    "mr 0, 3\n\t",
    0
)

// Scales the simultaneous-item cap (ItemFallDesc.item_max) too, or faster spawning only
// churns items. Only ever up: a slower timer already thins the field, and a scaled-down
// cap could truncate to 0. Returns 1 to skip the spawn.
static int SpawnRate_CTCapReached(int cur_num, int cap)
{
    int scaled_cap = (int)((float)cap * SpawnRate_GetScale());
    if (scaled_cap < cap)
        scaled_cap = cap;
    return cur_num >= scaled_cap;
}

// The cmpw of cur_num_items (r3) against item_max (r0) in the same function. The bl
// clobbers r5 = grBoxGeneInfo, which both exits read.
CODEPATCH_HOOKCONDITIONALCREATE(0x800eaa8c,
    "mr 4, 0\n\t",
    SpawnRate_CTCapReached,
    "lwz 5, 1552(13)\n\t",
    0x800eaa94, // under the cap: past the bge, on to the spawn
    0x800eab4c  // at the cap: skip the spawn
)

// TopRideItem_SpawnTimed (0x8034b8c8) spawns when HSD_Randf() < probability, so dividing
// the roll by the scale raises the probability.
static float SpawnRate_ScaledRandf(void)
{
    return HSD_Randf() / SpawnRate_GetScale();
}

void SpawnRate_Increment(void)
{
    if (ap_save->spawn_rate_level < 255)
        ap_save->spawn_rate_level++;
    float pct = SpawnRate_GetScale() * 100.0f;
    OSReport("[SpawnRate] Level %d, effective rate %.0f%%\n", ap_save->spawn_rate_level, pct);

    char suffix[32];
    sprintf(suffix, " increased (%.0f%%)", pct);
    APAnnounce_Grant(NULL, "Spawn rate", tb_api->ItemColor, suffix);
}

void SpawnRate_OnBoot(void)
{
    CODEPATCH_HOOKAPPLY(0x800ea8b0);
    CODEPATCH_HOOKAPPLY(0x800ea990);
    CODEPATCH_HOOKAPPLY(0x800eaa8c);
    CODEPATCH_REPLACECALL(0x8034bae0, SpawnRate_ScaledRandf);
    OSReport("[SpawnRate] Hooks installed\n");
}

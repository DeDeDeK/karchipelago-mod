#include "game.h"
#include "hsd.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "spawn_rate.h"
#include "os.h"
#include "textbox_api.h"

// Effective spawn rate scale = min_pct/100 + level * 0.1. spawn_rate_min is the
// AP starting rate in percent; each Spawn Rate Up adds +10%. The cap backstops
// item density against a malformed save.
#define SPAWN_RATE_SCALE_MAX 3.0f

float SpawnRate_GetScale()
{
    u32 min_pct = ap_save->options.spawn_rate_min;
    // 0 = options not yet received, so fall back to vanilla. The 10% floor keeps
    // scale positive so the timer divisions never divide by zero.
    if (min_pct == 0)
        min_pct = 100;
    else if (min_pct < 10)
        min_pct = 10;
    float scale = (float)min_pct / 100.0f + (float)ap_save->spawn_rate_level * 0.1f;
    if (scale > SPAWN_RATE_SCALE_MAX)
        scale = SPAWN_RATE_SCALE_MAX;
    return scale;
}

// City Trial counts a random timer down to 0 to spawn an item. Divide it by the
// scale before the store, floored at 4 frames.
int SpawnRate_ScaleCTTimer(int timer)
{
    float scaled = (float)timer / SpawnRate_GetScale();
    int result = (int)scaled;
    if (result < 4)
        result = 4;
    return result;
}

// First timer store site in CityItemSpawn_UpdateAndCheckToSpawn. r0 = new timer
// value, next instruction is stw r0, 44(r3).
// Clobbered: lwz r3, 1552(r13), which reloads grBoxGeneInfo*.
CODEPATCH_HOOKCREATE(0x800ea8b0,
    "mr 3, 0\n\t",
    SpawnRate_ScaleCTTimer,
    "mr 0, 3\n\t",
    0
)

// Second timer store site in the same function, same register state.
CODEPATCH_HOOKCREATE(0x800ea990,
    "mr 3, 0\n\t",
    SpawnRate_ScaleCTTimer,
    "mr 0, 3\n\t",
    0
)

// Scale the simultaneous-item cap (ItemFallDesc.item_max) up too, or faster
// spawning just churns items without growing density. Replaces the cmpw of
// cur_num_items vs item_max: 1 skips the spawn, 0 continues.
int SpawnRate_CTCapReached(int cur_num, int cap)
{
    int scaled_cap = (int)((float)cap * SpawnRate_GetScale());
    // Only scale up: for sub-vanilla rates the slower timer already suppresses
    // spawns, and a down-scaled cap could truncate to 0 and block them entirely.
    if (scaled_cap < cap)
        scaled_cap = cap;
    return cur_num >= scaled_cap;
}

CODEPATCH_HOOKCONDITIONALCREATE(0x800eaa8c,
    "mr 4, 0\n\t",                     // arg2 = item_max (was in r0)
    SpawnRate_CTCapReached,
    "lwz 5, 1552(13)\n\t",             // restore r5 = grBoxGeneInfo (volatile, clobbered by bl)
    0x800eaa94,                        // 0-return: skip past bge into spawn-success branch
    0x800eab4c                         // 1-return: jump to skip-spawn
)

// TopRideItem_SpawnTimed spawns if HSD_Randf() < probability, so dividing the
// random result by the scale effectively raises that probability.
float SpawnRate_ScaledRandf()
{
    return HSD_Randf() / SpawnRate_GetScale();
}

void SpawnRate_Increment()
{
    if (ap_save->spawn_rate_level < 255)
        ap_save->spawn_rate_level++;
    // Show the absolute effective rate, post-min and post-cap, rather than the
    // delta from vanilla.
    float pct = SpawnRate_GetScale() * 100.0f;
    OSReport("[SpawnRate] Level %d, effective rate %.0f%%\n",
             ap_save->spawn_rate_level, pct);
    tb_api->EnqueueColoredNounFmt(NULL, "Spawn rate", tb_api->ItemColor, " increased (%.0f%%)", pct);
}

void SpawnRate_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x800ea8b0);
    CODEPATCH_HOOKAPPLY(0x800ea990);
    CODEPATCH_HOOKAPPLY(0x800eaa8c);
    CODEPATCH_REPLACECALL(0x8034bae0, SpawnRate_ScaledRandf);
    OSReport("[SpawnRate] Spawn rate hooks installed\n");
}

#include "game.h"
#include "hsd.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "spawn_rate.h"
#include "os.h"
#include "textbox.h"
#include "textbox_colors.h"

// Returns the effective spawn rate scale factor.
//
// scale = max(min_pct, 100) / 100  +  level * 0.1, capped at 5.0
//
// Floor (`spawn_rate_min`, percent) is set by the AP slot option and represents
// the rate the player starts at before any items are received. Each Spawn Rate
// Up item adds +10% on top. Default min is 100 (vanilla baseline).
//
// The 5.0 cap keeps both modes in a similar effective rate ceiling: CT's
// 4-frame timer floor naturally caps spawns at ~15/sec, and at scale 5 TR's
// per-frame spawn probability (already small in vanilla) lands in roughly the
// same range. Without a cap, TR would saturate to "every frame spawns" while
// CT stayed bounded by the floor.
#define SPAWN_RATE_SCALE_MAX 5.0f

static float SpawnRate_GetScale()
{
    u32 min_pct = ap_save->options.spawn_rate_min;
    if (min_pct < 100)
        min_pct = 100;
    float scale = (float)min_pct / 100.0f + (float)ap_save->spawn_rate_level * 0.1f;
    if (scale > SPAWN_RATE_SCALE_MAX)
        scale = SPAWN_RATE_SCALE_MAX;
    return scale;
}

// City Trial: Scale the spawn timer down when it is reset.
//
// The vanilla spawn system picks a random timer in [spawn_time_min, spawn_time_max]
// and counts it down by 1 each frame. When it reaches 0, an item spawns.
// We hook right before the timer is stored and divide it by the scale factor,
// making items spawn faster.

// Called from HOOKCREATE prologue with the timer value.
// Returns the scaled timer value.
int SpawnRate_ScaleCTTimer(int timer)
{
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

// City Trial: Scale the simultaneous-item cap up.
//
// Without this, faster spawning is throttled once the item count reaches the
// vanilla cap — items just churn faster, density doesn't grow. The vanilla cap
// is `ItemFallDesc.item_max`, compared against `grBoxGeneInfo.cur_num_items`
// in CityItemSpawn_UpdateAndCheckToSpawn:
//
//   800eaa84: lwz  r3, 32(r5)   ; cur_num_items
//   800eaa88: lwz  r0,  4(r30)  ; ItemFallDesc.item_max
//   800eaa8c: cmpw r3, r0
//   800eaa90: bge  0x800eab4c   ; skip-spawn when cur >= cap
//
// We replace the comparison with a conditional hook at the cmpw. Returns 1
// (skip) if cur_num >= scaled_cap, else 0 (continue spawn). The original cmpw
// runs harmlessly on the 0-return path; we then jump past the bge.
//
// Register state at 0x800eaa8c: r3 = cur_num_items, r0 = item_max,
// r5 = grBoxGeneInfo (needed at the spawn-success branch target 0x800eaa94),
// r30 = ItemFallDesc* (preserved across the bl, non-volatile).

int SpawnRate_CTCapReached(int cur_num, int cap)
{
    int scaled_cap = (int)((float)cap * SpawnRate_GetScale());
    return cur_num >= scaled_cap;
}

CODEPATCH_HOOKCONDITIONALCREATE(0x800eaa8c,
    "mr 4, 0\n\t",                     // arg2 = item_max (was in r0)
    SpawnRate_CTCapReached,
    "lwz 5, 1552(13)\n\t",             // restore r5 = grBoxGeneInfo (volatile, clobbered by bl)
    0x800eaa94,                        // 0-return: skip past bge into spawn-success branch
    0x800eab4c                         // 1-return: jump to skip-spawn
)

// Top Ride: Scale the per-frame spawn probability up.
//
// TopRideItem_SpawnTimed computes a spawn probability in f30, then calls
// HSD_Randf() and spawns if random < probability. We replace the HSD_Randf
// call with our wrapper that divides the random result by the scale factor,
// effectively increasing the spawn probability.
float SpawnRate_ScaledRandf()
{
    return HSD_Randf() / SpawnRate_GetScale();
}

void SpawnRate_Increment()
{
    if (ap_save->spawn_rate_level < 255)
        ap_save->spawn_rate_level++;
    // Display the absolute effective rate (post-min, post-cap) so the textbox
    // tells the player where they are now, not just the delta from vanilla.
    float pct = SpawnRate_GetScale() * 100.0f;
    OSReport("[SpawnRate] Level %d, effective rate %.0f%%.\n",
             ap_save->spawn_rate_level, pct);
    TextBox_EnqueueColoredNounFmt(NULL, "Spawn rate", TextBox_ItemColor, " up! (%.0f%%)", pct);
}

void SpawnRate_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x800ea8b0);
    CODEPATCH_HOOKAPPLY(0x800ea990);
    CODEPATCH_HOOKAPPLY(0x800eaa8c);
    CODEPATCH_REPLACECALL(0x8034bae0, SpawnRate_ScaledRandf);
    OSReport("[SpawnRate] Spawn rate hooks installed\n");
}

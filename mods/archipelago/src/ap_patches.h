#ifndef ARCHIPELAGO_AP_PATCHES_H
#define ARCHIPELAGO_AP_PATCHES_H

#include "main.h"

// AP Patches: an AP Box in City Trial that drops AP Patches, one multiworld location per
// patch collected. Both are custom_items drop-ins kept out of every vanilla counter.

// The CustomItemDesc.name each drop-in's archive carries; a mismatch silently unbinds it.
#define AP_PATCH_ITEM_NAME "AP Patch"
#define AP_BOX_ITEM_NAME   "AP Box"

void APPatches_OnBoot(void);
void APPatches_On3DLoadStart(void);
void APPatches_On3DLoadEnd(void);
void APPatches_On3DExit(void);
void APPatches_OnSaveLoaded(void);

// Applies the AP Patches the client backfilled. Call only while ap_data->backfill_valid
// is set.
void APPatches_ApplyBackfill(void);

// Clear every collected bit in both save and mirror, or fill the first
// ap_patches of them. Neither persists - the caller owns the card write.
void APPatches_ResetAll(void);
void APPatches_DebugForceMarkAll(void);

// Claim the lowest unclaimed patch, as a pickup does. Returns 0 when the category
// is off or every patch is already collected.
int APPatches_DebugClaim(void);

// Clear every collected bit in the save, the wire mirror and the client's pending
// backfill, so the lowest patch is claimable again. Persists.
void APPatches_DebugClearCollected(void);

// Spawn one AP Box in front of a player's machine, bypassing the spawner. Returns
// 0 if the item was not registered when this scene loaded.
int APPatches_DebugSpawnBox(int ply);

// Patches collected so far, and how many of the seed's are still unclaimed.
int APPatches_CollectedCount(void);
int APPatches_Remaining(void);

// The seed's patch count, clamped to AP_PATCH_MAX, and a debug override of it that
// takes effect at the next round load. Lowering it drops the collected bits above
// the new count.
int APPatches_GetCount(void);
void APPatches_DebugSetCount(int count);

#endif // ARCHIPELAGO_AP_PATCHES_H

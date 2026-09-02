#ifndef ARCHIPELAGO_AP_PATCHES_H
#define ARCHIPELAGO_AP_PATCHES_H

#include "main.h"

// AP Patches: an AP-branded item box that drops AP-branded patches in City Trial,
// one multiworld location per patch collected. The box is a fourth outcome of the
// vanilla box roll. Both are custom_items drop-ins bound by display name, and both
// are isolated from every vanilla counter.

// CustomItemDesc.name of each drop-in. The authoring scripts write these into the
// archives; changing one without the other silently unbinds the item.
#define AP_PATCH_ITEM_NAME "AP Patch"
#define AP_BOX_ITEM_NAME   "AP Box"

void ApPatches_OnBoot(void);
void ApPatches_On3DLoadStart(void);
void ApPatches_On3DLoadEnd(void);
void ApPatches_On3DExit(void);
void ApPatches_OnFrameStart(void);
void ApPatches_OnSaveLoaded(void);

// Clear every collected bit in both save and mirror, or fill the first
// ap_patches of them. Neither persists - the caller owns the card write.
void ApPatches_ResetAll(void);
void ApPatches_DebugForceMarkAll(void);

// Claim the lowest unclaimed patch, as a pickup does. Returns 0 when the category
// is off or every patch is already collected.
int ApPatches_DebugClaim(void);

// Spawn one AP Box in front of a player's machine, bypassing the spawner. Returns
// 0 if the item was not registered when this scene loaded.
int ApPatches_DebugSpawnBox(int ply);

// Patches collected so far, and how many of the seed's are still unclaimed.
int ApPatches_CollectedCount(void);
int ApPatches_Remaining(void);

// The seed's patch count, clamped to AP_PATCH_MAX, and a debug override of it that
// takes effect at the next round load. Lowering it drops the collected bits above
// the new count.
int ApPatches_GetCount(void);
void ApPatches_DebugSetCount(int count);

#endif // ARCHIPELAGO_AP_PATCHES_H

#ifndef PATCH_CAP_H
#define PATCH_CAP_H

void PatchCap_OnBoot();
void PatchCap_Increment();

// Effective per-stat cap now: the seed's minimum plus one per Patch Cap Increase
// received, clamped to the seed's maximum.
int PatchCap_GetCap();

// Value a City Trial stat spawns at: 0 for HP, -2 otherwise. Patch counts are
// measured relative to this baseline.
float PatchCap_GetStatStart(int kind);

#endif

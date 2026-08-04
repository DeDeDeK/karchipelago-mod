#ifndef PATCH_CAP_H
#define PATCH_CAP_H

void PatchCap_OnBoot();
void PatchCap_Increment();

// Value a City Trial stat spawns at: 0 for HP, -2 otherwise. Patch counts are
// measured relative to this baseline.
float PatchCap_GetStatStart(int kind);

#endif

#ifndef PATCH_CAP_H
#define PATCH_CAP_H

void PatchCap_OnBoot();
void PatchCap_Increment();

// Returns the value a City Trial stat spawns at: 0 for HP, -2 for every other
// stat. Patch counts are measured relative to this baseline, so the cap clamp
// and the Max Stats goal both offset the raw stat value by it.
float PatchCap_GetStatStart(int kind);

#endif

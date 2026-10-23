#ifndef PATCH_CAP_H
#define PATCH_CAP_H

void PatchCap_OnBoot(void);
void PatchCap_Increment(void);

// The seed's per-stat ceiling, and the cap in force now: the seed's minimum plus one per
// Patch Cap Increase received, up to that ceiling. Vanilla's 18 until the options arrive.
int PatchCap_GetMax(void);
int PatchCap_GetCap(void);

// Value a City Trial stat spawns at: 0 for HP, -2 otherwise. Patch counts are measured
// from it.
float PatchCap_GetStatStart(int kind);

// 1 when stat `kind` holds at least `patches` patches or sits at the raw ceiling, which
// HP reaches two patches early.
int PatchCap_IsStatAt(const float *values, int kind, int patches);

#endif

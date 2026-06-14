#ifndef GATE_COLORS_H
#define GATE_COLORS_H

void GateColors_OnBoot();
int GateColors_UnlockColor(int color_idx, int announce);
void GateColors_ValidateCityTrialColors(void);
// Pick a random unlocked Kirby color (used to give CPUs a random color in each
// mode). Always returns a valid unlocked index (falls back to first unlocked).
int GateColors_RandomUnlockedColor(void);

#endif

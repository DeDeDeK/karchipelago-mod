#ifndef GATE_COLORS_H
#define GATE_COLORS_H

void GateColors_OnBoot();
int GateColors_UnlockColor(int color_idx, int announce);
void GateColors_ValidateCityTrialColors(void);
int GateColors_RandomUnlockedColor(void);
int GateColors_RandomUnlockedColorExcept(const u8 *taken, int num_taken);

#endif

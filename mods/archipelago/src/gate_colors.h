#ifndef GATE_COLORS_H
#define GATE_COLORS_H

void GateColors_OnBoot();
int GateColors_UnlockColor(int color_idx, int announce);
void GateColors_ValidateCityTrialColors(void);

// A random unlocked color for panel `slot` that no other visible panel shows. kinds holds
// each panel's slot kind; human_kind is the active-human value, which differs per screen.
int GateColors_RandomForPanel(const u8 *kinds, const u8 *colors, int slot, u8 human_kind);

#endif

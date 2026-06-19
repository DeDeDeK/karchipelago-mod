#ifndef CUSTOM_AI_H
#define CUSTOM_AI_H

#include "datatypes.h"

// Shared module for the custom AI mod. The settings menu is organized by game
// mode, each mode exposing only the AI domains it actually has:
//   - City Trial : CPU riders + Kirby Melee stadium enemies
//   - Air Ride   : CPU riders + enemies
//   - Top Ride   : CPU riders
// Two preset tables back these selectors:
//   - cpu_ai.*   : CPU-controlled riders (one shared preset set; per-mode select)
//   - enemy_ai.* : pool enemies (Waddle Dee, Sword Knight, ...) - one shared
//                  preset set with independent Air Ride / City Trial selection
// Every selector exposes a "Random" entry that resolves to a concrete preset via
// CustomAI_RollRandom.

// Roll a uniformly random preset index in [0, count). Used to resolve the
// "Random" menu entry for both selectors. Returns 0 if count <= 0.
int CustomAI_RollRandom(int count);

void CustomAI_OnBoot(void);

#endif // CUSTOM_AI_H

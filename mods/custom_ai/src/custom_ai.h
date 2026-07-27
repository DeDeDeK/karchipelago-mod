#ifndef CUSTOM_AI_H
#define CUSTOM_AI_H

#include "datatypes.h"

// Shared entry points for the custom AI mod. Two preset tables back the settings
// menu: one for CPU-controlled riders, one for pool enemies. Each selector
// exposes a "Random" entry resolved through CustomAI_RollRandom.

// Uniformly random index in [0, count). Returns 0 if count <= 0.
int CustomAI_RollRandom(int count);

void CustomAI_OnBoot(void);

#endif // CUSTOM_AI_H

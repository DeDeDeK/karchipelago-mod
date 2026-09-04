#ifndef SPAWN_RATE_H
#define SPAWN_RATE_H

void SpawnRate_OnBoot();
void SpawnRate_Increment();

// Item spawn frequency multiplier, 0.1 (options not yet received reads as vanilla 1.0)
// up to SPAWN_RATE_SCALE_MAX. Used as a divisor on the CT timer and the TR probability.
float SpawnRate_GetScale();

#endif

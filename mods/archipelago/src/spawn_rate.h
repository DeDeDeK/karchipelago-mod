#ifndef SPAWN_RATE_H
#define SPAWN_RATE_H

void SpawnRate_OnBoot();
void SpawnRate_Increment();

// Item spawn frequency multiplier, from 0.1 up to 3.0; options not yet received reads
// as vanilla 1.0. Divides the City Trial spawn timer and the Top Ride probability, and
// multiplies the City Trial simultaneous-item cap.
float SpawnRate_GetScale();

#endif

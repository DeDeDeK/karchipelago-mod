#ifndef SPAWN_RATE_H
#define SPAWN_RATE_H

void SpawnRate_OnBoot(void);
void SpawnRate_Increment(void);

// Item spawn frequency multiplier, 0.1 to 3.0; 1.0 until the options arrive.
float SpawnRate_GetScale(void);

#endif

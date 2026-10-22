#ifndef RAIN_DRAGON_H
#define RAIN_DRAGON_H

#include "structs.h"

#define RAIN_DRAGON_MAX 10

// Copies a volcano lava dragon's model and hitbox for the round and frees every slot.
// Returns 0 when no vanilla dragon is there to copy.
int RainDragon_Prepare(void);

int RainDragon_Count(void);

// A dragon falling head first from drop, removed when its head reaches a floor.
int RainDragon_Spawn(Vec3 *floor, Vec3 *drop);

// Removes every dragon still falling.
void RainDragon_RemoveAll(void);

// Forgets the dragons without touching them, for a scene exit.
void RainDragon_Forget(void);

#endif // RAIN_DRAGON_H

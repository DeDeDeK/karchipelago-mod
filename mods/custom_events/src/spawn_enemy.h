#ifndef SPAWN_ENEMY_H
#define SPAWN_ENEMY_H

#include "enemy.h"

// Install null-safe patches for standalone enemy spawning.
// Patches EventActor_GetParentScale and splArcLengthPoint to handle null pointers
// that occur when actors are spawned outside the normal event system.
void SpawnEnemy_OnBoot(void);

// Spawn a random enemy actor near the given machine.
// If use_splines is true, the enemy will be assigned the nearest stage spline
// and follow it naturally. Otherwise it stays stationary.
// Returns the spawned GOBJ, or NULL on failure.
GOBJ *SpawnEnemy_Random(GOBJ *machine_gobj, int use_splines);

// Spawn a meteor directly above every human player. The meteor falls
// straight down and damages on impact. Returns 1 if at least one
// meteor was spawned, 0 if not in a valid scene.
int SpawnEnemy_MeteorTrap(void);

#endif // SPAWN_ENEMY_H

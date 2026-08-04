#ifndef SPAWN_ENEMY_H
#define SPAWN_ENEMY_H

#include "enemy.h"

// Patches EventActor_GetParentScale and splArcLengthPoint to tolerate the null
// parent/spline pointers a standalone spawn has. Call once at boot.
void SpawnEnemy_OnBoot(void);

// use_splines attaches the actor to the nearest stage spline; otherwise it stays
// put. Returns the spawned GOBJ, or NULL on failure.
GOBJ *SpawnEnemy_Random(GOBJ *machine_gobj, int use_splines);

// Drops a meteor on every human player. Returns 1 if at least one spawned.
int SpawnEnemy_MeteorTrap(void);

#endif // SPAWN_ENEMY_H

#ifndef SPAWN_PROJECTILE_H
#define SPAWN_PROJECTILE_H

// Spawn a vanilla projectile in front of every human player, built from the
// machine's pos/forward/up/velocity so no copy ability need be active.
// Each returns 1 if at least one projectile was spawned.
int SpawnProjectile_BombTrap(void);
int SpawnProjectile_GordoTrap(void);
int SpawnProjectile_SensorBombTrap(void);

#endif // SPAWN_PROJECTILE_H

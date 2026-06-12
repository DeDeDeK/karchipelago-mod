#ifndef SPAWN_PROJECTILE_H
#define SPAWN_PROJECTILE_H

// Trap handlers that spawn vanilla projectiles (Bomb, Phan Phan Gordo,
// Sensor Bomb) in front of every human player. Each one constructs a
// ProjectileDesc from the machine's pos/forward/up/velocity and calls
// Projectile_Create directly, so they do NOT require any copy ability to
// be active on the rider.
//
// Returns 1 if at least one projectile was spawned, 0 otherwise.
int SpawnProjectile_BombTrap(void);
int SpawnProjectile_GordoTrap(void);
int SpawnProjectile_SensorBombTrap(void);

#endif // SPAWN_PROJECTILE_H

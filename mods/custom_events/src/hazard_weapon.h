#ifndef HAZARD_WEAPON_H
#define HAZARD_WEAPON_H

#include "obj.h"
#include "weapon.h"

// A weapon at pos with a random yaw, scale 1 and full charge, still in its HELD state 0.
// With no owner it hits every rider and credits no one. Returns NULL when the kind's
// data is not registered, which Weapon_Create would read unguarded.
WeaponData *HazardWeapon_Create(WeaponKind kind, Vec3 *pos, GOBJ *owner);

// Runs a vanilla active-weapon cap with every ownerless weapon of kind hidden from it:
// an ownerless weapon is a stage hazard rather than anyone's throw.
void HazardWeapon_EnforceMax(WeaponKind kind, void (*enforce)(void));

#endif // HAZARD_WEAPON_H

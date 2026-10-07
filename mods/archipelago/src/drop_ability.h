#ifndef ARCHIPELAGO_DROP_ABILITY_H
#define ARCHIPELAGO_DROP_ABILITY_H

// Press Z to drop the held copy ability (City Trial / Air Ride), gated by
// drop_ability_enabled.
void DropAbility_On3DLoadEnd(void);

// Top Ride: Z discards a held ability power (Fire / Freeze Fan / Bomb / Walky).
void DropAbility_OnTopRideLoadEnd(void);

#endif // ARCHIPELAGO_DROP_ABILITY_H

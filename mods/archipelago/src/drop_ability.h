#ifndef ARCHIPELAGO_DROP_ABILITY_H
#define ARCHIPELAGO_DROP_ABILITY_H

// Spawns the per-frame "press Z to drop your copy ability" applier for the
// current City Trial / Air Ride round. Gated by ap_menu_settings.drop_ability_enabled.
void DropAbility_On3DLoadEnd(void);

// Top Ride counterpart: press Z to discard a held ability-power item (Fire /
// Freeze Fan / Bomb / Walky - the TR analogs of copy abilities). Same setting.
void DropAbility_OnTopRideLoadEnd(void);

#endif // ARCHIPELAGO_DROP_ABILITY_H

#ifndef ARCHIPELAGO_CHECKLIST_REWARDS_H
#define ARCHIPELAGO_CHECKLIST_REWARDS_H

#include "structs.h"
#include "game.h"

// Install the ClearChecker_CheckUnlocked replacement. Call from OnBoot.
void ChecklistRewards_OnBoot(void);

// Restore received rewards from save data. Call from OnSaveLoaded,
// after ShuffleRewards_OnSaveLoaded (which restores clear_kind values).
void ChecklistRewards_OnSaveLoaded(void);

// Grant a checklist reward received from the AP server.
// Sets the AP bitfield and marks the local checklist slot if one is assigned.
void ChecklistRewards_Grant(GameMode mode, u8 reward_index);

// Apply the AP location assignment from ArchipelagoData.
// Call when archipelago_data->location_data_valid transitions to 1.
void ChecklistRewards_ApplyLocations(void);

#endif // ARCHIPELAGO_CHECKLIST_REWARDS_H

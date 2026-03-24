#ifndef ARCHIPELAGO_CHECKLIST_REWARDS_H
#define ARCHIPELAGO_CHECKLIST_REWARDS_H

#include "structs.h"
#include "game.h"

// Cross-mode reward mapping: for each (target_mode, clear_kind), stores which
// reward from another mode is placed there. source_mode == 0xFF means none.
typedef struct CrossModeSlot
{
    u8 source_mode;
    u8 source_reward_index;
} CrossModeSlot;

// Per-mode cross-mode slot table. Populated by ApplyLocations, rebuilt on save load.
extern CrossModeSlot cross_mode_slots[GMMODE_NUM][120];

// Allocate reward tables and install all checklist hooks. Call from OnBoot.
void ChecklistRewards_OnBoot(void);

// First boot: clear tables and mark initialized. Call from OnSaveInit.
void ChecklistRewards_OnSaveInit(void);

// Restore reward tables and received rewards from save data. Call from OnSaveLoaded.
void ChecklistRewards_OnSaveLoaded(void);

// Grant a checklist reward received from the AP server.
void ChecklistRewards_Grant(GameMode mode, u8 reward_index);

// Apply the AP location assignment from ArchipelagoData.
void ChecklistRewards_ApplyLocations(void);

// Reset all cross-mode slot entries to empty (source_mode = 0xFF).
void ChecklistRewards_ClearCrossModeSlots(void);

// Reveal and unlock all checklist squares across all modes (debug/option).
void RevealAllChecklists(void);

#endif // ARCHIPELAGO_CHECKLIST_REWARDS_H

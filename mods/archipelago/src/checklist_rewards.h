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

// Per-mode reward table sizes (REWARD_COUNT_AIRRIDE / TOPRIDE / CITYTRIAL).
extern const int reward_counts[GMMODE_NUM];

// Allocate reward tables and install all checklist hooks. Call from OnBoot.
void ChecklistRewards_OnBoot(void);

// Restore reward tables and received rewards from save data. Call from OnSaveLoaded.
void ChecklistRewards_OnSaveLoaded(void);

// Grant a checklist reward received from the AP server.
void ChecklistRewards_Grant(GameMode mode, u8 reward_index);

// Apply the AP location assignment from APData.
void ChecklistRewards_ApplyLocations(void);

// Reset all cross-mode slot entries to empty (source_mode = 0xFF).
void ChecklistRewards_ClearCrossModeSlots(void);

// Reveal and unlock all checklist squares across all modes (debug/option).
void RevealAllChecklists(void);

// Debug: fill APData location arrays with a random shuffle
// (~1/3 same-mode, ~1/3 cross-mode, ~1/3 remote) and apply immediately.
// For standalone testing without an AP client connection.
void ChecklistRewards_DebugSimulateLocationData(void);

// Debug: full checklist reset. Zeros every checkbox flag across all modes,
// wipes sent_checks / received rewards / goal_complete, clears the
// location/shuffle assignment in both save and shared memory, and resets
// the mod's reward tables and cross-mode slots to the empty state.
void ChecklistRewards_DebugClearAll(void);

#endif // ARCHIPELAGO_CHECKLIST_REWARDS_H

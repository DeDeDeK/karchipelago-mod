#ifndef ARCHIPELAGO_CHECKLIST_REWARDS_H
#define ARCHIPELAGO_CHECKLIST_REWARDS_H

#include "game.h"
#include "main.h"

// Allocate reward tables and install all checklist hooks. Call from OnBoot.
void ChecklistRewards_OnBoot(void);

// Initialize checklist-owned save fields on fresh save creation.
// Call from OnSaveInit, after the top-level memset of ap_save.
void ChecklistRewards_OnSaveInit(void);

// Restore reward tables and received rewards from save data. Call from OnSaveLoaded.
void ChecklistRewards_OnSaveLoaded(void);

// Grant a checklist reward received from the AP server. `announce=1` shows the
// "Received: ..." textbox; pass 0 from re-grant paths (save-load restoration,
// post-shuffle re-apply) where the player isn't receiving anything new.
void ChecklistRewards_Grant(GameMode mode, u8 reward_index, int announce);

// Announce a checkbox filler grant on the TextBox: "Received: Checkbox Filler
// (<Mode>)". Does not mutate filler state - call Checklist_GrantFiller for that.
void Checklist_AnnounceFiller(GameMode mode);

// Apply the AP location assignment from APData.
void ChecklistRewards_ApplyLocations(void);

// Reveal and unlock all checklist squares across all modes (debug/option).
void RevealAllChecklists(void);

// checklist_rewards_gated off: mark every non-progression (cosmetic, no-gate-mask)
// checklist reward as received at connect, so the content is available from the start
// and its box is freed for an ordinary item. Progression Dragoon/Hydra part markers
// and gated-category rewards are left alone.
void ChecklistRewards_GrantAllCosmetic(void);

// Debug: fill APData location arrays with a random shuffle
// (~1/3 same-mode, ~1/3 cross-mode, ~1/3 remote) and apply immediately.
void ChecklistRewards_DebugSimulateLocationData(void);

// Debug: full checklist reset. Zeros every checkbox flag across all modes,
// wipes sent_checks / received rewards / goal_complete, clears the
// location/shuffle assignment in both save and shared memory, and resets
// the mod's reward tables and cross-mode slots to the empty state.
void ChecklistRewards_DebugClearAll(void);

// The cell under the checklist cursor, read live from the UI element. Returns 1 with
// `*out_mode` and `*out_clear_kind` populated, or 0 when no checklist screen is up or
// the cursor is off the grid (in the checkbox-filler list).
int ChecklistRewards_GetHoveredCell(u8 *out_mode, u8 *out_clear_kind);

// Resolve which reward is placed at (mode, clear_kind). On hit, writes the source mode
// and reward_index. Returns 0 if the cell has no local placement.
int ChecklistRewards_ResolveCell(u8 mode, u8 clear_kind,
                                 u8 *out_source_mode, u8 *out_source_reward_index);

// Returns 1 if a reward is placed at (mode, clear_kind) AND has already been received
// from AP - i.e. whether the cell shows the received-reward badge.
int ChecklistRewards_CellHasReceivedReward(u8 mode, u8 clear_kind);

// Number of reward rows for the given mode. Out-of-range modes return 0.
int ChecklistRewards_GetRewardCount(GameMode mode);

// Translate an AP reward_index (the apworld's clear_kind-sorted numbering, as encoded
// in a 500-649 item ID) to the internal game reward-table index used by Grant /
// shuffled_rewards / received_checklist_rewards. Out-of-range inputs pass through
// unchanged (range-check with GetRewardCount first).
u8 ChecklistRewards_ApToGameIndex(GameMode mode, u8 ap_reward_index);

// Encoded `shuffled_rewards[mode][index]`: high byte = target mode, low byte = target
// clear_kind, 0xFFFF = remote. Out-of-range inputs return 0xFFFF.
u16 ChecklistRewards_GetShuffledReward(GameMode mode, u8 reward_index);

#endif // ARCHIPELAGO_CHECKLIST_REWARDS_H

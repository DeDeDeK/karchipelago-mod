#ifndef ARCHIPELAGO_CHECKLIST_REWARDS_H
#define ARCHIPELAGO_CHECKLIST_REWARDS_H

#include "game.h"
#include "main.h"

// Allocates writable reward tables and installs the checklist hooks.
void ChecklistRewards_OnBoot(void);
void ChecklistRewards_OnSaveInit(void);

// Rebuilds the reward tables from shuffled_rewards and quietly re-grants received rewards.
void ChecklistRewards_OnSaveLoaded(void);

// Grants a checklist reward by game reward-table index. announce=0 is for re-grants (save
// load, a new location assignment): no textbox and no filler token.
void ChecklistRewards_Grant(GameMode mode, u8 reward_index, int announce);

// "Received: Checkbox Filler (<Mode>)" only; Checklist_GrantFiller grants the token.
void ChecklistRewards_AnnounceFiller(GameMode mode);

// Applies the AP location assignment from APData.
void ChecklistRewards_ApplyLocations(void);

// Makes every square of a checklist-mode row visible. Visual only.
void ChecklistRewards_Reveal(int row);
void ChecklistRewards_RevealAll(void);

// Marks every placeable reward whose (mode, RewardType) bit is clear in placed_types as
// received. Bit = mode * CHECKLIST_REWARD_MODE_BITS + reward_type.
void ChecklistRewards_GrantUnplaced(u32 placed_types);

// Debug: a random APData.locations shuffle (~1/3 same-mode, ~1/3 cross-mode, ~1/3
// remote), applied at once.
void ChecklistRewards_DebugSimulateLocationData(void);

// Debug: fresh-boot checklist state - vanilla checkbox flags, sent checks, AP Patches,
// goals, received rewards and the location assignment all cleared.
void ChecklistRewards_DebugClearAll(void);

// The checklist cell under the cursor; 0 when no checklist is up or the cursor is off
// the grid.
int ChecklistRewards_GetHoveredCell(u8 *out_mode, u8 *out_clear_kind);

// The (source mode, reward_index) placed at a cell, cross-mode aware. 0 if the cell
// hosts none.
int ChecklistRewards_ResolveCell(u8 mode, u8 clear_kind,
                                 u8 *out_source_mode, u8 *out_source_reward_index);

// 1 if the cell hosts a reward already received from AP.
int ChecklistRewards_CellHasReceivedReward(u8 mode, u8 clear_kind);

// Number of reward rows for a mode; 0 out of range.
int ChecklistRewards_GetRewardCount(GameMode mode);

// AP reward_index (the apworld's clear_kind-sorted order, item IDs 500-649) -> game
// reward-table index. Out-of-range input passes through.
u8 ChecklistRewards_ApToGameIndex(GameMode mode, u8 ap_reward_index);

// shuffled_rewards[mode][index]: (target row << 8) | clear_kind, 0xFFFF = remote or out
// of range.
u16 ChecklistRewards_GetShuffledReward(GameMode mode, u8 reward_index);

#endif // ARCHIPELAGO_CHECKLIST_REWARDS_H

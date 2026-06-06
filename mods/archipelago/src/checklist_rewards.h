#ifndef ARCHIPELAGO_CHECKLIST_REWARDS_H
#define ARCHIPELAGO_CHECKLIST_REWARDS_H

#include "game.h"
#include "main.h"  // CLEAR_KIND_NUM, GMMODE_NUM, ap_save

// Allocate reward tables and install all checklist hooks. Call from OnBoot.
void ChecklistRewards_OnBoot(void);

// Initialize checklist-owned save fields on fresh save creation.
// Call from OnSaveInit, after the top-level memset of ap_save.
void ChecklistRewards_OnSaveInit(void);

// Restore reward tables and received rewards from save data. Call from OnSaveLoaded.
void ChecklistRewards_OnSaveLoaded(void);

// Grant a checklist reward received from the AP server. `announce=1` shows the
// "Received: …" textbox; pass 0 from re-grant paths (save-load restoration,
// post-shuffle re-apply) where the player isn't actually receiving anything new.
void ChecklistRewards_Grant(GameMode mode, u8 reward_index, int announce);

// Announce a checkbox filler grant on the TextBox: "Received: Checkbox Filler
// (<Mode>)". Does not mutate filler state — call Checklist_GrantFiller for that.
void Checklist_AnnounceFiller(GameMode mode);

// Apply the AP location assignment from APData.
void ChecklistRewards_ApplyLocations(void);

// Reveal and unlock all checklist squares across all modes (debug/option).
void RevealAllChecklists(void);

// checklist_rewards_gated off: mark every non-progression (cosmetic, no-gate-mask) checklist reward
// as received at connect, so the content is available from the start and its box is freed for an
// ordinary item. Progression Dragoon/Hydra part markers and gated-category rewards are left alone.
void ChecklistRewards_GrantAllCosmetic(void);

// Debug: fill APData location arrays with a random shuffle
// (~1/3 same-mode, ~1/3 cross-mode, ~1/3 remote) and apply immediately.
// For standalone testing without an AP client connection.
void ChecklistRewards_DebugSimulateLocationData(void);

// Debug: full checklist reset. Zeros every checkbox flag across all modes,
// wipes sent_checks / received rewards / goal_complete, clears the
// location/shuffle assignment in both save and shared memory, and resets
// the mod's reward tables and cross-mode slots to the empty state.
void ChecklistRewards_DebugClearAll(void);

// Debug helper: returns the currently-hovered cell in the checklist menu
// (captured by the UpdateCellInfo hook whenever the cursor moves). Returns 1
// on success with `*out_mode` and `*out_clear_kind` populated, 0 if no cell
// has been hovered yet this scene.
int ChecklistRewards_GetHoveredCell(u8 *out_mode, u8 *out_clear_kind);

// Resolve which reward is placed at (mode, clear_kind). On hit, writes the
// source mode and reward_index (same-mode: source_mode == mode; cross-mode:
// source_mode != mode). Returns 0 if the cell has no local placement.
int ChecklistRewards_ResolveCell(u8 mode, u8 clear_kind,
                                 u8 *out_source_mode, u8 *out_source_reward_index);

// Returns 1 if a reward is placed at (mode, clear_kind) AND that reward has
// already been received from AP. Fast path for "should this cell show the
// received reward badge?" — used by the backfill path in check_detection.
int ChecklistRewards_CellHasReceivedReward(u8 mode, u8 clear_kind);

// Number of reward rows for the given mode. Out-of-range modes return 0.
int ChecklistRewards_GetRewardCount(GameMode mode);

// Encoded `shuffled_rewards[mode][index]` for a given (mode, reward_index).
// Encoding: high byte = target mode, low byte = target clear_kind, 0xFFFF =
// remote (no local placement). Out-of-range inputs return 0xFFFF.
u16 ChecklistRewards_GetShuffledReward(GameMode mode, u8 reward_index);

#endif // ARCHIPELAGO_CHECKLIST_REWARDS_H

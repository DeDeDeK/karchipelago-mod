#ifndef SHUFFLE_REWARDS_H
#define SHUFFLE_REWARDS_H

// Allocate new reward tables and redirect pointers. Call from OnBoot (persistent alloc).
void ShuffleRewards_OnBoot();

// Clear tables and store to save data. Call from OnSaveInit (first boot).
void ShuffleRewards_OnSaveInit();

// Load saved reward data or initialize if needed. Call from OnSaveLoaded (subsequent boots).
void ShuffleRewards_OnSaveLoaded();

// Set all reward table clear_kind values to 0 (no checklist square triggers a reward).
// clear_kind=0 is a safe sentinel; hooks on CheckUnlocked and SetRewardFlagOnUnlocks
// prevent all issues with rewards pointing to square 0.
// Operates only on the in-memory tables — does not touch save data.
void ShuffleRewards_ClearAll();

// Reveal all checklist squares across all 3 modes. Call after save data is loaded.
void RevealAllChecklists();

#endif // SHUFFLE_REWARDS_H

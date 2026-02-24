#ifndef SHUFFLE_REWARDS_H
#define SHUFFLE_REWARDS_H

// Allocate new reward tables and redirect pointers. Call from OnBoot (persistent alloc).
void ShuffleRewards_OnBoot();

// Shuffle and store to save data. Call from OnSaveInit (first boot).
void ShuffleRewards_OnSaveInit();

// Load saved shuffle data or shuffle if needed. Call from OnSaveLoaded (subsequent boots).
void ShuffleRewards_OnSaveLoaded();

#endif // SHUFFLE_REWARDS_H

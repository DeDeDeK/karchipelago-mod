#include "game.h"
#include "stadium.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "stadium_lock.h"

// Apply patches needed for stadium locking
void StadiumLock_OnBoot() {
    OSReport("Applying stadium lock patches...\n");
    // Replace CityTrial_CheckIfStadiumIsDefaultUnlocked (hardcoded switch for 18 stadiums)
    // with Gm_StadiumCheckUnlocked (reads the runtime bitfield). This makes
    // CityTrial_DecideStadium respect our AP unlock state.
    CODEPATCH_REPLACEFUNC(0x8000C148, Gm_StadiumCheckUnlocked);
}

// Sync the game's stadium bitfield to match our saved unlock mask.
// The game persists the bitfield in its own save data, but we override it
// from our own saved mask to ensure AP unlocks are correctly applied.
void StadiumLock_OnSaveLoaded() {
    OSReport("Syncing stadium unlocks from save (mask=0x%08X).\n", save_data->stadium_unlocked_mask);
    for (int i = 0; i < STKIND_NUM; i++) {
        if (save_data->stadium_unlocked_mask & (1 << i)) {
            Gm_StadiumSetUnlockedDirect(i);
        } else {
            Gm_StadiumClearUnlockedDirect(i);
            Gm_StadiumClearNewLabelDirect(i);
        }
    }
}

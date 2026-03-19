#include "game.h"
#include "os.h"
#include "inline.h"

#include "main.h"
#include "shuffle_rewards.h"

static const int reward_counts[GMMODE_NUM] = {
    [GMMODE_AIRRIDE]   = REWARD_COUNT_AIRRIDE,
    [GMMODE_TOPRIDE]   = REWARD_COUNT_TOPRIDE,
    [GMMODE_CITYTRIAL] = REWARD_COUNT_CITYTRIAL,
};

// Pointers to our allocated tables (set in OnBoot, used in OnSaveInit/OnSaveLoaded)
static RewardEntry *new_tables[GMMODE_NUM];

// Per-mode pointers into save data for the shuffled clear_kind arrays
static u8 *save_arrays[GMMODE_NUM];

static void InitSaveArrayPtrs()
{
    save_arrays[GMMODE_AIRRIDE]   = save_data->shuffled_airride_rewards;
    save_arrays[GMMODE_TOPRIDE]   = save_data->shuffled_topride_rewards;
    save_arrays[GMMODE_CITYTRIAL] = save_data->shuffled_citytrial_rewards;
}

// Clear all reward tables and mark as initialized in save data.
// The AP client assigns reward mappings via ChecklistRewards_ApplyLocations;
// the game side just starts with a blank slate.
static void InitAndStore()
{
    InitSaveArrayPtrs();
    ShuffleRewards_ClearAll();
    save_data->rewards_shuffled = 1;
    OSReport("Reward tables cleared and marked initialized\n");
}

// Allocate new reward tables, copy originals, and redirect pointers.
// The copies allow the AP client to reassign clear_kind values without
// modifying the original ROM data.
// Must be called from OnBoot so allocations persist for the entire runtime.
void ShuffleRewards_OnBoot()
{
    for (int mode = GMMODE_AIRRIDE; mode < GMMODE_NUM; mode++)
    {
        int size = reward_counts[mode] * sizeof(RewardEntry);
        new_tables[mode] = HSD_MemAlloc(size);
        memcpy(new_tables[mode], stc_reward_table_ptrs[mode], size);
        stc_reward_table_ptrs[mode] = new_tables[mode];
    }
    OSReport("Reward tables allocated and pointers redirected\n");
}

// First boot: clear tables and store. Called from OnSaveInit.
void ShuffleRewards_OnSaveInit()
{
    InitAndStore();
}

// Set all clear_kind values to 0 across every mode's reward table.
// clear_kind=0 is a safe sentinel (valid index, no OOB or assert crash).
// Our hooks on CheckUnlocked and SetRewardFlagOnUnlocks prevent all issues:
//   - CheckUnlocked gates on has_local_location (returns 0 for unassigned rewards)
//   - SetRewardFlagOnUnlocks hook skips rewards without has_local_location
// Does not write to save data — for testing only.
void ShuffleRewards_ClearAll()
{
    for (int mode = GMMODE_AIRRIDE; mode < GMMODE_NUM; mode++)
    {
        for (int i = 0; i < reward_counts[mode]; i++)
            new_tables[mode][i].clear_kind = 0;
    }
    OSReport("Cleared all reward clear_kinds (no rewards mode)\n");
}

// Subsequent boots: load saved reward data. Called from OnSaveLoaded.
void ShuffleRewards_OnSaveLoaded()
{
    InitSaveArrayPtrs();
    if (save_data->rewards_shuffled)
    {
        // Restore saved clear_kind values into our allocated tables
        for (int mode = GMMODE_AIRRIDE; mode < GMMODE_NUM; mode++)
        {
            for (int i = 0; i < reward_counts[mode]; i++)
                new_tables[mode][i].clear_kind = save_arrays[mode][i];
        }
        OSReport("Loaded reward tables from save\n");
    }
    else
    {
        // Fallback: if OnSaveInit didn't run, initialize now
        InitAndStore();
    }
}

// Sets the is_visible bit on every checkbox across all 3 modes.
// The game persists checklist state in its own save, so this only needs to run once.
void RevealAllChecklists()
{
    for (int mode = 0; mode < GMMODE_NUM; mode++)
    {
        GameClearData *clear_data = gmGetClearcheckerTypeP(mode);
        for (int i = 0; i < 120; i++)
        {
            clear_data->clear[i].is_visible = 1;
            // debug also unlock so we can see the rewards
            clear_data->clear[i].is_unlocked = 1;
        }
    }
    OSReport("All checklist squares revealed (%d modes x 120 squares)\n", GMMODE_NUM);
}

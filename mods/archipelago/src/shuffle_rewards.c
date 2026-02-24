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

// Shuffle clear_kind values within a reward table using Fisher-Yates
static void ShuffleRewardTable(RewardEntry *table, int count)
{
    for (int i = count - 1; i > 0; i--) {
        int j = HSD_Randi(i + 1);
        u8 tmp = table[i].clear_kind;
        table[i].clear_kind = table[j].clear_kind;
        table[j].clear_kind = tmp;
    }
}

// Shuffle all tables and store the results to save data
static void ShuffleAndStore()
{
    InitSaveArrayPtrs();
    for (int mode = GMMODE_AIRRIDE; mode < GMMODE_NUM; mode++) {
        ShuffleRewardTable(new_tables[mode], reward_counts[mode]);
        for (int i = 0; i < reward_counts[mode]; i++) {
            save_arrays[mode][i] = new_tables[mode][i].clear_kind;
        }
    }
    save_data->rewards_shuffled = 1;
    OSReport("Shuffled reward tables and saved to memory card\n");
}

// Allocate new reward tables, copy originals, and redirect pointers.
// Must be called from OnBoot so allocations persist for the entire runtime.
void ShuffleRewards_OnBoot()
{
    for (int mode = GMMODE_AIRRIDE; mode < GMMODE_NUM; mode++) {
        int size = reward_counts[mode] * sizeof(RewardEntry);
        new_tables[mode] = HSD_MemAlloc(size);
        memcpy(new_tables[mode], stc_reward_table_ptrs[mode], size);
        stc_reward_table_ptrs[mode] = new_tables[mode];
    }
    OSReport("Reward tables allocated and pointers redirected\n");
}

// First boot: shuffle and store. Called from OnSaveInit.
void ShuffleRewards_OnSaveInit()
{
    ShuffleAndStore();
}

// Subsequent boots: load saved shuffle data. Called from OnSaveLoaded.
void ShuffleRewards_OnSaveLoaded()
{
    InitSaveArrayPtrs();
    if (save_data->rewards_shuffled) {
        // Restore saved clear_kind values into our allocated tables
        for (int mode = GMMODE_AIRRIDE; mode < GMMODE_NUM; mode++) {
            for (int i = 0; i < reward_counts[mode]; i++) {
                new_tables[mode][i].clear_kind = save_arrays[mode][i];
            }
        }
        OSReport("Loaded shuffled reward tables from save\n");
    } else {
        // Fallback: if OnSaveInit didn't run, shuffle now
        ShuffleAndStore();
    }
}

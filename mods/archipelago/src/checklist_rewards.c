#include "game.h"
#include "os.h"
#include "code_patch/code_patch.h"
#include "hoshi/func.h"

#include "main.h"
#include "checklist_rewards.h"

static const int reward_counts[GMMODE_NUM] = {
    [GMMODE_AIRRIDE]   = REWARD_COUNT_AIRRIDE,
    [GMMODE_TOPRIDE]   = REWARD_COUNT_TOPRIDE,
    [GMMODE_CITYTRIAL] = REWARD_COUNT_CITYTRIAL,
};

// Replacement for ClearChecker_CheckUnlocked (0x80049E24).
// Checks the AP received bitfield first; falls back to the original clear_kind check.
// Installed via CODEPATCH_REPLACEFUNC in ChecklistRewards_OnBoot.
int ChecklistRewards_CheckUnlocked(GameMode mode, u8 reward_index)
{
    if (save_data->received_checklist_rewards[mode] & (1ULL << reward_index))
        return 1;
    // Only check the local clear[] if this reward has a local checklist slot.
    // Remote rewards (clear_kind=0 sentinel) must not fall through here,
    // or completing square 0 would falsely unlock every remote reward.
    if (!(save_data->has_local_location[mode] & (1ULL << reward_index)))
        return 0;
    GameClearData *cd = gmGetClearcheckerTypeP(mode);
    u8 kind = stc_reward_table_ptrs[mode][reward_index].clear_kind;
    return cd->clear[kind].has_reward;
}

// Grant a checklist reward received from the AP server.
// Sets the AP bitfield, marks the local checklist slot if one is assigned,
// and updates the City Trial session cache if it is currently valid.
void ChecklistRewards_Grant(GameMode mode, u8 reward_index)
{
    save_data->received_checklist_rewards[mode] |= (1ULL << reward_index);

    if (save_data->has_local_location[mode] & (1ULL << reward_index))
    {
        u8 kind = stc_reward_table_ptrs[mode][reward_index].clear_kind;
        GameClearData *cd = gmGetClearcheckerTypeP(mode);
        cd->clear[kind].has_reward = 1;
        cd->clear[kind].is_unlocked = 1;
    }

    // Top Ride has no cache slot; only Air Ride and City Trial need the cache update.
    if (mode != GMMODE_TOPRIDE && Checklist_IsCacheValid())
    {
        GameData *gd = Gm_GetGameData();
        if (mode == GMMODE_AIRRIDE)
        {
            if (reward_index < 32)
                gd->unlock_cache.airride_unlock_lo |= (1u << reward_index);
            else
                gd->unlock_cache.airride_unlock_hi |= (1u << (reward_index - 32));
        }
        else // GMMODE_CITYTRIAL
        {
            if (reward_index < 32)
                gd->unlock_cache.citytrial_unlock_lo |= (1u << reward_index);
            else
                gd->unlock_cache.citytrial_unlock_hi |= (1u << (reward_index - 32));
        }
    }
}

// Filter for the reward loop in Checklist_SetRewardFlagOnUnlocks (0x8017DF5C).
// Returns 1 to skip (remote reward, no local checklist slot),
// 0 to process normally (local reward with an assigned checkbox).
// Called from the conditional hook at the top of the reward loop body.
int ChecklistRewards_ShouldSkipReward(GameMode mode, u8 reward_index)
{
    return !(save_data->has_local_location[mode] & (1ULL << reward_index));
}

// Hook at 0x8017dfd8: top of the reward loop body in Checklist_SetRewardFlagOnUnlocks.
// Clobbered instruction: lbz r3, 0x14(r30)  (loads mode from checklist UI struct).
// Normal exit (filter returns 0): execute clobbered insn, continue loop body at 0x8017dfdc.
// Alt exit (filter returns 1): skip to next iteration at 0x8017e064.
CODEPATCH_HOOKCONDITIONALCREATE(
    0x8017dfd8,
    "lbz 3, 0x14(30)\n\t"
    "mr 4, 27\n\t",
    ChecklistRewards_ShouldSkipReward,
    "",
    0,
    0x8017e064
)

// Install the ClearChecker_CheckUnlocked replacement. Call from OnBoot.
void ChecklistRewards_OnBoot()
{
    CODEPATCH_REPLACEFUNC(ClearChecker_CheckUnlocked, ChecklistRewards_CheckUnlocked);
    CODEPATCH_HOOKAPPLY(0x8017dfd8);
    OSReport("ClearChecker_CheckUnlocked replaced with AP checklist hook\n");
    OSReport("Checklist_SetRewardFlagOnUnlocks reward loop filtered for AP\n");
}

// Restore all received checklist rewards from save data into the live game state.
// Call from OnSaveLoaded, after ShuffleRewards_OnSaveLoaded has restored clear_kind values.
void ChecklistRewards_OnSaveLoaded()
{
    for (int mode = 0; mode < GMMODE_NUM; mode++)
    {
        u64 received = save_data->received_checklist_rewards[mode];
        while (received)
        {
            int idx = __builtin_ctzll(received);
            ChecklistRewards_Grant(mode, (u8)idx);
            received &= received - 1;
        }
    }
    OSReport("Checklist rewards restored from save\n");
}

// Apply the AP location assignment written by the Python client to ArchipelagoData.
// Updates RewardEntry.clear_kind for each reward_index, persists to save data, and
// re-applies grants for any items already received before location data arrived.
// The location_* arrays use 0xFF to indicate a reward has no local checklist slot
// (it will be received from another world rather than from a local checkpoint).
void ChecklistRewards_ApplyLocations()
{
    const u8 *location_arrays[GMMODE_NUM] = {
        archipelago_data->location_airride,
        archipelago_data->location_topride,
        archipelago_data->location_citytrial,
    };
    u8 *save_shuffled[GMMODE_NUM] = {
        save_data->shuffled_airride_rewards,
        save_data->shuffled_topride_rewards,
        save_data->shuffled_citytrial_rewards,
    };

    // Reset has_local_location before repopulating from the new assignment.
    for (int mode = 0; mode < GMMODE_NUM; mode++)
        save_data->has_local_location[mode] = 0;

    for (int mode = 0; mode < GMMODE_NUM; mode++)
    {
        int count = reward_counts[mode];
        for (int i = 0; i < count; i++)
        {
            u8 loc = location_arrays[mode][i];
            if (loc != 0xFF)
            {
                // Reward is placed at a local checklist slot.
                stc_reward_table_ptrs[mode][i].clear_kind = loc;
                save_shuffled[mode][i] = loc;
                save_data->has_local_location[mode] |= (1ULL << i);
            }
            else
            {
                // Reward comes from another world — no local slot.
                // Set clear_kind to 0 as a safe sentinel (valid index, no OOB).
                // 0xFF would be semantically cleaner but causes an assert crash in
                // unhooked vanilla functions that use clear_kind as an array index.
                // Our hooks prevent all issues with clear_kind=0:
                //   - CheckUnlocked gates on has_local_location (won't read clear[0])
                //   - SetRewardFlagOnUnlocks hook skips remote rewards (won't set has_reward)
                stc_reward_table_ptrs[mode][i].clear_kind = 0;
                save_shuffled[mode][i] = 0;
            }
        }
    }

    // Re-apply grants for items already received before location data arrived,
    // so their local checklist slots are correctly marked now that has_local_location is set.
    for (int mode = 0; mode < GMMODE_NUM; mode++)
    {
        u64 received = save_data->received_checklist_rewards[mode];
        while (received)
        {
            int idx = __builtin_ctzll(received);
            ChecklistRewards_Grant(mode, (u8)idx);
            received &= received - 1;
        }
    }

    save_data->rewards_shuffled = 1;
    archipelago_data->location_data_valid = 0;
    Hoshi_WriteSave();
    OSReport("AP location assignment applied to checklist reward tables\n");
}

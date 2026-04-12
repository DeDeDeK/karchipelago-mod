#include "game.h"
#include "hsd.h"
#include "os.h"
#include "text.h"
#include "inline.h"
#include "code_patch/code_patch.h"
#include "hoshi/func.h"

#include "main.h"
#include "checklist_rewards.h"

const int reward_counts[GMMODE_NUM] = {
    [GMMODE_AIRRIDE]   = REWARD_COUNT_AIRRIDE,
    [GMMODE_TOPRIDE]   = REWARD_COUNT_TOPRIDE,
    [GMMODE_CITYTRIAL] = REWARD_COUNT_CITYTRIAL,
};

// Mod-allocated copies of the per-mode reward tables (originals are in ROM).
static RewardEntry *new_tables[GMMODE_NUM];

CrossModeSlot cross_mode_slots[GMMODE_NUM][120];

// Tracks the source mode of the currently hovered reward in the checklist UI.
// Set by the reward lookup hook, read by the text display hook.
static u8 current_hover_source_mode;

void ChecklistRewards_ClearCrossModeSlots(void)
{
    for (int m = 0; m < GMMODE_NUM; m++)
        for (int k = 0; k < 120; k++)
            cross_mode_slots[m][k].source_mode = 0xFF;
}

// Decode a u16 location value into target_mode and clear_kind.
// Returns 0 for remote (0xFFFF), 1 for valid local assignment.
static int DecodeLocation(u16 loc, u8 *out_target_mode, u8 *out_clear_kind)
{
    if (loc == 0xFFFF)
        return 0;
    *out_target_mode = (u8)(loc >> 8);
    *out_clear_kind = (u8)(loc & 0xFF);
    return 1;
}

// Returns 1 iff reward_index in mode's table has a local placement in THAT
// mode's checklist (not cross-mode, not remote). This is the predicate that
// every vanilla-facing read of RewardEntry.clear_kind must gate on — cross-mode
// source rows store the 0 sentinel in their clear_kind field and must never
// cause a read of the source mode's clear[0].
static int IsSameModeLocalPlacement(u8 mode, u8 reward_index)
{
    if (!(ap_save->has_local_location[mode] & (1ULL << reward_index)))
        return 0;
    u16 loc = ap_save->shuffled_rewards[mode][reward_index];
    if (loc == 0xFFFF)
        return 0;
    return ((u8)(loc >> 8)) == mode;
}

// Replacement for ClearChecker_CheckUnlocked (0x80049E24).
// Order of checks:
//   1. AP received bitfield — covers same-mode, cross-mode, and remote rewards
//      that the player has already received.
//   2. Same-mode local placement gate — only fall through to clear[kind] for
//      rewards actually placed in THIS mode's checklist. Cross-mode source rows
//      (sentinel clear_kind=0) and remote rewards return 0 here, avoiding the
//      clear[0]-aliasing false positive.
// Installed via CODEPATCH_REPLACEFUNC in ChecklistRewards_OnBoot.
int ChecklistRewards_CheckUnlocked(GameMode mode, u8 reward_index)
{
    if (ap_save->received_checklist_rewards[mode] & (1ULL << reward_index))
        return 1;
    if (!IsSameModeLocalPlacement((u8)mode, reward_index))
        return 0;
    GameClearData *cd = gmGetClearcheckerTypeP(mode);
    u8 kind = stc_reward_table_ptrs[mode][reward_index].clear_kind;
    return cd->clear[kind].has_reward;
}

// Find the cross-mode placement for a reward, if any.
// Returns the target clear_kind and writes target_mode, or returns -1 if not cross-mode.
static int FindCrossModePlacement(GameMode source_mode, u8 source_reward_index, int *out_target_mode)
{
    for (int tm = 0; tm < GMMODE_NUM; tm++)
    {
        for (int ck = 0; ck < 120; ck++)
        {
            CrossModeSlot *slot = &cross_mode_slots[tm][ck];
            if (slot->source_mode == (u8)source_mode &&
                slot->source_reward_index == source_reward_index)
            {
                *out_target_mode = tm;
                return ck;
            }
        }
    }
    return -1;
}

// Grant a checklist reward received from the AP server.
// Sets the AP bitfield, marks the local checklist slot if one is assigned
// (same-mode or cross-mode), and updates the bitfield cache.
void ChecklistRewards_Grant(GameMode mode, u8 reward_index)
{
    ap_save->received_checklist_rewards[mode] |= (1ULL << reward_index);

    if (ap_save->has_local_location[mode] & (1ULL << reward_index))
    {
        int target_mode;
        int cross_ck = FindCrossModePlacement(mode, reward_index, &target_mode);

        if (cross_ck >= 0)
        {
            // Cross-mode: set has_reward on the target mode's checkbox.
            // Do NOT set is_unlocked — that bit is reserved for "player
            // completed this in gameplay" and is the source of truth for
            // outbound check detection (check_detection.c).
            GameClearData *cd = gmGetClearcheckerTypeP(target_mode);
            cd->clear[cross_ck].has_reward = 1;
        }
        else
        {
            // Same-mode: clear_kind is in this mode's reward table.
            u8 kind = stc_reward_table_ptrs[mode][reward_index].clear_kind;
            GameClearData *cd = gmGetClearcheckerTypeP(mode);
            cd->clear[kind].has_reward = 1;
        }
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
// Returns 1 to skip, 0 to process normally. We skip every reward that isn't a
// same-mode local placement — remote rewards and cross-mode-source rows both
// have their stored clear_kind set to the 0 sentinel, and letting vanilla read
// clear[0] via GetClearKindFromRewardIndex would spuriously set has_reward on
// clear[mode][0] whenever the player completes or fillers that checkbox.
// Cross-mode placements are handled post-loop by ApplyCrossModeHasReward.
int ChecklistRewards_ShouldSkipReward(GameMode mode, u8 reward_index)
{
    return !IsSameModeLocalPlacement((u8)mode, reward_index);
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

// SIS file names per mode (indexed by GameMode).
static const char *sis_filenames[GMMODE_NUM] = {
    "SisClrChk3D.dat",  // GMMODE_AIRRIDE
    "SisClrChk2D.dat",  // GMMODE_TOPRIDE
    "SisClrChkCT.dat",  // GMMODE_CITYTRIAL
};

// Maps GameMode -> SIS slot index. Slot 0 is always the current mode (so all
// vanilla code with default sis_id=0 works). The other two modes get slots 1-2.
static u8 mode_to_sis_slot[GMMODE_NUM];

// The current checklist mode, set during SIS loading.
static u8 checklist_current_mode;

// Load all 3 checklist SIS files. The current mode goes into slot 0 (so vanilla
// code works), the other two into slots 1 and 2.
static void LoadAllChecklistSIS(void)
{
    // Current mode goes into slot 0
    Text_LoadSisFile(0, (char *)sis_filenames[checklist_current_mode], "SIS_Clearchecker");
    mode_to_sis_slot[checklist_current_mode] = 0;

    // Other two modes go into slots 1 and 2
    int slot = 1;
    for (int m = 0; m < GMMODE_NUM; m++)
    {
        if (m == checklist_current_mode)
            continue;
        Text_LoadSisFile(slot, (char *)sis_filenames[m], "SIS_Clearchecker");
        mode_to_sis_slot[m] = (u8)slot;
        slot++;
    }
}

// Wrapper that sets checklist_current_mode from the register value, then loads.
static void LoadAllChecklistSIS_WithMode(u8 mode)
{
    checklist_current_mode = mode;
    LoadAllChecklistSIS();
}

// Hook at 0x801823c4: convergence point after the mode-specific SIS load in
// checklist init (zz_801822f4_). We NOP the 3 original Text_LoadSisFile calls
// and load all 3 SIS files here instead.
// r23 = current checklist mode (set at function entry)
// Clobbered instruction: lwz r3, 0x0ecc(r30)
CODEPATCH_HOOKCREATE(
    0x801823c4,
    "clrlwi 3, 23, 24\n\t",  // r3 = mode (from r23)
    LoadAllChecklistSIS_WithMode,
    "lwz 3, 0x0ecc(30)\n\t",
    0
)

// Hook at 0x80181ee4 in Checklist_UpdateCellInfo: intercept the reward_index
// reverse lookup. Replaces the vanilla scan entirely — we always alt-exit past
// it so vanilla never reads RewardEntry.clear_kind (which would alias cross-mode
// sentinel rows against clear_kind=0). Handles cross-mode via cross_mode_slots
// and same-mode via the save_shuffled encoding.
// Clobbered instruction: li r25, 0  (init loop counter before vanilla scan).
// Returns: positive = reward_index + 1 (unlocked, display this reward)
//          negative = -1 (no reward visible at this cell)
static int ChecklistRewards_FindRewardForCell(u8 current_mode, u8 clear_kind)
{
    // Cross-mode first: a reward from another mode may be placed at this cell.
    CrossModeSlot *slot = &cross_mode_slots[current_mode][clear_kind];
    if (slot->source_mode != 0xFF)
    {
        current_hover_source_mode = slot->source_mode;
        if (ap_save->received_checklist_rewards[slot->source_mode]
            & (1ULL << slot->source_reward_index))
            return (int)slot->source_reward_index + 1;
        GameClearData *cd = gmGetClearcheckerTypeP(current_mode);
        if (cd->clear[clear_kind].is_unlocked || cd->clear[clear_kind].has_reward)
            return (int)slot->source_reward_index + 1;
        return -1;
    }

    // Same-mode: look up via save_shuffled (not RewardEntry.clear_kind — that
    // field holds the sentinel 0 for cross-mode source rows and would alias).
    current_hover_source_mode = current_mode;
    u16 target = ((u16)current_mode << 8) | clear_kind;
    int count = reward_counts[current_mode];
    for (int ri = 0; ri < count; ri++)
    {
        if (ap_save->shuffled_rewards[current_mode][ri] != target)
            continue;
        // Found a same-mode placement at this cell. Mirror CheckUnlocked logic:
        // AP received bit first, then clear[].has_reward.
        if (ap_save->received_checklist_rewards[current_mode] & (1ULL << ri))
            return ri + 1;
        GameClearData *cd = gmGetClearcheckerTypeP(current_mode);
        if (cd->clear[clear_kind].has_reward)
            return ri + 1;
        return -1;
    }
    return -1; // Empty cell — no reward placed here.
}

// Hook at 0x80181ee4: replaces the entire vanilla reverse-lookup scan.
// Clobbered instruction: li r25, 0
// Args: r3 = mode (from r30+0x14), r4 = clear_kind (r26)
// Return handling: r3 = reward_index + 1 if unlocked, -1 if locked/empty.
// Epilogue sets r0 = reward_index (or -1), then alt-exits to 0x80181f5c where
// vanilla does `mr r27, r0`.
CODEPATCH_HOOKCREATE(
    0x80181ee4,
    "lbz 3, 0x14(30)\n\t"
    "mr 4, 26\n\t",
    ChecklistRewards_FindRewardForCell,
    "cmpwi 3, 0\n\t"
    "blt 0f\n\t"
    "addi 0, 3, -1\n\t"  // r0 = reward_index (undo the +1)
    "b 1f\n\t"
    "0:\n\t"
    "li 0, -1\n\t"       // r0 = -1 (no visible reward)
    "1:\n\t",
    0x80181f5c           // alt exit: skip past vanilla loop + unlock check
)

// Hook at 0x8018201c: reward text display in Checklist_UpdateCellInfo.
// Vanilla code:
//   8018201c: lwz r3, 12(r29)       -- text object
//   80182020: addi r4, r27, 0x7D    -- text_index = reward_index + 125
//   80182024: bl Text_FinalizeSisText
// We replace this entire sequence to handle cross-mode sis_id switching.
// r27 = reward_index (set by our lookup hook or vanilla loop)
// r29 = UI data struct (text object at +0x0C)
// r30 = checklist UI struct (mode at +0x14)
static void ChecklistRewards_DisplayRewardText(Text *text, int reward_index, u8 current_mode)
{
    // Temporarily switch sis_id to the source mode's slot to read the correct
    // text command data, then restore to slot 0 so Text_GX uses slot 0's
    // glyph/font data for rendering (all checklist SIS files share the same font).
    u8 source_slot = mode_to_sis_slot[current_hover_source_mode];
    text->sis_id = source_slot;
    Text_FinalizeSisText(text, reward_index + 0x7D);
    text->sis_id = 0;
}

CODEPATCH_HOOKCREATE(
    0x8018201c,
    "lwz 3, 0x0c(29)\n\t"   // text object -> r3
    "mr 4, 27\n\t"           // reward_index -> r4
    "lbz 5, 0x14(30)\n\t",  // current_mode -> r5
    ChecklistRewards_DisplayRewardText,
    "",
    0x80182028               // skip past the vanilla lwz + addi + bl sequence
)

// Hook for blank text sis_id: ensure sis_id is reset to 0 (current mode's slot)
// after a cross-mode reward may have changed it. The reward text hook sets sis_id
// to the source mode's slot, so we must restore it here.
// Hook at 0x80181f8c: blank text path (lwz r3, 12(r29) before li r4, 0x7c).
static void ChecklistRewards_SetBlankTextSisId(Text *text, u8 current_mode)
{
    text->sis_id = 0; // Slot 0 is always the current mode
    Text_FinalizeSisText(text, 0x7C);
}

CODEPATCH_HOOKCREATE(
    0x80181f8c,
    "lwz 3, 0x0c(29)\n\t"   // text object -> r3
    "lbz 4, 0x14(30)\n\t",  // current_mode -> r4
    ChecklistRewards_SetBlankTextSisId,
    "",
    0x80181f98               // skip past vanilla lwz + li + bl
)

// Hook at 0x80182170: reward type icon lookup in the icon display function
// (0x801820B4). This function runs every frame to update the reward type icon
// shown next to the reward text.
//
// Vanilla sequence:
//   80182170: lbz r4, 0x14(r29)    -- reward_index
//   80182174: lbz r3, 0x14(r31)    -- mode (current checklist mode — WRONG for cross-mode)
//   80182178: bl 0x80049d10         -- returns reward_type from stc_reward_table_ptrs[mode][reward_index]
//
// For cross-mode rewards, reward_index is the source mode's index but mode is the
// current checklist mode, causing a lookup in the wrong reward table. We hook at
// 0x80182170, return source_mode in r3, let the clobbered instruction reload r4
// (reward_index), and skip past the vanilla mode load to 0x80182178.
static u8 ChecklistRewards_GetHoverSourceMode(void)
{
    return current_hover_source_mode;
}

CODEPATCH_HOOKCREATE(
    0x80182170,
    "",                         // no prologue needed
    ChecklistRewards_GetHoverSourceMode,
    "",                         // no epilogue needed — r3 = source_mode from return
    0x80182178                  // skip vanilla mode load at 0x80182174, go straight to bl
)

// Post-reward-loop hook: set has_reward on cross-mode reward checkboxes, and
// grant a checkbox filler when the cross-mode placement is a REWARD_FILLER.
//
// Vanilla's reward loop in Checklist_SetRewardFlagOnUnlocks only iterates the
// current mode's reward table, and only grants a filler when the reward_index
// appears in stc_special_rewards[current_mode] — which in vanilla is just a
// hardcoded {0,1,2,3,4} list pointing at the first 5 rewards of each mode,
// all of which happen to have reward_type == REWARD_FILLER. It's functionally
// equivalent to "grant a filler iff the reward is REWARD_FILLER."
//
// Cross-mode placements are skipped by ShouldSkipReward in the vanilla loop,
// so we replicate both has_reward and the filler grant here. We check
// reward_type directly instead of scanning stc_special_rewards — simpler,
// and the filler goes to current_mode (the mode whose cell was completed)
// per the semantic "complete an objective, earn a filler in that checklist."
static void ChecklistRewards_ApplyCrossModeHasReward(u8 current_mode)
{
    GameClearData *cd = gmGetClearcheckerTypeP(current_mode);
    for (int ck = 0; ck < 120; ck++)
    {
        CrossModeSlot *slot = &cross_mode_slots[current_mode][ck];
        if (slot->source_mode == 0xFF)
            continue;
        if (cd->clear[ck].has_reward)
            continue;  // Already processed on a prior pass — don't double-grant.
        if (!(cd->clear[ck].is_unlocked || cd->clear[ck].is_filler))
            continue;

        cd->clear[ck].has_reward = 1;

        RewardEntry *src = &stc_reward_table_ptrs[slot->source_mode][slot->source_reward_index];
        if (src->reward_type == REWARD_FILLER)
            Checklist_GrantFiller((GameMode)current_mode);
    }
}

// Hook after the reward loop in Checklist_SetRewardFlagOnUnlocks.
// The outer loop condition at 0x8017e078 branches back to 0x8017dfd8.
// When the loop exits (reward_index >= count), execution falls through to 0x8017e07c.
// Clobbered instruction: lbz r0, 0(r31)
CODEPATCH_HOOKCREATE(
    0x8017e07c,
    "lbz 3, 0x14(30)\n\t",  // current_mode -> r3
    ChecklistRewards_ApplyCrossModeHasReward,
    "lbz 0, 0(31)\n\t",     // re-execute clobbered instruction
    0
)

// Re-grant all received rewards so their checklist slots are correctly marked.
// Called after restoring from save and after applying new location data.
static void RegrantAllReceivedRewards(void)
{
    for (int mode = 0; mode < GMMODE_NUM; mode++)
    {
        u64 received = ap_save->received_checklist_rewards[mode];
        while (received)
        {
            int idx = __builtin_ctzll(received);
            ChecklistRewards_Grant(mode, (u8)idx);
            received &= received - 1;
        }
    }
}


// Allocate new reward tables, copy originals, and redirect pointers.
// Must be called from OnBoot so allocations persist for the entire runtime.
static void AllocateRewardTables(void)
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

// Restore saved reward data from memory card. Rebuilds clear_kind values for
// same-mode rewards and cross_mode_slots for cross-mode rewards.
static void RestoreRewardTablesFromSave(void)
{
    ChecklistRewards_ClearCrossModeSlots();
    for (int source_mode = GMMODE_AIRRIDE; source_mode < GMMODE_NUM; source_mode++)
    {
        for (int i = 0; i < reward_counts[source_mode]; i++)
        {
            u16 loc = ap_save->shuffled_rewards[source_mode][i];
            if (loc == 0xFFFF || loc == 0)
            {
                new_tables[source_mode][i].clear_kind = 0;
                continue;
            }
            u8 target_mode = (u8)(loc >> 8);
            u8 clear_kind = (u8)(loc & 0xFF);
            if (target_mode == (u8)source_mode)
            {
                new_tables[source_mode][i].clear_kind = clear_kind;
            }
            else
            {
                new_tables[source_mode][i].clear_kind = 0;
                cross_mode_slots[target_mode][clear_kind].source_mode = (u8)source_mode;
                cross_mode_slots[target_mode][clear_kind].source_reward_index = (u8)i;
            }
        }
    }
    OSReport("Loaded reward tables from save\n");
}

// Debug: simulate the AP client sending location data by filling the
// APData location arrays with a random shuffle. Rewards are
// distributed roughly:
//   ~33% same-mode   (reward stays in its own mode's checklist)
//   ~33% cross-mode  (reward placed in a different mode's checklist)
//   ~33% remote      (sent to another world, no local checkbox)
// Applies immediately via ChecklistRewards_ApplyLocations so callers
// (debug menu, test paths) see the result without waiting a frame.
void ChecklistRewards_DebugSimulateLocationData(void)
{
    // Build per-mode shuffled pools of clear_kinds (0-119)
    u8 pools[GMMODE_NUM][120];
    int pool_idxs[GMMODE_NUM] = {0, 0, 0};
    for (int m = 0; m < GMMODE_NUM; m++)
    {
        for (int i = 0; i < 120; i++)
            pools[m][i] = (u8)i;
        for (int i = 119; i > 0; i--)
        {
            int j = HSD_Randi(i + 1);
            u8 tmp = pools[m][i];
            pools[m][i] = pools[m][j];
            pools[m][j] = tmp;
        }
    }

    for (int mode = 0; mode < GMMODE_NUM; mode++)
    {
        int count = reward_counts[mode];
        int local_count = 0, cross_count = 0;

        for (int i = 0; i < count; i++)
        {
            int roll = HSD_Randi(3);
            if (roll == 0 && pool_idxs[mode] < 120)
            {
                // Same-mode: reward stays in its own checklist
                u8 ck = pools[mode][pool_idxs[mode]++];
                ap_data->locations[mode][i] = ((u16)mode << 8) | ck;
                local_count++;
            }
            else if (roll == 1)
            {
                // Cross-mode: reward placed in a different mode's checklist
                int target = (mode + 1 + HSD_Randi(2)) % GMMODE_NUM;
                if (pool_idxs[target] < 120)
                {
                    u8 ck = pools[target][pool_idxs[target]++];
                    ap_data->locations[mode][i] = ((u16)target << 8) | ck;
                    cross_count++;
                }
                else
                {
                    ap_data->locations[mode][i] = 0xFFFF;
                }
            }
            else
            {
                // Remote
                ap_data->locations[mode][i] = 0xFFFF;
            }
        }
        OSReport("  Mode %d: %d same, %d cross, %d remote\n",
                 mode, local_count, cross_count,
                 count - local_count - cross_count);
    }

    ap_data->location_data_valid = 1;
    OSReport("Debug: simulated location data written\n");

    // Apply immediately so the result is visible without waiting for the
    // next frame's AP client poll.
    ChecklistRewards_ApplyLocations();
}

// Full checklist reset for debugging. Returns the mod to the same state as a
// fresh boot with no location assignment and no progress:
//   - Every GameClearData checkbox flag byte cleared (is_visible/is_unlocked/
//     has_reward/is_filler/is_new/etc.), plus the per-mode header counters.
//     grid_mapping is left alone (it's a display layout, not progress).
//   - ap_save: received_checklist_rewards / has_local_location / sent_checks
//     / goal_complete / shuffled_*_rewards all zeroed. shuffled_* arrays are
//     filled with 0xFFFF (the "remote / no placement" sentinel).
//   - ap_data: sent_checks / goal_complete / location_* mirrors cleared,
//     location_data_valid cleared so a stale client write doesn't immediately
//     re-apply.
//   - Mod reward tables reset to clear_kind=0 sentinels, cross_mode_slots emptied.
void ChecklistRewards_DebugClearAll(void)
{
    // 1. Mod-side reward tables and cross-mode slot map.
    for (int mode = GMMODE_AIRRIDE; mode < GMMODE_NUM; mode++)
        for (int i = 0; i < reward_counts[mode]; i++)
            new_tables[mode][i].clear_kind = 0;
    ChecklistRewards_ClearCrossModeSlots();
    current_hover_source_mode = 0;

    // 2. Save-side checklist state.
    for (int m = 0; m < GMMODE_NUM; m++)
    {
        ap_save->received_checklist_rewards[m] = 0;
        ap_save->has_local_location[m] = 0;
        ap_save->sent_checks[m][0] = 0;
        ap_save->sent_checks[m][1] = 0;
    }
    for (int m = 0; m < GMMODE_NUM; m++)
        for (int i = 0; i < reward_counts[m]; i++)
            ap_save->shuffled_rewards[m][i] = 0xFFFF;
    ap_save->goal_complete = 0;

    // 3. Shared-memory mirrors the Python client reads.
    for (int m = 0; m < GMMODE_NUM; m++)
    {
        ap_data->sent_checks[m][0] = 0;
        ap_data->sent_checks[m][1] = 0;
    }
    ap_data->goal_complete = 0;
    for (int m = 0; m < GMMODE_NUM; m++)
        for (int i = 0; i < reward_counts[m]; i++)
            ap_data->locations[m][i] = 0xFFFF;
    ap_data->location_data_valid = 0;

    // 4. In-memory GameClearData for each mode: zero checkbox flags + counters.
    // The clear[] entries are packed 8 bitfields into 1 byte each, so a memset
    // of the whole array is a clean full-reset of every flag.
    for (int m = 0; m < GMMODE_NUM; m++)
    {
        GameClearData *cd = gmGetClearcheckerTypeP((GameMode)m);
        if (!cd)
            continue;
        cd->new_unlock_flag = 0;
        cd->display_state = 0;
        cd->checkbox_filler_num = 0;
        cd->checkbox_filler_list_len = 0;
        memset(cd->clear, 0, sizeof(cd->clear));
    }

    // 5. Invalidate the in-game unlock bitfield cache so it rebuilds from the
    // now-empty tables next time a checklist screen opens. Safe regardless of
    // whether the cache is currently marked valid.
    GameData *gd = Gm_GetGameData();
    if (gd)
    {
        gd->unlock_cache.airride_unlock_lo = 0;
        gd->unlock_cache.airride_unlock_hi = 0;
        gd->unlock_cache.citytrial_unlock_lo = 0;
        gd->unlock_cache.citytrial_unlock_hi = 0;
    }

    Hoshi_WriteSave();
    OSReport("[Checklist] Debug: cleared ALL checklist data (flags, sent_checks, rewards, shuffle)\n");
}

// Sets the is_visible and is_unlocked bits on every checkbox across all 3 modes.
void RevealAllChecklists(void)
{
    for (int mode = 0; mode < GMMODE_NUM; mode++)
    {
        GameClearData *clear_data = gmGetClearcheckerTypeP(mode);
        for (int i = 0; i < 120; i++)
        {
            clear_data->clear[i].is_visible = 1;
            //clear_data->clear[i].is_unlocked = 1;
        }
    }
    OSReport("All checklist squares revealed (%d modes x 120 squares)\n", GMMODE_NUM);
}

// Install all checklist hooks. Call from OnBoot.
void ChecklistRewards_OnBoot()
{
    AllocateRewardTables();

    // Core reward system hooks
    CODEPATCH_REPLACEFUNC(ClearChecker_CheckUnlocked, ChecklistRewards_CheckUnlocked);
    CODEPATCH_HOOKAPPLY(0x8017dfd8);  // Skip remote rewards in SetRewardFlagOnUnlocks
    CODEPATCH_HOOKAPPLY(0x8017e07c);  // Post-reward-loop: apply cross-mode has_reward
    OSReport("ClearChecker_CheckUnlocked replaced with AP checklist hook\n");

    // Multi-SIS loading: NOP the 3 original per-mode Text_LoadSisFile calls,
    // and hook the convergence point to load all 3 SIS files.
    CODEPATCH_REPLACEINSTRUCTION(0x80182378, 0x60000000); // NOP: AR bl Text_LoadSisFile
    CODEPATCH_REPLACEINSTRUCTION(0x8018238c, 0x60000000); // NOP: TR bl Text_LoadSisFile
    CODEPATCH_REPLACEINSTRUCTION(0x801823a0, 0x60000000); // NOP: CT bl Text_LoadSisFile
    CODEPATCH_HOOKAPPLY(0x801823c4);  // Load all 3 SIS files
    OSReport("Checklist multi-SIS loading installed\n");

    // Checklist_UpdateCellInfo hooks for cross-mode reward display
    CODEPATCH_HOOKAPPLY(0x80181ee4);  // Cross-mode reward lookup
    CODEPATCH_HOOKAPPLY(0x8018201c);  // Cross-mode reward text display
    CODEPATCH_HOOKAPPLY(0x80181f8c);  // Blank text sis_id fix
    CODEPATCH_HOOKAPPLY(0x80182170);  // Cross-mode reward type icon
    OSReport("Checklist cross-mode UI hooks installed\n");

    ChecklistRewards_ClearCrossModeSlots();
}

// Restore reward tables and received rewards from save data.
// Call from OnSaveLoaded (handles both first boot and subsequent boots).
void ChecklistRewards_OnSaveLoaded(void)
{
    RestoreRewardTablesFromSave();
    RegrantAllReceivedRewards();
    OSReport("Checklist rewards restored from save\n");
}

// Apply the AP location assignment written by the Python client to APData.
// Handles both same-mode and cross-mode reward placements via u16 encoding:
//   (target_mode << 8) | clear_kind  — local reward at target_mode's checklist
//   0xFFFF                           — remote reward (no local slot)
// Updates RewardEntry.clear_kind for same-mode rewards, populates cross_mode_slots
// for cross-mode rewards, persists to save data, and re-applies grants.
void ChecklistRewards_ApplyLocations()
{
    // Reset before repopulating from the new assignment.
    for (int mode = 0; mode < GMMODE_NUM; mode++)
        ap_save->has_local_location[mode] = 0;
    ChecklistRewards_ClearCrossModeSlots();

    for (int source_mode = 0; source_mode < GMMODE_NUM; source_mode++)
    {
        int count = reward_counts[source_mode];
        for (int i = 0; i < count; i++)
        {
            u16 loc = ap_data->locations[source_mode][i];
            ap_save->shuffled_rewards[source_mode][i] = loc;

            if (loc == 0xFFFF)
            {
                // Remote reward — no local slot.
                // Set clear_kind to 0 as a safe sentinel (valid index, no OOB).
                // Our hooks prevent issues: CheckUnlocked gates on has_local_location,
                // SetRewardFlagOnUnlocks hook skips rewards without has_local_location.
                stc_reward_table_ptrs[source_mode][i].clear_kind = 0;
                continue;
            }

            u8 target_mode, clear_kind;
            DecodeLocation(loc, &target_mode, &clear_kind);
            ap_save->has_local_location[source_mode] |= (1ULL << i);

            if (target_mode == (u8)source_mode)
            {
                // Same-mode: reward stays in its own mode's checklist.
                stc_reward_table_ptrs[source_mode][i].clear_kind = clear_kind;
            }
            else
            {
                // Cross-mode: reward placed in a different mode's checklist.
                // Set sentinel in source table; the real placement is in cross_mode_slots.
                stc_reward_table_ptrs[source_mode][i].clear_kind = 0;
                cross_mode_slots[target_mode][clear_kind].source_mode = (u8)source_mode;
                cross_mode_slots[target_mode][clear_kind].source_reward_index = (u8)i;
            }
        }
    }

    // Re-apply grants for items already received before location data arrived,
    // so their local checklist slots are correctly marked now that has_local_location is set.
    RegrantAllReceivedRewards();

    ap_data->location_data_valid = 0;
    Hoshi_WriteSave();
    OSReport("AP location assignment applied to checklist reward tables\n");
}

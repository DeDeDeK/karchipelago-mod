#include "game.h"
#include "os.h"
#include "text.h"
#include "inline.h"
#include "code_patch/code_patch.h"
#include "hoshi/func.h"

#include "main.h"
#include "checklist_rewards.h"

static const int reward_counts[GMMODE_NUM] = {
    [GMMODE_AIRRIDE]   = REWARD_COUNT_AIRRIDE,
    [GMMODE_TOPRIDE]   = REWARD_COUNT_TOPRIDE,
    [GMMODE_CITYTRIAL] = REWARD_COUNT_CITYTRIAL,
};

// Mod-allocated copies of the per-mode reward tables (originals are in ROM).
static RewardEntry *new_tables[GMMODE_NUM];

// Per-mode pointers into save data for the shuffled location arrays (u16).
static u16 *save_arrays[GMMODE_NUM];

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
    save_data->received_checklist_rewards[mode] |= (1ULL << reward_index);

    if (save_data->has_local_location[mode] & (1ULL << reward_index))
    {
        int target_mode;
        int cross_ck = FindCrossModePlacement(mode, reward_index, &target_mode);

        if (cross_ck >= 0)
        {
            // Cross-mode: set has_reward on the target mode's checkbox.
            GameClearData *cd = gmGetClearcheckerTypeP(target_mode);
            cd->clear[cross_ck].has_reward = 1;
            cd->clear[cross_ck].is_unlocked = 1;
        }
        else
        {
            // Same-mode: clear_kind is in this mode's reward table.
            u8 kind = stc_reward_table_ptrs[mode][reward_index].clear_kind;
            GameClearData *cd = gmGetClearcheckerTypeP(mode);
            cd->clear[kind].has_reward = 1;
            cd->clear[kind].is_unlocked = 1;
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

// ===========================================================================
// UI Hooks: Multi-SIS loading and cross-mode text display
// ===========================================================================

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
// reverse lookup. Vanilla searches the current mode's reward table for a matching
// clear_kind; we check cross_mode_slots first for cross-mode rewards.
// Clobbered instruction: li r25, 0  (init loop counter before reward search loop).
// Returns 0 = not cross-mode (vanilla loop runs),
//         positive = source_reward_index + 1 (cross-mode, unlocked),
//         negative = -1 (cross-mode, locked).
static int ChecklistRewards_FindCrossModeReward(u8 current_mode, u8 clear_kind)
{
    CrossModeSlot *slot = &cross_mode_slots[current_mode][clear_kind];
    if (slot->source_mode == 0xFF)
    {
        current_hover_source_mode = current_mode;
        return 0; // Not cross-mode — fall through to vanilla loop
    }

    // Cross-mode reward found. Check if the source reward has been received
    // (AP bitfield) or if the target checkbox is unlocked — mirrors the same
    // logic that vanilla ClearChecker_CheckUnlocked uses for same-mode rewards.
    // We cannot rely on has_reward alone because the post-loop hook that sets it
    // (ApplyCrossModeHasReward) may not have run yet on the current frame.
    current_hover_source_mode = slot->source_mode;
    if (save_data->received_checklist_rewards[slot->source_mode] & (1ULL << slot->source_reward_index))
        return (int)slot->source_reward_index + 1;
    GameClearData *cd = gmGetClearcheckerTypeP(current_mode);
    if (cd->clear[clear_kind].is_unlocked || cd->clear[clear_kind].has_reward)
        return (int)slot->source_reward_index + 1;
    return -1; // Cross-mode but checkbox not yet unlocked
}

// Hook at 0x80181ee4: before the reward reverse lookup loop.
// Clobbered instruction: li r25, 0
// Args: r3 = mode (from r30+0x14), r4 = clear_kind (r26)
// If cross-mode found and unlocked: set r27 = source_reward_index, skip to 0x80181f5c
// If cross-mode found but locked: set r27 = -1 (no reward visible), skip to 0x80181f5c
// If not cross-mode: execute clobbered insn, continue to vanilla loop
CODEPATCH_HOOKCONDITIONALCREATE(
    0x80181ee4,
    "lbz 3, 0x14(30)\n\t"
    "mr 4, 26\n\t",
    ChecklistRewards_FindCrossModeReward,
    // On non-zero return (cross-mode found): r3 has the result.
    // If > 0, r3 = source_reward_index + 1 (unlocked), set r0 = r3 - 1.
    // If < 0, set r0 = -1 (not unlocked).
    // r0 is used because 0x80181f60 does `mr r27, r0` after our alt exit.
    "cmpwi 3, 0\n\t"
    "blt 0f\n\t"
    "addi 0, 3, -1\n\t"  // r0 = source_reward_index (undo the +1)
    "b 1f\n\t"
    "0:\n\t"
    "li 0, -1\n\t"       // r0 = -1 (no visible reward)
    "1:\n\t",
    0,                // normal exit: clobbered insn + continue at 0x80181ee8
    0x80181f5c        // alt exit: skip past vanilla loop + unlock check
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

// Post-reward-loop hook: set has_reward on cross-mode reward checkboxes.
// Checklist_SetRewardFlagOnUnlocks only iterates the current mode's reward table,
// so cross-mode rewards placed in this mode are missed. We iterate cross_mode_slots
// for the current mode and set has_reward on any checkbox that is unlocked/filled
// and has a cross-mode reward assigned (same logic as the vanilla loop for same-mode).
static void ChecklistRewards_ApplyCrossModeHasReward(u8 current_mode)
{
    GameClearData *cd = gmGetClearcheckerTypeP(current_mode);
    for (int ck = 0; ck < 120; ck++)
    {
        CrossModeSlot *slot = &cross_mode_slots[current_mode][ck];
        if (slot->source_mode == 0xFF)
            continue;
        // Mirror vanilla has_reward logic: set when the checkbox is unlocked or filled
        if (cd->clear[ck].is_unlocked || cd->clear[ck].is_filler)
            cd->clear[ck].has_reward = 1;
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

// ===========================================================================
// Reward table management
// ===========================================================================

static void InitSaveArrayPtrs(void)
{
    save_arrays[GMMODE_AIRRIDE]   = save_data->shuffled_airride_rewards;
    save_arrays[GMMODE_TOPRIDE]   = save_data->shuffled_topride_rewards;
    save_arrays[GMMODE_CITYTRIAL] = save_data->shuffled_citytrial_rewards;
}

// Set all clear_kind values to 0 across every mode's reward table.
// clear_kind=0 is a safe sentinel; hooks prevent all issues.
static void ClearAllRewardTables(void)
{
    for (int mode = GMMODE_AIRRIDE; mode < GMMODE_NUM; mode++)
        for (int i = 0; i < reward_counts[mode]; i++)
            new_tables[mode][i].clear_kind = 0;
    OSReport("Cleared all reward clear_kinds (no rewards mode)\n");
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
    InitSaveArrayPtrs();
    if (save_data->rewards_shuffled)
    {
        ChecklistRewards_ClearCrossModeSlots();
        for (int source_mode = GMMODE_AIRRIDE; source_mode < GMMODE_NUM; source_mode++)
        {
            for (int i = 0; i < reward_counts[source_mode]; i++)
            {
                u16 loc = save_arrays[source_mode][i];
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
        OSReport("Loaded reward tables from save (with cross-mode support)\n");
    }
    else
    {
        InitSaveArrayPtrs();
        ClearAllRewardTables();
        save_data->rewards_shuffled = 1;
        OSReport("Reward tables cleared and marked initialized\n");
    }
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
            clear_data->clear[i].is_unlocked = 1;
        }
    }
    OSReport("All checklist squares revealed (%d modes x 120 squares)\n", GMMODE_NUM);
}

// ===========================================================================
// Lifecycle entry points
// ===========================================================================

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

// First boot: clear tables and mark initialized. Call from OnSaveInit.
void ChecklistRewards_OnSaveInit(void)
{
    InitSaveArrayPtrs();
    ClearAllRewardTables();
    save_data->rewards_shuffled = 1;
    OSReport("Reward tables cleared and marked initialized\n");
}

// Restore reward tables and received rewards from save data.
// Call from OnSaveLoaded (handles both first boot and subsequent boots).
void ChecklistRewards_OnSaveLoaded(void)
{
    RestoreRewardTablesFromSave();

    // Re-grant all received rewards so their checklist slots are correctly marked.
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
// Handles both same-mode and cross-mode reward placements via u16 encoding:
//   (target_mode << 8) | clear_kind  — local reward at target_mode's checklist
//   0xFFFF                           — remote reward (no local slot)
// Updates RewardEntry.clear_kind for same-mode rewards, populates cross_mode_slots
// for cross-mode rewards, persists to save data, and re-applies grants.
void ChecklistRewards_ApplyLocations()
{
    const u16 *location_arrays[GMMODE_NUM] = {
        archipelago_data->location_airride,
        archipelago_data->location_topride,
        archipelago_data->location_citytrial,
    };
    u16 *save_shuffled[GMMODE_NUM] = {
        save_data->shuffled_airride_rewards,
        save_data->shuffled_topride_rewards,
        save_data->shuffled_citytrial_rewards,
    };

    // Reset before repopulating from the new assignment.
    for (int mode = 0; mode < GMMODE_NUM; mode++)
        save_data->has_local_location[mode] = 0;
    ChecklistRewards_ClearCrossModeSlots();

    for (int source_mode = 0; source_mode < GMMODE_NUM; source_mode++)
    {
        int count = reward_counts[source_mode];
        for (int i = 0; i < count; i++)
        {
            u16 loc = location_arrays[source_mode][i];
            save_shuffled[source_mode][i] = loc;

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
            save_data->has_local_location[source_mode] |= (1ULL << i);

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

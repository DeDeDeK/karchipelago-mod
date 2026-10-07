#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "os.h"
#include "game.h"
#include "stadium.h"
#include "inline.h"
#include "hoshi/func.h"

#include "main.h"
#include "ap_options.h"
#include "ap_unlock.h"
#include "ap_goal.h"
#include "ap_checks.h"
#include "ap_checklist.h"
#include "ap_check_detect.h"
#include "ap_patches.h"
#include "checklist_rewards.h"
#include "gate_machines.h"
#include "gate_topride_items.h"
#include "patch_cap.h"
#include "settings_menu.h"

static const u16 gating_flags[AP_UNLOCK_NUM] = {
    [AP_UNLOCK_MACHINE]       = offsetof(APSlotOptions, machine_gating_enabled),
    [AP_UNLOCK_ABILITY]       = offsetof(APSlotOptions, ability_gating_enabled),
    [AP_UNLOCK_EVENT]         = offsetof(APSlotOptions, event_gating_enabled),
    [AP_UNLOCK_PATCH]         = offsetof(APSlotOptions, patch_gating_enabled),
    [AP_UNLOCK_ITEM]          = offsetof(APSlotOptions, item_gating_enabled),
    [AP_UNLOCK_BOX]           = offsetof(APSlotOptions, box_gating_enabled),
    [AP_UNLOCK_AIRRIDE_STAGE] = offsetof(APSlotOptions, airride_stage_gating_enabled),
    [AP_UNLOCK_TOPRIDE_STAGE] = offsetof(APSlotOptions, topride_stage_gating_enabled),
    [AP_UNLOCK_TOPRIDE_ITEM]  = offsetof(APSlotOptions, topride_item_gating_enabled),
    [AP_UNLOCK_COLOR]         = offsetof(APSlotOptions, color_gating_enabled),
    [AP_UNLOCK_STADIUM]       = offsetof(APSlotOptions, stadium_gating_enabled),
    [AP_UNLOCK_BASE_ABILITY]  = offsetof(APSlotOptions, base_ability_gating_enabled),
    [AP_UNLOCK_AP_STAR_PIECE] = offsetof(APSlotOptions, item_gating_enabled),
};

static u32 *GatingFlag(APUnlockCategory cat)
{
    return (u32 *)((u8 *)&ap_save->options + gating_flags[cat]);
}

int APOptions_GetGating(APUnlockCategory cat)
{
    if ((unsigned)cat >= AP_UNLOCK_NUM)
        return 1;
    return *GatingFlag(cat) ? 1 : 0;
}

void APOptions_DebugSetGating(APUnlockCategory cat, int enabled)
{
    if ((unsigned)cat < AP_UNLOCK_NUM)
        *GatingFlag(cat) = enabled ? 1u : 0u;
}

static void AppendCsv(char *buf, int *pos, const char *name)
{
    *pos += sprintf(&buf[*pos], "%s%s", *pos ? ", " : "", name);
}

// buf receives the count goal's threshold, so each goal named in one line needs its own.
static const char *GoalName(const APSlotOptions *opts, int row, char *buf)
{
    static const char *const names[] = {
        [GOAL_100_CHECKLIST]      = "100 squares",
        [GOAL_N_CHECKLIST]        = "N squares",
        [GOAL_CHECKLIST_LIST]     = "listed squares",
        [GOAL_HYDRA_AND_DRAGOON]  = "Hydra + Dragoon",
        [GOAL_BEAT_KING_DEDEDE]   = "beat King Dedede",
        [GOAL_MAX_STATS_CT]       = "max stats",
        [GOAL_ASSEMBLE_AP_STAR]   = "assemble AP Star",
        [GOAL_ALL_LEGENDARIES_CT] = "all legendaries",
        [GOAL_NONE]               = "none",
    };
    u32 goal = opts->goal[row];

    if (goal >= GetElementsIn(names))
        return "?";
    if (goal == GOAL_N_CHECKLIST)
    {
        sprintf(buf, "%d squares", opts->checklist_amount[row]);
        return buf;
    }
    return names[goal];
}

void APOptions_ApplyRevealChecklists(void)
{
    for (int row = 0; row < CHECKLIST_MODE_NUM; row++)
        if (ap_save->options.reveal_checklists[row])
            ChecklistRewards_Reveal(row);
}

// Bits the goal keeps locked even with their category ungated: the AP world ships these
// unlocks because the goal's one feat would otherwise be free at connect.
static u32 GoalForcedBits(APUnlockCategory cat, u32 forced)
{
    switch (cat)
    {
    case AP_UNLOCK_ITEM:          return (forced & GOALGATE_LEGENDARY_PIECES) ? LEGENDARY_PIECE_ITEM_BITS : 0;
    case AP_UNLOCK_STADIUM:       return (forced & GOALGATE_VS_KING_DEDEDE) ? (1u << STKIND_VSKINGDEDEDE) : 0;
    case AP_UNLOCK_AP_STAR_PIECE: return (forced & GOALGATE_AP_STAR_PIECES) ? ~0u : 0;
    default:                      return 0;
    }
}

// The AP world ships no unlock items for an ungated category, so its mask is pre-filled.
// This bypasses the per-unlock textbox path, so connecting does not flood the screen.
static void APOptions_ApplyUngatedCategories(void)
{
    const APSlotOptions *opts = &ap_save->options;
    char list[224];
    int n = 0;

    for (int cat = 0; cat < AP_UNLOCK_NUM; cat++)
    {
        if (APOptions_GetGating(cat))
            continue;
        u32 full = (1u << APUnlock_Bits(cat)) - 1;
        APUnlock_SetMask(cat, full & ~GoalForcedBits(cat, opts->goal_forced_gates));
        AppendCsv(list, &n, APUnlock_Name(cat));
    }
    OSReport("[APOptions] Gating on, except: %s\n", n ? list : "nothing");

    // The AP world ships the Free Star / Steer Star unlocks only when Top Ride is in the
    // seed, but the one machine mask stays gated for City Trial and Air Ride.
    if (opts->machine_gating_enabled && opts->goal[GMMODE_TOPRIDE] == GOAL_NONE)
    {
        APUnlock_SetMask(AP_UNLOCK_MACHINE, APUnlock_GetMask(AP_UNLOCK_MACHINE) | TR_MACHINE_BITS);
        OSReport("[APOptions] Top Ride not in the seed, its machines unlocked\n");
    }

    if (!opts->topride_item_gating_enabled)
        GateTopRideItems_MarkNewItemRewardsReceived();

    ChecklistRewards_GrantUnplaced(opts->checklist_reward_placed_types);

    n = 0;
    if (opts->goal_forced_gates & GOALGATE_LEGENDARY_PIECES) AppendCsv(list, &n, "legendary pieces");
    if (opts->goal_forced_gates & GOALGATE_VS_KING_DEDEDE)   AppendCsv(list, &n, "Vs. King Dedede");
    if (opts->goal_forced_gates & GOALGATE_AP_STAR_PIECES)   AppendCsv(list, &n, "AP Star spheres");
    if (n)
        OSReport("[APOptions] Gating forced by the goal: %s\n", list);
}

static void APOptions_ReportTransfer(const APSlotOptions *opts)
{
    char list[224];
    int n = 0;
    if (opts->death_link_enabled)  AppendCsv(list, &n, "DeathLink");
    if (opts->energy_link_enabled) AppendCsv(list, &n, "EnergyLink");
    if (opts->trap_link_enabled)   AppendCsv(list, &n, "TrapLink");
    OSReport("[APOptions] Links on: %s\n", n ? list : "none");

    char goals[CHECKLIST_MODE_NUM][24];
    OSReport("[APOptions] Goals - AirRide: %s, TopRide: %s, CityTrial: %s, %s: %s\n",
             GoalName(opts, GMMODE_AIRRIDE, goals[GMMODE_AIRRIDE]),
             GoalName(opts, GMMODE_TOPRIDE, goals[GMMODE_TOPRIDE]),
             GoalName(opts, GMMODE_CITYTRIAL, goals[GMMODE_CITYTRIAL]),
             AP_CHECKLIST_NAME, GoalName(opts, AP_CHECKLIST_ROW, goals[AP_CHECKLIST_ROW]));

    n = 0;
    for (int r = 0; r < CHECKLIST_MODE_NUM; r++)
        if (opts->reveal_checklists[r])
            AppendCsv(list, &n, APChecklist_RowName(r));
    OSReport("[APOptions] CT patch cap %d-%d, checklists pre-revealed: %s\n",
             opts->city_trial_patch_cap_min, opts->city_trial_patch_cap_max,
             n ? list : "none");
}

// Options are fixed per slot, so the copy runs once per save. Every client write is still
// acknowledged by clearing options_valid after the menu mirrors are republished: the
// client reads the clear as the mirrors being the player's choices. No card write - the
// client resends the options on every connect.
void APOptions_OnFrameStart(void)
{
    if (!ap_data->options_valid)
        return;

    int first = !ap_save->options_received;
    if (first)
    {
        memcpy(&ap_save->options, &ap_data->options, sizeof(APSlotOptions));
        ap_save->options_received = 1;
        SettingsMenu_SeedFromSlotOptions(&ap_save->options);
    }

    SyncMenuStateToAPData();
    ap_data->options_valid = 0;

    if (!first)
        return;

    OSReport("[APOptions] Slot options received\n");
    APOptions_ReportTransfer(&ap_save->options);
    APOptions_ApplyRevealChecklists();
    APOptions_ApplyUngatedCategories();
    APGoal_Evaluate();
}

void APOptions_GetPatchCapRange(int *out_min, int *out_max)
{
    if (out_min)
        *out_min = (int)ap_save->options.city_trial_patch_cap_min;
    if (out_max)
        *out_max = (int)ap_save->options.city_trial_patch_cap_max;
}

int APOptions_GetSpawnRateMin(void)
{
    return (int)ap_save->options.spawn_rate_min;
}

// One bound each: the menu offers fixed steps and shows the nearest at or below the live
// value, so writing both from either row would round the untouched one down.
void APOptions_DebugSetPatchCapMin(int min)
{
    ap_save->options.city_trial_patch_cap_min = (u32)min;
}

void APOptions_DebugSetPatchCapMax(int max)
{
    ap_save->options.city_trial_patch_cap_max = (u32)max;
}

void APOptions_DebugSetSpawnRateMin(int percent)
{
    ap_save->options.spawn_rate_min = (u32)percent;
}

// The pre-fill only sets bits, so every mask is cleared first. received_checklist_rewards
// is left alone: it also holds rewards genuinely received.
void APOptions_DebugReapply(void)
{
    if (!ap_save->options_received)
        OSReport("[APOptions] No slot options received, every category re-applies as ungated\n");

    for (int cat = 0; cat < AP_UNLOCK_NUM; cat++)
        APUnlock_SetMask((APUnlockCategory)cat, 0);

    APOptions_ApplyRevealChecklists();
    APOptions_ApplyUngatedCategories();

    Hoshi_WriteSave();
}

void APOptions_DebugReportState(void)
{
    OSReport("[APOptions] %d items received, %d queued, options received %d\n",
             ap_save->item_received_count, ap_save->unprocessed_count, ap_save->options_received);

    for (int cat = 0; cat < AP_UNLOCK_NUM; cat++)
        OSReport("[APOptions] %s gated %d, mask %s\n", APUnlock_Name(cat), APOptions_GetGating(cat),
                 MaskBits(APUnlock_GetMask(cat), APUnlock_Bits(cat)));

    OSReport("[APOptions] Patch cap %d (%d received, seed range %d-%d), spawn rate floor %d%%\n",
             PatchCap_GetCap(), ap_save->patch_cap_count,
             ap_save->options.city_trial_patch_cap_min,
             ap_save->options.city_trial_patch_cap_max,
             ap_save->options.spawn_rate_min);

    char perm[64];
    int n = 0;
    for (int i = 0; i < PATCHKIND_NUM; i++)
        n += sprintf(&perm[n], "%s%d", i ? " " : "", ap_save->permanent_patches[i]);
    OSReport("[APOptions] Permanent patches (PatchKind order): %s\n", perm);

    char goal_buf[24];
    for (int r = 0; r < CHECKLIST_MODE_NUM; r++)
        OSReport("[APOptions] %s: goal %s, %d squares done\n", APChecklist_RowName(r),
                 GoalName(&ap_save->options, r, goal_buf), APChecks_PopcountRow(r));

    OSReport("[APOptions] goal_complete %d, met %s, max stats CT %d\n",
             ap_data->goal_complete, MaskBits(ap_save->goal_latched, CHECKLIST_MODE_NUM),
             ap_save->max_stats_ct_achieved);

    OSReport("[APOptions] AP Patches %d collected of %d, %d left\n",
             APPatches_CollectedCount(), APPatches_GetCount(), APPatches_Remaining());

    OSReport("[APOptions] Energy %lld MJ, %u deposited, %u withdrawn this boot\n",
             ap_data->energy_balance, ap_data->energy_deposit_total,
             ap_data->energy_withdraw_total);

    APCheckDetect_ReportProgress();
}

// Permanent patches already on a rider stay for the round, so this shows from the next
// round load.
void APOptions_DebugResetProgression(void)
{
    ap_save->patch_cap_count = 0;
    ap_save->spawn_rate_level = 0;
    memset(ap_save->permanent_patches, 0, sizeof(ap_save->permanent_patches));

    memset(&ap_save->checks, 0, sizeof(ap_save->checks));
    ap_save->max_stats_ct_achieved = 0;

    ap_save->item_received_count = 0;
    ap_save->unprocessed_count = 0;
    ap_data->item_received_index = 0;

    Hoshi_WriteSave();
    OSReport("[APOptions] Progression reset: patch cap, spawn rate, permanent patches, "
             "check progress, max stats and the item queue\n");
}

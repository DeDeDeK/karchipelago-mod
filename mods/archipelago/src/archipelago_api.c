#include "os.h"
#include "game.h"
#include "hsd.h"
#include "hoshi/mod.h"

#include "archipelago_api.h"
#include "main.h"
#include "ap_unlock.h"
#include "ap_options.h"
#include "ap_item_handler.h"
#include "checklist_rewards.h"
#include "ap_checks.h"
#include "ap_goal.h"
#include "energylink.h"
#include "gate_ap_star.h"
#include "ap_patches.h"
#include "ap_text.h"
#include "ap_check_detect.h"

static int ApiQueueItem(int ap_item_id)
{
    return APItems_Queue((uint)ap_item_id);
}

static void ApiGrantReward(GameMode mode, u8 reward_index)
{
    ChecklistRewards_Grant(mode, reward_index, /*announce=*/1);
}

static void ApiDebugTriggerDeathlinkReceive(void)
{
    ap_data->deathlink_receive = 1;
}

static void ApiDebugTriggerTraplinkReceive(void)
{
    ap_data->traplink_receive = 1;
}

static const ArchipelagoAPI api = {
    .GetUnlockMask                = APUnlock_GetMask,
    .SetUnlockMask                = APUnlock_SetMask,
    .QueueItem                    = ApiQueueItem,
    .GrantReward                  = ApiGrantReward,
    .GetHoveredCell               = ChecklistRewards_GetHoveredCell,
    .ResolveCell                  = ChecklistRewards_ResolveCell,
    .GetRewardCount               = ChecklistRewards_GetRewardCount,
    .GetShuffledReward            = ChecklistRewards_GetShuffledReward,
    .DebugRevealAllChecklists     = ChecklistRewards_RevealAll,
    .DebugSimulateLocationData    = ChecklistRewards_DebugSimulateLocationData,
    .DebugClearAllChecklistData   = ChecklistRewards_DebugClearAll,
    .DebugClearAllSentChecks      = APChecks_DebugClearAll,
    .DebugForceMarkAllChecks      = APChecks_DebugForceMarkAll,
    .DebugTriggerGoalComplete     = APGoal_DebugComplete,
    .DebugTriggerDeathlinkReceive = ApiDebugTriggerDeathlinkReceive,
    .DebugRevealChecklist         = ChecklistRewards_Reveal,
    .DebugSpawnApStarPiece        = GateApStar_SpawnPiece,
    .DebugSpawnApBox              = APPatches_DebugSpawnBox,
    .DebugCollectApPatch          = APPatches_DebugClaim,
    .GetApPatchCount              = APPatches_GetCount,
    .DebugSetApPatchCount         = APPatches_DebugSetCount,
    .DebugClearApPatchCollected   = APPatches_DebugClearCollected,
    .DebugTriggerTraplinkReceive  = ApiDebugTriggerTraplinkReceive,
    .DebugSendText                = APText_DebugSend,
    .DebugSendOverlongText        = APText_DebugSendOverlong,
    .GetGoal                      = APGoal_Get,
    .DebugSetGoals                = APGoal_DebugSetGoals,
    .GetGating                    = APOptions_GetGating,
    .DebugSetGating               = APOptions_DebugSetGating,
    .GetPatchCapRange             = APOptions_GetPatchCapRange,
    .GetSpawnRateMin              = APOptions_GetSpawnRateMin,
    .DebugSetPatchCapMin          = APOptions_DebugSetPatchCapMin,
    .DebugSetPatchCapMax          = APOptions_DebugSetPatchCapMax,
    .DebugSetSpawnRateMin         = APOptions_DebugSetSpawnRateMin,
    .DebugReapplySlotOptions      = APOptions_DebugReapply,
    .GetCheckProgress             = APCheckDetect_GetProgress,
    .DebugSetCheckProgress        = APCheckDetect_DebugSetProgress,
    .GetEnergyBalance             = EnergyLink_GetBalance,
    .DebugSetEnergyBalance        = EnergyLink_DebugSetBalance,
    .DebugReportState             = APOptions_DebugReportState,
    .DebugResetProgression        = APOptions_DebugResetProgression,
};

void ArchipelagoAPI_Export(void)
{
    Hoshi_ExportMod((void *)&api);
}

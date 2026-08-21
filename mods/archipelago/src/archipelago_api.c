#include "os.h"
#include "game.h"
#include "hsd.h"
#include "hoshi/mod.h"

#include "archipelago_api.h"
#include "main.h"
#include "ap_item_handler.h"
#include "checklist_rewards.h"
#include "check_detection.h"
#include "energylink.h"
#include "gate_ap_star.h"
#include "textbox_api.h"

u32 Unlock_GetMask(APUnlockCategory cat)
{
    switch (cat)
    {
        case AP_UNLOCK_MACHINE:        return ap_save->machine_unlocked_mask;
        case AP_UNLOCK_ABILITY:        return ap_save->ability_unlocked_mask;
        case AP_UNLOCK_EVENT:          return ap_save->event_unlocked_mask;
        case AP_UNLOCK_PATCH:          return ap_save->patch_unlocked_mask;
        case AP_UNLOCK_ITEM:           return ap_save->item_unlocked_mask;
        case AP_UNLOCK_BOX:            return ap_save->box_unlocked_mask;
        case AP_UNLOCK_AIRRIDE_STAGE:  return ap_save->airride_stage_unlocked_mask;
        case AP_UNLOCK_TOPRIDE_STAGE:  return ap_save->topride_stage_unlocked_mask;
        case AP_UNLOCK_TOPRIDE_ITEM:   return ap_save->topride_item_unlocked_mask;
        case AP_UNLOCK_COLOR:          return ap_save->color_unlocked_mask;
        case AP_UNLOCK_STADIUM:        return ap_save->stadium_unlocked_mask;
        case AP_UNLOCK_BASE_ABILITY:   return ap_save->base_ability_unlocked_mask;
        case AP_UNLOCK_AP_STAR_PIECE:  return ap_save->ap_star_piece_unlocked_mask;
        default:                       return 0;
    }
}

void Unlock_SetMask(APUnlockCategory cat, u32 mask)
{
    switch (cat)
    {
        case AP_UNLOCK_MACHINE:        ap_save->machine_unlocked_mask        = (u32)mask; break;
        case AP_UNLOCK_ABILITY:        ap_save->ability_unlocked_mask        = (u16)mask; break;
        case AP_UNLOCK_EVENT:          ap_save->event_unlocked_mask          = (u32)mask; break;
        case AP_UNLOCK_PATCH:          ap_save->patch_unlocked_mask          = (u16)mask; break;
        case AP_UNLOCK_ITEM:           ap_save->item_unlocked_mask           = (u32)mask; break;
        case AP_UNLOCK_BOX:            ap_save->box_unlocked_mask            = (u8)mask;  break;
        case AP_UNLOCK_AIRRIDE_STAGE:  ap_save->airride_stage_unlocked_mask  = (u16)mask; break;
        case AP_UNLOCK_TOPRIDE_STAGE:  ap_save->topride_stage_unlocked_mask  = (u16)mask; break;
        case AP_UNLOCK_TOPRIDE_ITEM:   ap_save->topride_item_unlocked_mask   = (u32)mask; break;
        case AP_UNLOCK_COLOR:          ap_save->color_unlocked_mask          = (u8)mask;  break;
        case AP_UNLOCK_STADIUM:        ap_save->stadium_unlocked_mask        = (u32)mask; break;
        case AP_UNLOCK_BASE_ABILITY:   ap_save->base_ability_unlocked_mask   = (u8)mask;  break;
        case AP_UNLOCK_AP_STAR_PIECE:  ap_save->ap_star_piece_unlocked_mask  = (u8)mask;  break;
        default: break;
    }

    // ap_star reads its sphere gate at 3D load start, and it runs before this mod
    // does, so the mask is pushed on every write rather than read back later.
    if (cat == AP_UNLOCK_AP_STAR_PIECE)
        GateApStar_PushMask();
}

static int ApiQueueItem(int ap_item_id)
{
    return APItems_Queue((uint)ap_item_id);
}

static void ApiAddEnergy(float amount)
{
    EnergyLink_Deposit(amount);
}

static void ApiGrantReward(GameMode mode, u8 reward_index)
{
    ChecklistRewards_Grant(mode, reward_index, /*announce=*/1);
}

static int ApiGetHoveredCell(u8 *out_mode, u8 *out_clear_kind)
{
    return ChecklistRewards_GetHoveredCell(out_mode, out_clear_kind);
}

static int ApiResolveCell(u8 mode, u8 clear_kind, u8 *out_src_mode, u8 *out_src_ri)
{
    return ChecklistRewards_ResolveCell(mode, clear_kind, out_src_mode, out_src_ri);
}

static int ApiGetRewardCount(GameMode mode)
{
    return ChecklistRewards_GetRewardCount(mode);
}

static u16 ApiGetShuffledReward(GameMode mode, u8 reward_index)
{
    return ChecklistRewards_GetShuffledReward(mode, reward_index);
}

static void ApiTextbox(const char *msg)
{
    tb_api->Enqueue("%s", msg);
}

static void ApiDebugRevealAllChecklists(void)
{
    RevealAllChecklists();
}

static void ApiDebugRevealChecklist(int mode)
{
    RevealChecklist(mode);
}

static void ApiDebugSimulateLocationData(void)
{
    ChecklistRewards_DebugSimulateLocationData();
}

static void ApiDebugClearAllChecklistData(void)
{
    ChecklistRewards_DebugClearAll();
}

static void ApiDebugClearAllSentChecks(void)
{
    CheckDetection_DebugClearAll();
}

static void ApiDebugForceMarkAllChecks(void)
{
    CheckDetection_DebugForceMarkAll();
}

static void ApiDebugTriggerGoalComplete(void)
{
    CheckDetection_DebugTriggerGoal();
}

static void ApiDebugWriteIncomingItem(int ap_item_id)
{
    ap_data->incoming_item_id = (uint)ap_item_id;
}

static void ApiDebugTriggerDeathlinkReceive(void)
{
    ap_data->deathlink_receive = 1;
}

static void ApiDebugTriggerTraplinkReceive(void)
{
    ap_data->traplink_receive = 1;
}

static int ApiDebugSpawnApStarPiece(int piece, int ply)
{
    return GateApStar_SpawnPiece(piece, ply);
}

static const ArchipelagoAPI api = {
    .GetUnlockMask                = Unlock_GetMask,
    .SetUnlockMask                = Unlock_SetMask,
    .QueueItem                    = ApiQueueItem,
    .AddEnergy                    = ApiAddEnergy,
    .GrantReward                  = ApiGrantReward,
    .GetHoveredCell               = ApiGetHoveredCell,
    .ResolveCell                  = ApiResolveCell,
    .GetRewardCount               = ApiGetRewardCount,
    .GetShuffledReward            = ApiGetShuffledReward,
    .Textbox                      = ApiTextbox,
    .DebugRevealAllChecklists     = ApiDebugRevealAllChecklists,
    .DebugSimulateLocationData    = ApiDebugSimulateLocationData,
    .DebugClearAllChecklistData   = ApiDebugClearAllChecklistData,
    .DebugClearAllSentChecks      = ApiDebugClearAllSentChecks,
    .DebugForceMarkAllChecks      = ApiDebugForceMarkAllChecks,
    .DebugTriggerGoalComplete     = ApiDebugTriggerGoalComplete,
    .DebugWriteIncomingItem       = ApiDebugWriteIncomingItem,
    .DebugTriggerDeathlinkReceive = ApiDebugTriggerDeathlinkReceive,
    .DebugTriggerTraplinkReceive  = ApiDebugTriggerTraplinkReceive,
    .DebugRevealChecklist         = ApiDebugRevealChecklist,
    .DebugSpawnApStarPiece        = ApiDebugSpawnApStarPiece,
};

void ArchipelagoAPI_Export(void)
{
    Hoshi_ExportMod((void *)&api);
}

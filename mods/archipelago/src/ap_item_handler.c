#include "game.h"
#include "inline.h"
#include "stadium.h"
#include "hoshi/func.h"

#include "ap_item_handler.h"
#include "checklist_rewards.h"
#include "ap_checklist.h"
#include "kirby_scale.h"
#include "textbox_api.h"
#include "ap_colors.h"
#include "city_trial_event.h"
#include "ability_item.h"
#include "patch_item.h"
#include "permanent_patch.h"
#include "patch_cap.h"
#include "gate_events.h"
#include "gate_abilities.h"
#include "gate_base_abilities.h"
#include "gate_boxes.h"
#include "gate_patches.h"
#include "gate_items.h"
#include "gate_machines.h"
#include "gate_airride_stages.h"
#include "gate_topride_stages.h"
#include "gate_topride_items.h"
#include "gate_colors.h"
#include "gate_stadiums.h"
#include "spawn_rate.h"
#include "gate_ap_star.h"
#include "main.h"
#include "settings_menu.h"
#include "ap_announce.h"

static void APItems_CheckMailbox(void)
{
    static int warned_full = 0;

    uint incoming = ap_data->incoming_item_id;
    if (incoming == 0)
        return;

    if (ap_save->unprocessed_count >= MAX_RECEIVED_ITEMS)
    {
        // Held in the mailbox: the client waits for incoming_item_id == 0, so this is the
        // protocol's backpressure.
        if (!warned_full)
        {
            OSReport("[APItems] Unprocessed queue full (%d), item %d held in the mailbox\n",
                     MAX_RECEIVED_ITEMS, incoming);
            warned_full = 1;
        }
        return;
    }
    warned_full = 0;

    uint idx = ap_save->item_received_count;
    ap_save->item_received_count++;

    APItems_Queue(incoming);

    ap_data->item_received_index = ap_save->item_received_count;

    OSReport("[APItems] AP item ID %d received (index %d)\n", incoming, idx);

    ap_data->incoming_item_id = 0;
}

// Textbox color for a directly-received ITKIND item, by category.
static GXColor ItemReceiveColor(ItemKind k)
{
    switch (k)
    {
        case ITKIND_ACCELDOWN:   case ITKIND_TOPSPEEDDOWN: case ITKIND_OFFENSEDOWN:
        case ITKIND_DEFENSEDOWN: case ITKIND_TURNDOWN:     case ITKIND_GLIDEDOWN:
        case ITKIND_CHARGEDOWN:  case ITKIND_WEIGHTDOWN:
        case ITKIND_SPEEDMIN:    case ITKIND_CHARGENONE:
        case ITKIND_ACCELFAKE:   case ITKIND_TOPSPEEDFAKE: case ITKIND_OFFENSEFAKE:
        case ITKIND_DEFENSEFAKE: case ITKIND_TURNFAKE:     case ITKIND_GLIDEFAKE:
        case ITKIND_CHARGEFAKE:  case ITKIND_WEIGHTFAKE:
            return APColor_Trap;
        case ITKIND_WEIGHT:                            return tb_api->PatchColors[PATCHKIND_WEIGHT];
        case ITKIND_ACCEL:                             return tb_api->PatchColors[PATCHKIND_ACCEL];
        case ITKIND_TOPSPEED:   case ITKIND_SPEEDMAX:  return tb_api->PatchColors[PATCHKIND_TOPSPEED];
        case ITKIND_TURN:                              return tb_api->PatchColors[PATCHKIND_TURN];
        case ITKIND_CHARGE:     case ITKIND_CHARGEMAX: return tb_api->PatchColors[PATCHKIND_CHARGE];
        case ITKIND_GLIDE:                             return tb_api->PatchColors[PATCHKIND_GLIDE];
        case ITKIND_OFFENSE:    case ITKIND_OFFENSEMAX:return tb_api->PatchColors[PATCHKIND_OFFENSE];
        case ITKIND_DEFENSE:    case ITKIND_DEFENSEMAX:return tb_api->PatchColors[PATCHKIND_DEFENSE];
        case ITKIND_HP:                                return tb_api->PatchColors[PATCHKIND_HP];
        case ITKIND_ALLUP:                             return tb_api->PatchColors[PATCHKIND_CHARGE];
        case ITKIND_HYDRA1:   case ITKIND_HYDRA2:   case ITKIND_HYDRA3:
        case ITKIND_DRAGOON1: case ITKIND_DRAGOON2: case ITKIND_DRAGOON3:
            return tb_api->MachineColor;
        case ITKIND_BOXBLUE:  return tb_api->BoxColors[BOXKIND_BLUE];
        case ITKIND_BOXGREEN: return tb_api->BoxColors[BOXKIND_GREEN];
        case ITKIND_BOXRED:   return tb_api->BoxColors[BOXKIND_RED];
        default:
            return tb_api->ItemColor;
    }
}

static void NotifyItemReceived(ItemKind k)
{
    if ((unsigned)k < ITKIND_NUM && ItemKind_Names[k])
        APAnnounce_Grant("Received: ", ItemKind_Names[k], ItemReceiveColor(k), NULL);
}

// The give handlers return how many players took the item; none means try again later.
static int Applied(int count)
{
    return count ? AP_ITEM_APPLIED : AP_ITEM_RETRY;
}

// In machine forward units.
#define AP_SPAWN_FORWARD 10.0f

int APItems_SpawnForward(int ply, ItemKind kind, int box_kind, int size)
{
    if (ply < 0 || ply >= PLY_NUM)
        return 0;
    GOBJ *mg = Ply_GetMachineGObj(ply);
    if (!mg)
        return 0;
    MachineData *md = mg->userdata;

    Vec3 pos;
    pos.X = md->pos.X + AP_SPAWN_FORWARD * md->forward.X;
    pos.Y = md->pos.Y + AP_SPAWN_FORWARD * md->forward.Y;
    pos.Z = md->pos.Z + AP_SPAWN_FORWARD * md->forward.Z;

    // Mirrors SpawnItemPlayer: the initial raycast from is_airborne=1 settles the item
    // onto the ground.
    ItemDesc desc;
    Item_InitDesc(&desc, kind, 1.0f, 0, &pos, &md->up, &md->forward,
                  box_kind, size, 1, 3, -1, -1);
    return CityItem_Create(&desc) != NULL;
}

// A box carries its color and a size off the stage's table, which is what its break
// rolls the contents from.
static int SpawnBoxHumansForward(ItemKind kind)
{
    int color = kind - ITKIND_BOXBLUE;
    int gave = 0;
    for (int i = 0; i < PLY_NUM; i++)
        if (Ply_GetPKind(i) == PKIND_HMN)
            gave += APItems_SpawnForward(i, kind, color, GateBoxes_RollSize(color));
    return gave;
}

int APItems_HandleItem(uint ap_item_id)
{
    switch (ap_item_id)
    {
        case AP_ITEM_CHECKBOX_FILLER_AIRRIDE:
        case AP_ITEM_CHECKBOX_FILLER_TOPRIDE:
        case AP_ITEM_CHECKBOX_FILLER_CITYTRIAL:
        {
            GameMode mode = ap_item_id - AP_ITEM_CHECKBOX_FILLER_AIRRIDE;
            Checklist_GrantFiller(mode);
            ChecklistRewards_AnnounceFiller(mode);
            return AP_ITEM_APPLIED;
        }
        case AP_ITEM_CHECKBOX_FILLER_ARCHIPELAGO:
            // Without custom_checklist there is no AP tab to hold the filler.
            if (!APChecklist_IsRegistered())
                return AP_ITEM_DROP;
            Checklist_GrantFiller((GameMode)ap_checklist_mode);
            ChecklistRewards_AnnounceFiller((GameMode)ap_checklist_mode);
            return AP_ITEM_APPLIED;
        case AP_ITEM_PATCH_CAP_INCREASE:
            PatchCap_Increment();
            return AP_ITEM_APPLIED;
        case AP_ITEM_SPAWN_RATE_UP:
            SpawnRate_Increment();
            return AP_ITEM_APPLIED;
    }

    // KirbyScale_HandleItem retries until Kirby models exist, Top Ride's included.
    if (ap_item_id == AP_ITEM_BIG_KIRBY || ap_item_id == AP_ITEM_SMALL_KIRBY)
        return KirbyScale_HandleItem(ap_item_id);

    if (ap_item_id >= AP_CHECKLIST_REWARD_BASE &&
        ap_item_id < AP_CHECKLIST_REWARD_BASE + GMMODE_NUM * AP_CHECKLIST_REWARD_STRIDE)
    {
        u32 offset = ap_item_id - AP_CHECKLIST_REWARD_BASE;
        GameMode mode = offset / AP_CHECKLIST_REWARD_STRIDE;
        u8 ap_reward_index = offset % AP_CHECKLIST_REWARD_STRIDE;
        // Each mode uses fewer than its stride, so IDs in the gaps are not rewards.
        if (ap_reward_index >= ChecklistRewards_GetRewardCount(mode))
        {
            OSReport("[APItems] Checklist reward ID %d out of range (mode %d index %d), dropped\n",
                     ap_item_id, mode, ap_reward_index);
            return AP_ITEM_DROP;
        }
        u8 reward_index = ChecklistRewards_ApToGameIndex(mode, ap_reward_index);
        ChecklistRewards_Grant(mode, reward_index, /*announce=*/1);
        return AP_ITEM_APPLIED;
    }

    if (ap_item_id >= AP_EVENT_UNLOCK_BASE && ap_item_id < AP_EVENT_UNLOCK_BASE + EVKIND_NUM)
        return GateEvents_UnlockEvent(ap_item_id - AP_EVENT_UNLOCK_BASE);

    if (ap_item_id >= AP_ABILITY_UNLOCK_BASE && ap_item_id < AP_ABILITY_UNLOCK_BASE + COPYKIND_NUM)
        return GateAbilities_UnlockAbility(ap_item_id - AP_ABILITY_UNLOCK_BASE);

    if (ap_item_id >= AP_BASE_ABILITY_UNLOCK_BASE && ap_item_id < AP_BASE_ABILITY_UNLOCK_BASE + BASEABILITY_NUM)
        return GateBaseAbilities_UnlockAbility(ap_item_id - AP_BASE_ABILITY_UNLOCK_BASE);

    if (ap_item_id >= AP_PATCH_UNLOCK_BASE && ap_item_id < AP_PATCH_UNLOCK_BASE + PATCHKIND_NUM)
        return GatePatches_UnlockPatch(ap_item_id - AP_PATCH_UNLOCK_BASE);

    if (ap_item_id >= AP_ITEM_UNLOCK_BASE && ap_item_id < AP_ITEM_UNLOCK_BASE + ITUNLOCK_NUM)
        return GateItems_UnlockItem(ap_item_id - AP_ITEM_UNLOCK_BASE);

    if (ap_item_id >= AP_STAR_PIECE_UNLOCK_BASE && ap_item_id < AP_STAR_PIECE_UNLOCK_BASE + AP_STAR_PIECE_NUM)
        return GateApStar_UnlockPiece(ap_item_id - AP_STAR_PIECE_UNLOCK_BASE);

    // VCKIND_WHEELVSDEDEDE (855) is the stadium-only CPU machine and is never shipped.
    if (ap_item_id >= AP_MACHINE_UNLOCK_BASE && ap_item_id < AP_MACHINE_UNLOCK_BASE + AP_MACHINE_BIT_NUM &&
        ap_item_id != AP_MACHINE_UNLOCK_BASE + VCKIND_WHEELVSDEDEDE)
        return GateMachines_UnlockMachine(ap_item_id - AP_MACHINE_UNLOCK_BASE, /*announce=*/1);

    if (ap_item_id >= AP_BOX_UNLOCK_BASE && ap_item_id < AP_BOX_UNLOCK_BASE + BOXKIND_NUM)
        return GateBoxes_UnlockBox(ap_item_id - AP_BOX_UNLOCK_BASE);

    if (ap_item_id >= AP_STAGE_UNLOCK_AIRRIDE_BASE && ap_item_id < AP_STAGE_UNLOCK_AIRRIDE_BASE + AIRRIDE_NUM)
        return GateAirRideStages_UnlockStage(ap_item_id - AP_STAGE_UNLOCK_AIRRIDE_BASE, /*announce=*/1);

    if (ap_item_id >= AP_COLOR_UNLOCK_BASE && ap_item_id < AP_COLOR_UNLOCK_BASE + KIRBYCOLOR_NUM)
        return GateColors_UnlockColor(ap_item_id - AP_COLOR_UNLOCK_BASE, /*announce=*/1);

    if (ap_item_id >= AP_STAGE_UNLOCK_TOPRIDE_BASE && ap_item_id < AP_STAGE_UNLOCK_TOPRIDE_BASE + TOPRIDE_NUM)
        return GateTopRideStages_UnlockStage(ap_item_id - AP_STAGE_UNLOCK_TOPRIDE_BASE);

    if (ap_item_id >= AP_TOPRIDE_ITEM_UNLOCK_BASE && ap_item_id < AP_TOPRIDE_ITEM_UNLOCK_BASE + TRITEM_NUM)
        return GateTopRideItems_UnlockItem(ap_item_id - AP_TOPRIDE_ITEM_UNLOCK_BASE, /*announce=*/1);

    // GateTopRideItems_GiveItem refuses outside a running Top Ride race, so the give stays
    // queued. Announced here rather than in the give, which TrapLink shares.
    if (ap_item_id >= AP_TOPRIDE_ITEM_GIVE_BASE && ap_item_id < AP_TOPRIDE_ITEM_GIVE_BASE + TRITEM_NUM)
    {
        TopRideItemKind kind = ap_item_id - AP_TOPRIDE_ITEM_GIVE_BASE;
        int ok = GateTopRideItems_GiveItem(kind);
        if (ok && TopRideItemKind_Names[kind])
            APAnnounce_Grant("Received: TR ", TopRideItemKind_Names[kind],
                             tb_api->TopRideItemColor, NULL);
        return Applied(ok);
    }

    // In Top Ride a copy-ability item gives its TR analog; one with no analog waits for
    // City Trial or Air Ride.
    if (Scene_GetCurrentMajor() == MJRKIND_TOP &&
        ap_item_id >= AP_ITKIND_BASE && ap_item_id < AP_ITKIND_BASE + ITKIND_NUM)
    {
        CopyKind copy_kind = Ability_ItKindToCopyKind(ap_item_id - AP_ITKIND_BASE);
        if (copy_kind != COPYKIND_NONE)
        {
            int tr_item = GateTopRideItems_AbilityToItem(copy_kind);
            if (tr_item < 0)
                return AP_ITEM_RETRY;
            int ok = GateTopRideItems_GiveItem((TopRideItemKind)tr_item);
            if (ok)
                APAnnounce_Grant("Received: ", CopyKind_Names[copy_kind],
                                 tb_api->AbilityColors[copy_kind], " ability");
            return Applied(ok);
        }
    }

    if (ap_item_id >= AP_STADIUM_UNLOCK_BASE && ap_item_id < AP_STADIUM_UNLOCK_BASE + STKIND_NUM)
        return GateStadiums_UnlockStadium(ap_item_id - AP_STADIUM_UNLOCK_BASE, /*announce=*/1);

    // Save-only; the stats land at the next round start.
    if (ap_item_id >= AP_PERM_PATCH_BASE && ap_item_id < AP_PERM_PATCH_BASE + PATCHKIND_NUM)
        return PermanentPatch_GiveItem(ap_item_id - AP_PERM_PATCH_BASE);

    if (ap_item_id == AP_ITEM_PERM_PATCH_ALL_UP)
        return PermanentPatch_GiveAllUp();

    // Everything below needs a 3D round with its intro over. Top Ride runs under its own
    // minor and never passes this gate, so all it can apply sits above. intro_state reads
    // GMINTRO_END outside 3D, hence the minor check.
    MajorKind major = Scene_GetCurrentMajor();
    if (major != MJRKIND_CITY && major != MJRKIND_AIR)
        return AP_ITEM_RETRY;
    if (Scene_GetCurrentMinor() != MNRKIND_3D)
        return AP_ITEM_RETRY;
    if (Gm_GetIntroState() != GMINTRO_END)
        return AP_ITEM_RETRY;

    // Copy abilities grant through the rider, so they need no item data tables and bypass
    // the ability gate.
    if (ap_item_id >= AP_ITKIND_BASE && ap_item_id < AP_ITKIND_BASE + ITKIND_NUM)
    {
        CopyKind copy_kind = Ability_ItKindToCopyKind(ap_item_id - AP_ITKIND_BASE);
        if (copy_kind != COPYKIND_NONE)
            return Applied(Ability_GiveItem(copy_kind));
    }

    // CT Free Run and stadiums load no item data tables, and every spawn below would
    // crash in Item_GetItDataPtr.
    if (major == MJRKIND_CITY &&
        (Gm_GetCityMode() == CITYMODE_FREERUN || CityTrial_IsInStadium()))
        return AP_ITEM_RETRY;

    if (ap_item_id >= AP_EVENT_BASE && ap_item_id < AP_EVENT_BASE + EVKIND_NUM)
    {
        EventKind kind = ap_item_id - AP_EVENT_BASE;
        int ok = CTEvent_Give(kind);
        if (ok && EventKind_Names[kind])
            APAnnounce_Grant("Received: ", EventKind_Names[kind], tb_api->EventColor, NULL);
        return Applied(ok);
    }

    // "+1" stat patches apply in City Trial and Air Ride; every other item spawns a pickup,
    // City Trial only.
    if (ap_item_id >= AP_ITKIND_BASE && ap_item_id < AP_ITKIND_BASE + ITKIND_NUM)
    {
        ItemKind it_kind = ap_item_id - AP_ITKIND_BASE;

        PatchKind patch_kind = PatchItem_ItKindToPatchKind(it_kind);
        if (patch_kind != PATCHKIND_NUM)
        {
            if (!PatchItem_Give(patch_kind))
                return AP_ITEM_RETRY;
            NotifyItemReceived(it_kind);
            return AP_ITEM_APPLIED;
        }

        if (!Gm_IsInCity())
            return AP_ITEM_RETRY;

        int gave = (it_kind <= ITKIND_BOXRED) ? SpawnBoxHumansForward(it_kind)
                                              : SpawnItemHumans(it_kind);
        if (!gave)
            return AP_ITEM_RETRY;
        NotifyItemReceived(it_kind);
        return AP_ITEM_APPLIED;
    }

    if (ap_item_id == AP_ITEM_DROP_PATCHES_TRAP)
    {
        if (!Gm_IsInCity())
            return AP_ITEM_RETRY;
        int ok = PatchItem_DropTrap();
        if (ok)
            APAnnounce_Grant("Received: ", "Drop Patches", APColor_Trap, NULL);
        return Applied(ok);
    }

    // Announced by the gate, which owns the sphere names.
    if (ap_item_id >= AP_STAR_PIECE_GIVE_BASE && ap_item_id < AP_STAR_PIECE_GIVE_BASE + AP_STAR_PIECE_NUM)
        return GateApStar_GivePiece(ap_item_id - AP_STAR_PIECE_GIVE_BASE);

    if (ap_item_id == AP_ITEM_GIVE_DRAGOON || ap_item_id == AP_ITEM_GIVE_HYDRA)
    {
        int is_dragoon = ap_item_id == AP_ITEM_GIVE_DRAGOON;
        int result = GateMachines_GiveLegendaryMachine(is_dragoon ? 0 : 1);
        if (result == AP_ITEM_APPLIED)
            APAnnounce_Grant("Received: ", is_dragoon ? "Dragoon" : "Hydra", tb_api->MachineColor, NULL);
        return result;
    }

    if (ap_item_id == AP_ITEM_GIVE_AP_STAR)
        return GateApStar_GiveStar();

    if (ap_item_id == AP_ITEM_ALL_DOWN || ap_item_id == AP_ITEM_ALL_UP)
    {
        int is_up = ap_item_id == AP_ITEM_ALL_UP;
        int ok = PatchItem_GiveAllUp(is_up ? 1 : -1);
        if (ok)
            APAnnounce_Grant("Received: ", is_up ? "All Up" : "All Down",
                             is_up ? tb_api->PatchColors[PATCHKIND_CHARGE] : APColor_Trap, NULL);
        return Applied(ok);
    }

    if (ap_item_id == AP_ITEM_1_HP_TRAP)
    {
        int applied = 0;
        for (int i = 0; i < PLY_NUM; i++)
        {
            if (Ply_GetPKind(i) != PKIND_HMN)
                continue;
            GOBJ *mg = Ply_GetMachineGObj(i);
            if (!mg)
                continue;
            MachineData *md = mg->userdata;
            float damage = md->hp - 1.0f;
            if (damage > 0.0f)
            {
                Machine_GiveDamage(md, damage, &md->hurt_data->hitcoll_log_idx);
                applied = 1;
            }
        }
        if (applied)
            APAnnounce_Grant("Received: ", "1 HP", APColor_Trap, NULL);
        return Applied(applied);
    }

    OSReport("[APItems] Unknown AP item ID %d, dropped\n", ap_item_id);
    return AP_ITEM_DROP;
}

int APItems_Queue(uint ap_item_id)
{
    if (ap_save->unprocessed_count >= MAX_RECEIVED_ITEMS)
        return 0;
    ap_save->unprocessed_items[ap_save->unprocessed_count++] = ap_item_id;
    return 1;
}

// At most one queued item resolves per frame. RETRY items stay queued without blocking
// the items behind them.
void APItems_OnFrameStart(void)
{
    APItems_CheckMailbox();

    for (uint i = 0; i < ap_save->unprocessed_count; i++)
    {
        if (APItems_HandleItem(ap_save->unprocessed_items[i]) == AP_ITEM_RETRY)
            continue;

        ap_save->unprocessed_count--;
        ap_save->unprocessed_items[i] = ap_save->unprocessed_items[ap_save->unprocessed_count];
        break;
    }
}

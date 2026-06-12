#include "game.h"
#include "inline.h"
#include "stadium.h"
#include "hoshi/func.h"

#include "ap_item_handler.h"
#include "checklist_rewards.h"
#include "kirby_scale.h"
#include "textbox_api.h"
#include "city_trial_event.h"
#include "ability_item.h"
#include "patch_item.h"
#include "patch_cap.h"
#include "gate_events.h"
#include "gate_abilities.h"
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
#include "main.h"

// Check the mailbox for an incoming item from the AP client.
// Bump the received counter, add to unprocessed list,
// and immediately acknowledge receipt.
// Returns 1 if an item was received, 0 otherwise.
int APItems_CheckMailbox()
{
    static int warned_full = 0;

    uint incoming = ap_data->incoming_item_id;
    if (incoming == 0)
        return 0;

    if (ap_save->unprocessed_count >= MAX_RECEIVED_ITEMS)
    {
        // Queue full: leave the item in the mailbox (do NOT clear it). The client
        // gates its next write on incoming_item_id == 0 and only advances its send
        // cursor after a successful write, so holding the value here applies natural
        // backpressure and the item is retried each frame as the list drains.
        // Clearing it would lose the item permanently: the client already advanced
        // past it, and item_received_count was never incremented for it.
        if (!warned_full)
        {
            OSReport("[APItems] unprocessed queue full (%d); holding item %d in mailbox\n",
                     MAX_RECEIVED_ITEMS, incoming);
            warned_full = 1;
        }
        return 0;
    }
    warned_full = 0;

    uint idx = ap_save->item_received_count;
    ap_save->item_received_count++;

    // Add to unprocessed list
    ap_save->unprocessed_items[ap_save->unprocessed_count] = incoming;
    ap_save->unprocessed_count++;

    // Sync received count to shared memory so AP client can read it
    ap_data->item_received_index = ap_save->item_received_count;

    OSReport("[APItems] AP item ID %d received (index %d).\n", incoming, idx);

    // Clear the mailbox so the client can write the next item
    ap_data->incoming_item_id = 0;
    return 1;
}

// Pick the TextBox color for a directly-received ITKIND item, by category.
// Detrimental kinds (stat-downs, min/none, fakes) read as traps; stat-ups and
// maxes take their per-stat patch color (All Up falls back to a gold-ish one);
// legendary pieces machine-blue; boxes take their per-box color; the rest
// (food, candy, weapons) general-item green.
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
            return tb_api->TrapColor;
        case ITKIND_WEIGHT:                            return tb_api->PatchColors[PATCHKIND_WEIGHT];
        case ITKIND_ACCEL:                             return tb_api->PatchColors[PATCHKIND_ACCEL];
        case ITKIND_TOPSPEED:   case ITKIND_SPEEDMAX:  return tb_api->PatchColors[PATCHKIND_TOPSPEED];
        case ITKIND_TURN:                              return tb_api->PatchColors[PATCHKIND_TURN];
        case ITKIND_CHARGE:     case ITKIND_CHARGEMAX: return tb_api->PatchColors[PATCHKIND_CHARGE];
        case ITKIND_GLIDE:                             return tb_api->PatchColors[PATCHKIND_GLIDE];
        case ITKIND_OFFENSE:    case ITKIND_OFFENSEMAX:return tb_api->PatchColors[PATCHKIND_OFFENSE];
        case ITKIND_DEFENSE:    case ITKIND_DEFENSEMAX:return tb_api->PatchColors[PATCHKIND_DEFENSE];
        case ITKIND_HP:                                return tb_api->PatchColors[PATCHKIND_HP];
        case ITKIND_ALLUP:                             return tb_api->PatchColors[PATCHKIND_CHARGE]; // affects all stats - gold-ish representative
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

// Announce a directly-received ITKIND item ("Received: <name>").
static void NotifyItemReceived(ItemKind k)
{
    if ((unsigned)k < ITKIND_NUM && ItemKind_Names[k])
        tb_api->EnqueueColoredNoun("Received: ", ItemKind_Names[k], ItemReceiveColor(k), NULL);
}

// Distance (in machine forward units) to push a granted box ahead of the
// rider, so it lands in front of them to drive into rather than on top of them.
#define AP_BOX_SPAWN_FORWARD 10.0f

// Spawn a box pickup for every human player, offset ahead of the machine
// along its forward vector. Mirrors SpawnItemPlayer (the initial raycast from
// is_airborne=1 settles the box onto the ground) but places it at
// md->pos + AP_BOX_SPAWN_FORWARD * md->forward instead of right on the rider.
// Caller must guarantee item data tables are loaded (City Trial only).
static void SpawnBoxHumansForward(ItemKind kind)
{
    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;
        GOBJ *mg = Ply_GetMachineGObj(i);
        if (!mg)
            continue;
        MachineData *md = mg->userdata;

        Vec3 pos;
        pos.X = md->pos.X + AP_BOX_SPAWN_FORWARD * md->forward.X;
        pos.Y = md->pos.Y + AP_BOX_SPAWN_FORWARD * md->forward.Y;
        pos.Z = md->pos.Z + AP_BOX_SPAWN_FORWARD * md->forward.Z;

        ItemDesc desc;
        Item_InitDesc(&desc, kind, 1.0f, 0, &pos, &md->up, &md->forward,
                      -1, -1, 1, 3, -1, -1);
        Item_Create(&desc);
    }
}

// Handle an AP item by its raw ID. Maps the ID directly to game behavior.
// Returns an APItemResult: AP_ITEM_APPLIED on success, AP_ITEM_RETRY if it
// can't be applied yet (wrong scene / event already active - keep and retry),
// or AP_ITEM_DROP if the ID is unrecognized or out of range (remove without
// applying, so a malformed ID can't wedge the queue and re-log every frame).
int APItems_HandleItem(uint ap_item_id)
{
    // Items that apply immediately with no scene requirement
    switch (ap_item_id)
    {
        case AP_ITEM_CHECKBOX_FILLER_AIRRIDE:
            Checklist_GrantFiller(GMMODE_AIRRIDE);
            Checklist_AnnounceFiller(GMMODE_AIRRIDE);
            return 1;
        case AP_ITEM_CHECKBOX_FILLER_TOPRIDE:
            Checklist_GrantFiller(GMMODE_TOPRIDE);
            Checklist_AnnounceFiller(GMMODE_TOPRIDE);
            return 1;
        case AP_ITEM_CHECKBOX_FILLER_CITYTRIAL:
            Checklist_GrantFiller(GMMODE_CITYTRIAL);
            Checklist_AnnounceFiller(GMMODE_CITYTRIAL);
            return 1;
        case AP_ITEM_PATCH_CAP_INCREASE:
            PatchCap_Increment();
            return 1;
        case AP_ITEM_SPAWN_RATE_UP:
            SpawnRate_Increment();
            return 1;
    }

    // Big / Small Kirby cosmetic model scaling. Handled here, above the 3D-only
    // scene gate below, because it also applies in Top Ride (minor 19), which
    // never satisfies that gate. KirbyScale_HandleItem does its own scene check
    // and returns RETRY until the player is in a scene with Kirby models.
    if (ap_item_id == AP_ITEM_BIG_KIRBY || ap_item_id == AP_ITEM_SMALL_KIRBY)
        return KirbyScale_HandleItem(ap_item_id);

    // Checklist rewards (AP_CHECKLIST_REWARD_BASE + mode*50 + reward_index).
    // Apply immediately regardless of scene - the hook handles all reward checks at runtime.
    if (ap_item_id >= AP_CHECKLIST_REWARD_BASE && ap_item_id < AP_CHECKLIST_REWARD_BASE + 150)
    {
        u32 offset = ap_item_id - AP_CHECKLIST_REWARD_BASE;
        GameMode mode = offset / 50;
        u8 ap_reward_index = offset % 50;
        // Reward bands are stride-50 but each mode uses fewer (AR 46, TR 33,
        // CT 44). IDs in the gaps are not valid rewards - drop rather than
        // grant an out-of-range index.
        if (ap_reward_index >= ChecklistRewards_GetRewardCount(mode))
        {
            OSReport("[APItems] Checklist reward ID %d out of range (mode %d index %d) - dropping\n",
                     ap_item_id, mode, ap_reward_index);
            return AP_ITEM_DROP;
        }
        // The item ID carries the apworld's clear_kind-sorted reward index;
        // translate to the game reward-table index the grant path expects.
        u8 reward_index = ChecklistRewards_ApToGameIndex(mode, ap_reward_index);
        ChecklistRewards_Grant(mode, reward_index, /*announce=*/1);
        return AP_ITEM_APPLIED;
    }

    // Event unlock items (AP_EVENT_UNLOCK_BASE + EventKind)
    if (ap_item_id >= AP_EVENT_UNLOCK_BASE && ap_item_id < AP_EVENT_UNLOCK_BASE + EVKIND_NUM)
    {
        EventKind kind = ap_item_id - AP_EVENT_UNLOCK_BASE;
        return GateEvents_UnlockEvent(kind);
    }

    // Copy ability unlock items (AP_ABILITY_UNLOCK_BASE + CopyKind)
    if (ap_item_id >= AP_ABILITY_UNLOCK_BASE && ap_item_id < AP_ABILITY_UNLOCK_BASE + COPYKIND_NUM)
    {
        CopyKind kind = ap_item_id - AP_ABILITY_UNLOCK_BASE;
        return GateAbilities_UnlockAbility(kind);
    }

    // Patch type unlock items (AP_PATCH_UNLOCK_BASE + PatchKind)
    if (ap_item_id >= AP_PATCH_UNLOCK_BASE && ap_item_id < AP_PATCH_UNLOCK_BASE + PATCHKIND_NUM)
    {
        PatchKind kind = ap_item_id - AP_PATCH_UNLOCK_BASE;
        return GatePatches_UnlockPatch(kind);
    }

    // Individual item unlock items (AP_ITEM_UNLOCK_BASE + ItemUnlockKind)
    if (ap_item_id >= AP_ITEM_UNLOCK_BASE && ap_item_id < AP_ITEM_UNLOCK_BASE + ITUNLOCK_NUM)
    {
        ItemUnlockKind kind = ap_item_id - AP_ITEM_UNLOCK_BASE;
        return GateItems_UnlockItem(kind);
    }

    // Machine unlock items (AP_MACHINE_UNLOCK_BASE + MachineKind, IDs 830-854).
    // The AP world generates an unlock for every in-range VCKIND (0-24); only
    // VCKIND_WHEELVSDEDEDE (25) is excluded.
    // VCKIND_FREE and VCKIND_STEER are Top Ride machines whose bits ARE read by
    // the Top Ride lobby gating (GateMachines_TRLobbyCanStart / IsTRMachineUnlocked),
    // so granting them is NOT a no-op. A few bits (WINGKIRBY, WHEELNORMAL,
    // WHEELKIRBY) are set here but read by no game code, so flipping them is
    // harmless. VCKIND_WHEELDEDEDE (24) is the player-facing Dedede machine and
    // the canonical Dedede unlock. VCKIND_WHEELVSDEDEDE (25) is the Vs. King
    // Dedede stadium's CPU-only machine and is *not* part of this range - the
    // upper bound stops at it so an erroneous ID 855 falls through to the
    // unknown-item path instead of flipping a dead bit.
    if (ap_item_id >= AP_MACHINE_UNLOCK_BASE && ap_item_id < AP_MACHINE_UNLOCK_BASE + VCKIND_WHEELVSDEDEDE)
    {
        MachineKind kind = ap_item_id - AP_MACHINE_UNLOCK_BASE;
        return GateMachines_UnlockMachine(kind, /*announce=*/1);
    }

    // Box type unlock items (AP_BOX_UNLOCK_BASE + BoxKind)
    if (ap_item_id >= AP_BOX_UNLOCK_BASE && ap_item_id < AP_BOX_UNLOCK_BASE + BOXKIND_NUM)
    {
        BoxKind kind = ap_item_id - AP_BOX_UNLOCK_BASE;
        return GateBoxes_UnlockBox(kind);
    }

    // Air Ride stage unlock items (AP_STAGE_UNLOCK_AIRRIDE_BASE + stage_kind)
    if (ap_item_id >= AP_STAGE_UNLOCK_AIRRIDE_BASE &&
        ap_item_id < AP_STAGE_UNLOCK_AIRRIDE_BASE + AIRRIDE_NUM)
    {
        int stage_kind = ap_item_id - AP_STAGE_UNLOCK_AIRRIDE_BASE;
        return GateAirRideStages_UnlockStage(stage_kind, /*announce=*/1);
    }

    // Kirby color unlock items (AP_COLOR_UNLOCK_BASE + KirbyColor)
    if (ap_item_id >= AP_COLOR_UNLOCK_BASE && ap_item_id < AP_COLOR_UNLOCK_BASE + KIRBYCOLOR_NUM)
    {
        int color = ap_item_id - AP_COLOR_UNLOCK_BASE;
        return GateColors_UnlockColor(color, /*announce=*/1);
    }

    // Top Ride stage unlock items (AP_STAGE_UNLOCK_TOPRIDE_BASE + course)
    if (ap_item_id >= AP_STAGE_UNLOCK_TOPRIDE_BASE &&
        ap_item_id < AP_STAGE_UNLOCK_TOPRIDE_BASE + TOPRIDE_NUM)
    {
        int course = ap_item_id - AP_STAGE_UNLOCK_TOPRIDE_BASE;
        return GateTopRideStages_UnlockStage(course);
    }

    // Top Ride item unlock items (AP_TOPRIDE_ITEM_UNLOCK_BASE + TopRideItemKind)
    if (ap_item_id >= AP_TOPRIDE_ITEM_UNLOCK_BASE &&
        ap_item_id < AP_TOPRIDE_ITEM_UNLOCK_BASE + TRITEM_NUM)
    {
        TopRideItemKind kind = ap_item_id - AP_TOPRIDE_ITEM_UNLOCK_BASE;
        return GateTopRideItems_UnlockItem(kind, /*announce=*/1);
    }

    // Top Ride item give items (AP_TOPRIDE_ITEM_GIVE_BASE + TopRideItemKind).
    // Handled above the MNRKIND_3D gate below since Top Ride uses MNRKIND_19.
    // GateTopRideItems_GiveItem returns 0 when not in Top Ride, which keeps
    // the item in the unprocessed queue until the player enters a TR match.
    if (ap_item_id >= AP_TOPRIDE_ITEM_GIVE_BASE &&
        ap_item_id < AP_TOPRIDE_ITEM_GIVE_BASE + TRITEM_NUM)
    {
        TopRideItemKind kind = ap_item_id - AP_TOPRIDE_ITEM_GIVE_BASE;
        // Notify here (not inside GateTopRideItems_GiveItem) - TrapLink also
        // calls that handler and shows its own "Trap received!" message.
        int ok = GateTopRideItems_GiveItem(kind);
        if (ok && (unsigned)kind < TRITEM_NUM && TopRideItemKind_Names[kind])
            tb_api->EnqueueColoredNoun("Received: TR ", TopRideItemKind_Names[kind],
                                       tb_api->TopRideItemColor, NULL);
        return ok;
    }

    // Copy ability ITKIND items in Top Ride. Top Ride has no RiderData kirbys
    // to receive a real copy ability, and the ITKIND branch further down sits
    // behind the MNRKIND_3D scene gate that Top Ride (MNRKIND_19) never
    // satisfies - so handle it here, above that gate. Map the ability to its
    // Top Ride item analog (Freeze->Freeze Fan, Fire->Fire, Bomb->Bomb,
    // Mic->Walky) and give that instead. Abilities with no analog (Wheel,
    // Sleep, Sword, Plasma, Needle, Tornado, Wing) fall through to retry once
    // the player leaves Top Ride for a copy-ability mode (City Trial / Air Ride).
    if (Scene_GetCurrentMajor() == MJRKIND_TOP &&
        ap_item_id >= AP_ITKIND_BASE && ap_item_id < AP_ITKIND_BASE + ITKIND_NUM)
    {
        ItemKind it_kind = ap_item_id - AP_ITKIND_BASE;
        CopyKind copy_kind = Ability_ItKindToCopyKind(it_kind);
        if (copy_kind != COPYKIND_NONE)
        {
            int tr_item = GateTopRideItems_AbilityToItem(copy_kind);
            if (tr_item < 0)
                return 0; // no Top Ride analog - retry in City Trial / Air Ride
            int ok = GateTopRideItems_GiveItem((TopRideItemKind)tr_item);
            if (ok)
                tb_api->EnqueueColoredNoun("Received: ", CopyKind_Names[copy_kind],
                                           tb_api->AbilityColors[copy_kind], " ability");
            return ok;
        }
    }

    // Stadium unlock items (AP_STADIUM_UNLOCK_BASE + StadiumKind)
    if (ap_item_id >= AP_STADIUM_UNLOCK_BASE && ap_item_id < AP_STADIUM_UNLOCK_BASE + STKIND_NUM)
    {
        StadiumKind kind = ap_item_id - AP_STADIUM_UNLOCK_BASE;
        return GateStadiums_UnlockStadium(kind, /*announce=*/1);
    }

    // Permanent +1 patches (AP_PERM_PATCH_BASE + PatchKind). Save-only - the
    // stat application happens at the next round start, so the receive path
    // never needs a 3D scene and stays above the scene gate below.
    if (ap_item_id >= AP_PERM_PATCH_BASE && ap_item_id < AP_PERM_PATCH_BASE + PATCHKIND_NUM)
    {
        PatchKind kind = ap_item_id - AP_PERM_PATCH_BASE;
        return PermanentPatch_GiveItem(kind);
    }

    // Permanent +1 All Up
    if (ap_item_id == AP_ITEM_PERM_PATCH_ALL_UP)
        return PermanentPatch_GiveAllUp();

    // All remaining items require being in an actual 3D game scene
    // with the intro/countdown finished.
    // Check minor == MNRKIND_3D (not just major) because the CSS and other
    // non-gameplay minors run under the same major, and intro_state defaults
    // to 0 (GMINTRO_END) when not in 3D, bypassing that check.
    MajorKind major = Scene_GetCurrentMajor();
    if (major != MJRKIND_CITY && major != MJRKIND_AIR && major != MJRKIND_TOP)
        return 0;
    if (Scene_GetCurrentMinor() != MNRKIND_3D)
        return 0;
    if (Gm_GetIntroState() != GMINTRO_END)
        return 0;
    // CT Free Run and stadiums don't load item data tables. Any handler
    // below that touches the item spawn pipeline would crash
    // Item_GetItDataPtr; the remaining non-spawn handlers (HP damage,
    // projectile/enemy traps) aren't interesting there either. Queue for
    // a real game mode.
    if (major == MJRKIND_CITY &&
        (Gm_GetCityMode() == CITYMODE_FREERUN || Gm_IsStadiumMode()))
        return 0;

    // City Trial events (AP_EVENT_BASE + EventKind)
    if (ap_item_id >= AP_EVENT_BASE && ap_item_id < AP_EVENT_BASE + EVKIND_NUM)
    {
        EventKind kind = ap_item_id - AP_EVENT_BASE;
        int ok = Event_GiveItem(kind);
        if (ok && kind < EVKIND_NUM && EventKind_Names[kind])
            tb_api->EnqueueColoredNoun("Received: ", EventKind_Names[kind], tb_api->EventColor, NULL);
        return ok;
    }

    // Direct ITKIND items (AP_ITKIND_BASE + ItemKind)
    // ITKIND_COPY* always go through Ability_GiveItem so they bypass the
    // ability gate - an AP-granted copy item is meant to apply regardless of
    // unlock state, matching the Air Ride path. The nine "+1" stat patches go
    // through Patch_GiveItem, which applies in both City Trial (item pickup) and
    // Air Ride (direct stat change). Other ITKIND items spawn a real pickup in
    // City Trial only (so the visual plays); outside City Trial the item data
    // tables aren't loaded and we can't service them.
    if (ap_item_id >= AP_ITKIND_BASE && ap_item_id < AP_ITKIND_BASE + ITKIND_NUM)
    {
        ItemKind it_kind = ap_item_id - AP_ITKIND_BASE;
        CopyKind copy_kind = Ability_ItKindToCopyKind(it_kind);
        if (copy_kind != COPYKIND_NONE)
            return Ability_GiveItem(copy_kind);

        // Stat "+1" patches apply to machine stats in City Trial and Air Ride.
        // Patch_GiveItem spawns the pickup in CT and applies directly via
        // Machine_GivePatch in AR. Top Ride has no MachineData, so defer there
        // (return 0 -> retry once the player reaches a patch-capable mode). The
        // CT Free Run / stadium cases are already filtered by the scene gate above.
        PatchKind patch_kind = Patch_ItKindToPatchKind(it_kind);
        if (patch_kind != PATCHKIND_NUM)
        {
            if (major != MJRKIND_CITY && major != MJRKIND_AIR)
                return 0;
            Patch_GiveItem(patch_kind, 1);
            NotifyItemReceived(it_kind);
            return 1;
        }

        if (Gm_IsInCity())
        {
            // Boxes spawn ahead of the rider (to drive into and break) rather
            // than on top of them; everything else spawns at the rider.
            if (it_kind <= ITKIND_BOXRED)
                SpawnBoxHumansForward(it_kind);
            else
                SpawnItemHumans(it_kind);
            NotifyItemReceived(it_kind);
            return 1;
        }
        return 0;
    }

    // Drop-patches trap - eject each human rider's stats as physical patches.
    // City Trial only (Free Run is already blocked by the major+city_mode
    // gate above).
    if (ap_item_id == AP_ITEM_DROP_PATCHES_TRAP)
    {
        if (!Gm_IsInCity())
            return 0;
        int ok = Patch_DropTrap();
        if (ok)
            tb_api->EnqueueColoredNoun("Received: ", "Drop Patches", tb_api->TrapColor, NULL);
        return ok;
    }

    // Legendary machine assembly - give assembled Dragoon/Hydra via cinematic
    if (ap_item_id == AP_ITEM_GIVE_DRAGOON)
    {
        int ok = GateMachines_GiveLegendaryMachine(0);
        if (ok)
            tb_api->EnqueueColoredNoun("Received: ", "Dragoon", tb_api->MachineColor, NULL);
        return ok;
    }
    if (ap_item_id == AP_ITEM_GIVE_HYDRA)
    {
        int ok = GateMachines_GiveLegendaryMachine(1);
        if (ok)
            tb_api->EnqueueColoredNoun("Received: ", "Hydra", tb_api->MachineColor, NULL);
        return ok;
    }

    // All Down - lower all stats by 1 for each human player
    if (ap_item_id == AP_ITEM_ALL_DOWN)
    {
        int ok = Patch_AllUp_GiveItem(-1);
        if (ok)
            tb_api->EnqueueColoredNoun("Received: ", "All Down", tb_api->TrapColor, NULL);
        return ok;
    }

    // All Up - raise all stats by 1 for each human player
    if (ap_item_id == AP_ITEM_ALL_UP)
    {
        int ok = Patch_AllUp_GiveItem(1);
        if (ok)
            tb_api->EnqueueColoredNoun("Received: ", "All Up", tb_api->PatchColors[PATCHKIND_CHARGE], NULL);
        return ok;
    }

    // 1 HP trap - set each human player's HP to 1
    if (ap_item_id == AP_ITEM_1_HP_TRAP)
    {
        int applied = 0;
        for (int i = 0; i < 5; i++)
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
                Machine_GiveDamage(md, damage, mg);
                applied = 1;
            }
        }
        if (applied)
            tb_api->EnqueueColoredNoun("Received: ", "1 HP", tb_api->TrapColor, NULL);
        return applied;
    }

    OSReport("[APItems] Unknown AP item ID: %d - dropping\n", ap_item_id);
    return AP_ITEM_DROP;
}

// Append an AP item ID to the unprocessed queue. Returns 1 if queued,
// 0 if the queue is full.
int APItems_Queue(uint ap_item_id)
{
    if (ap_save->unprocessed_count >= MAX_RECEIVED_ITEMS)
        return 0;
    ap_save->unprocessed_items[ap_save->unprocessed_count++] = ap_item_id;
    return 1;
}

// Initialize the GOBJ that will process received items every frame
void APItems_OnSceneChange()
{
    GOBJ_EZCreator(0, 0, 0, 0, HSD_Free, HSD_OBJKIND_NONE, 0, APItems_PerFrame, 0, 0, 0, 0);
}

// Scan the unprocessed list for the first item we can resolve. Processes one
// item per frame. Items that can't apply yet (e.g., blocked events) are skipped,
// allowing items behind them to process out of order. Applied items and
// unrecognized/out-of-range items are both removed; only RETRY items stay.
void APItems_PerFrame(GOBJ *g)
{
    // Pull from mailbox into persistent list
    int item_received = APItems_CheckMailbox();

    // Scan unprocessed items for one we can resolve this frame.
    for (uint i = 0; i < ap_save->unprocessed_count; i++)
    {
        uint item_id = ap_save->unprocessed_items[i];
        int result = APItems_HandleItem(item_id);
        if (result == AP_ITEM_RETRY)
            continue;

        if (result == AP_ITEM_APPLIED)
            OSReport("[APItems] AP item ID %d applied.\n", item_id);
        else
            OSReport("[APItems] AP item ID %d dropped.\n", item_id);

        // Remove by swapping with last element.
        ap_save->unprocessed_count--;
        ap_save->unprocessed_items[i] = ap_save->unprocessed_items[ap_save->unprocessed_count];
        break;
    }

}

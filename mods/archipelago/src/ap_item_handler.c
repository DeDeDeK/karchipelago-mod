#include "game.h"
#include "inline.h"
#include "stadium.h"
#include "hoshi/func.h"

#include "ap_item_handler.h"
#include "checklist_rewards.h"
#include "textbox.h"
#include "city_trial_event.h"
#include "ability_item.h"
#include "patch_item.h"
#include "spawn_item.h"
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
#include "spawn_enemy.h"
#include "spawn_projectile.h"
#include "main.h"

// Check the mailbox for an incoming item from the AP client.
// Store it in the persistent received list, add to unprocessed list,
// and immediately acknowledge receipt.
// Returns 1 if an item was received, 0 otherwise.
int APItems_CheckMailbox()
{
    uint incoming = ap_data->incoming_item_id;
    if (incoming == 0)
        return 0;

    uint idx = ap_save->item_received_count;
    if (idx >= MAX_RECEIVED_ITEMS)
    {
        OSReport("APItems_CheckMailbox: received list is full!\n");
        ap_data->incoming_item_id = 0;
        return 0;
    }

    // Store in persistent received list
    ap_save->received_items[idx] = incoming;
    ap_save->item_received_count++;

    // Add to unprocessed list
    ap_save->unprocessed_items[ap_save->unprocessed_count] = incoming;
    ap_save->unprocessed_count++;

    // Sync received count to shared memory so AP client can read it
    ap_data->item_received_index = ap_save->item_received_count;

    OSReport("AP item ID %d received (index %d).\n", incoming, idx);

    // Clear the mailbox so the client can write the next item
    ap_data->incoming_item_id = 0;
    return 1;
}

// Handle an AP item by its raw ID. Maps the ID directly to game behavior.
// Returns 1 if the item was successfully applied, 0 if it can't be applied yet
// (e.g., player is not in the right scene, or an event is already active).
int APItems_HandleItem(uint ap_item_id)
{
    // Items that apply immediately with no scene requirement
    switch (ap_item_id)
    {
        case AP_ITEM_CHECKBOX_FILLER_AIRRIDE:
            Checklist_GrantFiller(GMMODE_AIRRIDE);
            return 1;
        case AP_ITEM_CHECKBOX_FILLER_TOPRIDE:
            Checklist_GrantFiller(GMMODE_TOPRIDE);
            return 1;
        case AP_ITEM_CHECKBOX_FILLER_CITYTRIAL:
            Checklist_GrantFiller(GMMODE_CITYTRIAL);
            return 1;
        case AP_ITEM_PATCH_CAP_INCREASE:
            PatchCap_Increment();
            return 1;
    }

    // Checklist rewards (AP_CHECKLIST_REWARD_BASE + mode*50 + reward_index).
    // Apply immediately regardless of scene — the hook handles all reward checks at runtime.
    if (ap_item_id >= AP_CHECKLIST_REWARD_BASE && ap_item_id < AP_CHECKLIST_REWARD_BASE + 150)
    {
        u32 offset = ap_item_id - AP_CHECKLIST_REWARD_BASE;
        GameMode mode = offset / 50;
        u8 reward_index = offset % 50;
        ChecklistRewards_Grant(mode, reward_index);
        return 1;
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

    // Machine unlock items (AP_MACHINE_UNLOCK_BASE + MachineKind)
    if (ap_item_id >= AP_MACHINE_UNLOCK_BASE && ap_item_id < AP_MACHINE_UNLOCK_BASE + VCKIND_NUM)
    {
        MachineKind kind = ap_item_id - AP_MACHINE_UNLOCK_BASE;
        return GateMachines_UnlockMachine(kind);
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
        return GateAirRideStages_UnlockStage(stage_kind);
    }

    // Kirby color unlock items (AP_COLOR_UNLOCK_BASE + KirbyColor)
    if (ap_item_id >= AP_COLOR_UNLOCK_BASE && ap_item_id < AP_COLOR_UNLOCK_BASE + KIRBYCOLOR_NUM)
    {
        int color = ap_item_id - AP_COLOR_UNLOCK_BASE;
        return GateColors_UnlockColor(color);
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
        return GateTopRideItems_UnlockItem(kind);
    }

    // Stadium unlock items (AP_STADIUM_BASE + StadiumKind)
    if (ap_item_id >= AP_STADIUM_BASE && ap_item_id < AP_STADIUM_BASE + STKIND_NUM)
    {
        StadiumKind kind = ap_item_id - AP_STADIUM_BASE;
        ap_save->stadium_unlocked_mask |= (1 << kind);
        Gm_StadiumSetUnlockedDirect(kind);
        Gm_StadiumSetNewLabelDirect(kind);
        return 1;
    }

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

    // Permanent +1 patches (AP_PERM_PATCH_BASE + PatchKind)
    if (ap_item_id >= AP_PERM_PATCH_BASE && ap_item_id < AP_PERM_PATCH_BASE + PATCHKIND_NUM)
    {
        PatchKind kind = ap_item_id - AP_PERM_PATCH_BASE;
        return PermanentPatch_GiveItem(kind);
    }

    // Permanent +1 All Up
    if (ap_item_id == AP_ITEM_PERM_PATCH_ALL_UP)
        return PermanentPatch_GiveAllUp();

    // City Trial events (AP_EVENT_BASE + EventKind)
    if (ap_item_id >= AP_EVENT_BASE && ap_item_id < AP_EVENT_BASE + EVKIND_NUM)
    {
        EventKind kind = ap_item_id - AP_EVENT_BASE;
        return Event_GiveItem(kind);
    }

    // Custom City Trial event
    if (ap_item_id == AP_ITEM_EVENT_CUSTOM)
    {
        if (custom_events && Gm_GetCurrentGrKind() == GRKIND_CITY1)
            return custom_events->Do(CUSTOM_EVKIND_GRAVITY_CHANGE);
        return 0;
    }

    // Direct ITKIND items (AP_ITKIND_BASE + ItemKind)
    // City Trial only — item data tables aren't loaded in Air Ride / Top Ride.
    if (ap_item_id >= AP_ITKIND_BASE && ap_item_id < AP_ITKIND_BASE + ITKIND_NUM)
    {
        if (!Gm_IsInCity())
            return 0;
        ItemKind it_kind = ap_item_id - AP_ITKIND_BASE;
        SpawnItemHumans(it_kind);
        return 1;
    }

    // Meteor trap — spawn a meteor directly above each human player
    if (ap_item_id == AP_ITEM_METEOR_TRAP)
        return SpawnEnemy_MeteorTrap();

    // Bomb trap — spawn a Bomb-ability projectile from each human player
    if (ap_item_id == AP_ITEM_BOMB_TRAP)
        return SpawnProjectile_BombTrap();

    // Gordo trap — spawn a Gordo projectile (Phan Phan throw) from each human
    if (ap_item_id == AP_ITEM_GORDO_TRAP)
        return SpawnProjectile_GordoTrap();

    // Sensor bomb trap — spawn a Sensor Bomb projectile from each human
    if (ap_item_id == AP_ITEM_SENSORBOMB_TRAP)
        return SpawnProjectile_SensorBombTrap();

    // Legendary machine assembly — give assembled Dragoon/Hydra via cinematic
    if (ap_item_id == AP_ITEM_GIVE_DRAGOON)
        return GateMachines_GiveLegendaryMachine(0);
    if (ap_item_id == AP_ITEM_GIVE_HYDRA)
        return GateMachines_GiveLegendaryMachine(1);

    // All Down — lower all stats by 1 for each human player
    if (ap_item_id == AP_ITEM_ALL_DOWN)
        return Patch_AllUp_GiveItem(-1);

    // 1 HP trap — set each human player's HP to 1
    if (ap_item_id == AP_ITEM_1_HP_TRAP)
    {
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
                Machine_GiveDamage(md, damage, mg);
        }
        return 1;
    }

    OSReport("Unknown AP item ID: %d\n", ap_item_id);
    return 0;
}

// Initialize the GOBJ that will process received items every frame
void APItems_OnSceneChange()
{
    GOBJ_EZCreator(0, 0, 0, 0, HSD_Free, HSD_OBJKIND_NONE, 0, APItems_PerFrame, 0, 0, 0, 0);
}

// Scan the unprocessed list for the first item that can be applied.
// Processes one item per frame. Items that can't apply yet (e.g., blocked events)
// are skipped, allowing items behind them to process out of order.
void APItems_PerFrame(GOBJ *g)
{
    // Pull from mailbox into persistent list
    int item_received = APItems_CheckMailbox();

    // Scan unprocessed items for one we can handle
    for (uint i = 0; i < ap_save->unprocessed_count; i++)
    {
        uint item_id = ap_save->unprocessed_items[i];
        if (APItems_HandleItem(item_id))
        {
            OSReport("AP item ID %d applied.\n", item_id);
            // Remove by swapping with last element
            ap_save->unprocessed_count--;
            ap_save->unprocessed_items[i] = ap_save->unprocessed_items[ap_save->unprocessed_count];
            break;
        }
    }

}

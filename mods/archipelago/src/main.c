#include <string.h>

#include "os.h"
#include "game.h"
#include "scene.h"
#include "hoshi/mod.h"
#include "hoshi/func.h"
#include "stage.h"
#include "stadium.h"

#include "main.h"
#include "save_icon.h"
#include "deathlink.h"
#include "city_trial_event.h"
#include "ap_item_handler.h"
#include "kirby_scale.h"
#include "energylink.h"
#include "traplink.h"
#include "fake_patches.h"
#include "patch_item.h"
#include "checklist_rewards.h"
#include "check_detection.h"
#include "ap_checklist.h"
#include "gate_stadiums.h"
#include "patch_cap.h"
#include "gate_events.h"
#include "gate_abilities.h"
#include "gate_boxes.h"
#include "gate_items.h"
#include "gate_machines.h"
#include "gate_airride_stages.h"
#include "gate_topride_stages.h"
#include "gate_topride_items.h"
#include "gate_colors.h"
#include "spawn_rate.h"
#include "item_spawn_filter.h"
#include "settings_menu.h"
#include "main_menu.h"
#include "goal_max_stats_ct.h"

// Define global variables.
APData *ap_data;
APSave *ap_save;
const TextBoxAPI *tb_api = 0;

// The checklist mode the custom_checklist framework assigned to the AP tab.
// GMMODE_NUM until APChecklist_Register lands; see main.h.
int ap_checklist_mode = GMMODE_NUM;

ModDesc mod_desc = {
    .name = "KARchipelago",                     // Name of the mod.
    .author = "DeDeDK",                         // Creator of the mod.
    .version.major = 1,                         // Version of the mod.
    .version.minor = 0,
    .save_size = sizeof(struct APSave),         // Size of the save data your mod uses.
    .save_ptr = 0,                              // Updated by hoshi at runtime. read-only!
    .option_desc = &ModSettings,                // Link to the settings menu
    .OnBoot = OnBoot,
    .OnSaveInit = OnSaveInit,
    .OnSaveLoaded = OnSaveLoaded,
    .OnMainMenuLoad = OnMainMenuLoad,
    .OnPlayerSelectLoad = OnPlayerSelectLoad,
    .On3DLoadStart = On3DLoadStart,
    .On3DLoadEnd = On3DLoadEnd,
    .On3DPause = On3DPause,
    .On3DUnpause = On3DUnpause,
    .On3DExit = On3DExit,
    .OnSceneChange = OnSceneChange,
    .OnFrameStart = OnFrameStart,
    .OnFrameEnd = OnFrameEnd,
    .OnTopRideLoadEnd = OnTopRideLoadEnd,
};


// Runs immediately after the mod file is loaded.
// Calls to HSD_MemAlloc in THIS function specifically will persist throughout the entire runtime of the game.
// All calls to HSD_MemAlloc from elsewhere will return an allocation that exists only within the current scene.
void OnBoot()
{
    OSReport("[Main] Running OnBoot for %s\n", mod_desc.name);

    // Persistent allocation of ap_data
    ap_data = HSD_MemAlloc(sizeof(APData));
    memset(ap_data, 0, sizeof(APData));
    OSReport("[Main] APData at 0x%08x (%d bytes)\n", (uint)ap_data, sizeof(APData));

    // Place pointer to this allocation at a static address so the Python client can find it
    APData **static_ptr = (APData **)0x805d52d4;
    (*static_ptr) = ap_data;

    // Give the shared hoshi memory-card file an Archipelago icon (otherwise its tile is blank)
    Hoshi_SetSaveIcon("KARchipelago", "Save Data", ap_save_icon, 1, CARD_STAT_SPEED_MIDDLE);

    // Replace ClearChecker_CheckUnlocked with AP bitfield hook
    ChecklistRewards_OnBoot();

    // Replace ClearChecker_SetNewUnlock with the check-detection wrapper.
    // Must run after ChecklistRewards_OnBoot since they touch related code.
    CheckDetection_OnBoot();

    // Patches for stadium unlocks
    GateStadiums_OnBoot();

    // Patches for patch cap
    PatchCap_OnBoot();

    // Patches for deathlink
    DeathLink_OnBoot();

    // Patches for city trial event gating
    GateEvents_OnBoot();

    // Patches for copy ability gating
    GateAbilities_OnBoot();

    // Item category gating (all-ups, food, stat items, legendary pieces, etc.)
    GateItems_OnBoot();

    // Patches for box type gating
    GateBoxes_OnBoot();

    // Patches for machine spawn gating
    GateMachines_OnBoot();

    // Patches for Air Ride stage gating
    GateAirRideStages_OnBoot();

    // Patches for Top Ride stage gating
    GateTopRideStages_OnBoot();

    // Patches for Top Ride item gating
    GateTopRideItems_OnBoot();

    // Patches for fake item effects outside fake items event
    FakePatches_OnBoot();

    // Patches for Kirby color gating
    GateColors_OnBoot();

    // Traplink send hooks
    TrapLink_OnBoot();

    // Item spawn rate scaling hooks (City Trial + Top Ride)
    SpawnRate_OnBoot();

    // Item spawn table filtering hooks (covers all gate categories)
    ItemSpawnFilter_OnBoot();

    // Replace main-menu demo rider/machine (Kirby on Warp Star -> Dedede on Wheelie)
    MainMenu_OnBoot();

    // Publish the public API so external mods can import it.
    ArchipelagoAPI_Export();
}

// Runs on boot when hoshi creates save data for the mod.
// Initialize default save file values here.
void OnSaveInit()
{
    ap_save = (APSave *)mod_desc.save_ptr;
    OSReport("[Main] save data for %s created!\n", mod_desc.name);
    memset(ap_save, 0, sizeof(*ap_save));

    ChecklistRewards_OnSaveInit();
}

// Runs on startup after any save data is loaded into memory.
// This callback is executed regardless of if a memory card is inserted or contained existing save data.
void OnSaveLoaded()
{
    // Resolve the textbox API (separate mod). Deferred to OnSaveLoaded because
    // mods are booted in alphabetical order; "archipelago" boots before
    // "textbox", so Hoshi_ImportMod would return NULL during our own OnBoot.
    // By OnSaveLoaded all mods have exported their APIs.
    if (!tb_api)
    {
        tb_api = (const TextBoxAPI *)Hoshi_ImportMod(
            (char *)TEXTBOX_MOD_NAME, TEXTBOX_API_MAJOR, TEXTBOX_API_MINOR);
        if (!tb_api)
            OSReport("[Main] failed to import textbox API\n");
    }

    ap_save = (APSave *)mod_desc.save_ptr;
    ap_save->boot_num++;

    OSReport("[Main] Boot #%d, %d items received, options %s\n",
             ap_save->boot_num, ap_save->item_received_count,
             ap_save->options_received ? "loaded" : "pending");

    // Sync received count to ap_data so the AP client can read it
    ap_data->item_received_index = ap_save->item_received_count;

    // Restore reward tables and received checklist rewards from save
    ChecklistRewards_OnSaveLoaded();

    // Mirror sent_checks/goal_complete into shared memory and run initial
    // goal evaluation.
    CheckDetection_OnSaveLoaded();

    // Register the AP checklist tab with the custom_checklist framework. Deferred
    // to OnSaveLoaded (not OnBoot) because the framework mod boots after us, so its
    // API only resolves once every mod has exported.
    APChecklist_Register();

    // Publish restored link-toggle state. Hoshi's Mod_CopyFromSave has run
    // by now, so ap_menu_settings reflects the player's persisted choices.
    SyncLinkMenuStateToAPData();

    // Signal to the AP client that the mod is fully initialized
    ap_data->game_ready = 1;
    OSReport("[Main] game_ready set - waiting for AP client connection\n");
}

// For any category whose slot option marks gating as disabled, pre-fill the
// corresponding unlock mask with all-1s. The AP world ships no unlock items
// for ungated categories, so the mod has to deliver "everything unlocked"
// itself. Per-bit reads in gate_*.c are unchanged. Skips the textbox/log
// path in GateX_UnlockY to avoid a connect-time popup flood.
static void APOptions_ApplyUngatedCategories(void)
{
    const APSlotOptions *opts = &ap_save->options;
    if (!opts->machine_gating_enabled)       Unlock_SetMask(AP_UNLOCK_MACHINE,       (1u << VCKIND_NUM) - 1);
    if (!opts->ability_gating_enabled)       Unlock_SetMask(AP_UNLOCK_ABILITY,       (1u << COPYKIND_NUM) - 1);
    if (!opts->event_gating_enabled)         Unlock_SetMask(AP_UNLOCK_EVENT,         (1u << EVKIND_NUM) - 1);
    if (!opts->patch_gating_enabled)         Unlock_SetMask(AP_UNLOCK_PATCH,         (1u << PATCHKIND_NUM) - 1);
    if (!opts->item_gating_enabled)          Unlock_SetMask(AP_UNLOCK_ITEM,          (1u << ITUNLOCK_NUM) - 1);
    if (!opts->box_gating_enabled)           Unlock_SetMask(AP_UNLOCK_BOX,           (1u << BOXKIND_NUM) - 1);
    if (!opts->airride_stage_gating_enabled) Unlock_SetMask(AP_UNLOCK_AIRRIDE_STAGE, (1u << AIRRIDE_NUM) - 1);
    if (!opts->topride_stage_gating_enabled) Unlock_SetMask(AP_UNLOCK_TOPRIDE_STAGE, (1u << TOPRIDE_NUM) - 1);
    if (!opts->topride_item_gating_enabled)  Unlock_SetMask(AP_UNLOCK_TOPRIDE_ITEM,  (1u << TRITEM_NUM) - 1);
    if (!opts->color_gating_enabled)         Unlock_SetMask(AP_UNLOCK_COLOR,         (1u << KIRBYCOLOR_NUM) - 1);
    if (!opts->stadium_gating_enabled)       Unlock_SetMask(AP_UNLOCK_STADIUM,       (1u << STKIND_NUM) - 1);

    // The three Top Ride "New Item" types (Chickie/Who? Paint/Lantern, enabled-
    // mask bits 20/18/15) need a second nudge. Unlike every other category, the
    // mask-all above can't enable them: TopRideItem_MgrInit only enables those
    // slots when GameData.topride_extra_unlocks is set, and that is fed by
    // TopRide_OnCourseSelect / TopRide_PreGameThink calling ClearChecker_CheckUnlocked
    // (mod-replaced to read received_checklist_rewards) for TR reward indices
    // 8/9/10 - and GateTopRideItems_ApplyMask only ANDs (clears, never sets).
    // So mark those three rewards received here, exactly as
    // ChecklistRewards_GrantAllCosmetic does for ungated cosmetics, so
    // CheckUnlocked returns true and the engine enables the types at course init.
    // Setting only the received bit drives the enable without touching is_unlocked
    // (no spurious outbound check) and without any clear[] write (the old code set
    // has_reward via GetClearKindFromRewardIndex, which for these unplaced rewards
    // resolves to the clear_kind=0 sentinel and wrongly badged that cell).
    if (!opts->topride_item_gating_enabled)
        for (u8 ri = 8; ri <= 10; ri++)
            ap_save->received_checklist_rewards[GMMODE_TOPRIDE] |= (1ULL << ri);

    // Non-progression checklist rewards: unlock the whole cosmetic set at connect when ungated. Not a
    // mask category - these unlock via the received_checklist_rewards bitfield (see checklist_rewards.c).
    if (!opts->checklist_rewards_gating_enabled)
        ChecklistRewards_GrantAllCosmetic();

    OSReport("[Main] Gating - machines:%d abilities:%d events:%d patches:%d items:%d boxes:%d AR-stages:%d TR-stages:%d TR-items:%d colors:%d stadiums:%d\n",
             opts->machine_gating_enabled, opts->ability_gating_enabled,
             opts->event_gating_enabled, opts->patch_gating_enabled,
             opts->item_gating_enabled, opts->box_gating_enabled,
             opts->airride_stage_gating_enabled, opts->topride_stage_gating_enabled,
             opts->topride_item_gating_enabled, opts->color_gating_enabled,
             opts->stadium_gating_enabled);
}

// Check if the AP client has written slot options to APData.
// On first detection, copy them to save data (one-time transfer).
// Options are immutable per AP slot, so this only runs once per save file.
static void APOptions_TransferToSave()
{
    // Already received options for this slot
    if (ap_save->options_received)
        return;
    // Client hasn't written options yet
    if (!ap_data->options_valid)
        return;

    // One-time transfer: copy options to save data
    OSReport("[Main] AP client connected - transferring slot options to save data\n");
    memcpy(&ap_save->options, &ap_data->options, sizeof(APSlotOptions));
    ap_save->options_received = 1;

    // Set initial menu toggle values from AP slot options
    ap_menu_settings.deathlink_enabled = ap_save->options.death_link_enabled;
    ap_menu_settings.energylink_enabled = ap_save->options.energy_link_enabled;
    ap_menu_settings.traplink_enabled = ap_save->options.trap_link_enabled;
    SyncLinkMenuStateToAPData();
    OSReport("[Main] Menu toggles set - DeathLink: %d, EnergyLink: %d, TrapLink: %d\n",
             ap_save->options.death_link_enabled, ap_save->options.energy_link_enabled, ap_save->options.trap_link_enabled);
    OSReport("[Main] Goals - AirRide: %d, TopRide: %d, CityTrial: %d\n",
             ap_save->options.goal[GMMODE_AIRRIDE],
             ap_save->options.goal[GMMODE_TOPRIDE],
             ap_save->options.goal[GMMODE_CITYTRIAL]);
    OSReport("[Main] CityTrial - PatchCap min: %d, max: %d\n",
             ap_save->options.city_trial_patch_cap_min,
             ap_save->options.city_trial_patch_cap_max);
    OSReport("[Main] RevealChecklists: %d\n", ap_save->options.reveal_checklists);

    if (ap_save->options.reveal_checklists)
        RevealAllChecklists();

    APOptions_ApplyUngatedCategories();

    Hoshi_WriteSave();
    OSReport("[Main] AP slot options saved to memory card\n");
}

// Runs when entering the main menu.
void OnMainMenuLoad()
{
    OSReport("[Main] Entering the main menu.\n");
}

// Runs when entering the player select menu (Air Ride or City Trial).
void OnPlayerSelectLoad()
{
    OSReport("[Main] Entering player select (minor %d).\n", Scene_GetCurrentMinor());

    // City Trial colors persist from prior sessions (no init block to hook
    // like AR/TR), so we validate here on every CSS load. Machine filtering
    // is handled inside CitySelect_CreateMachineIcons / CitySelect_InitPlayerMachines
    // via the gate_machines hooks - no extra pass needed here.
    if (Scene_GetCurrentMinor() == MNRKIND_CITYPLYSELECT)
        GateColors_ValidateCityTrialColors();
}

// Runs before the game is initialized.
void On3DLoadStart()
{

}

// Runs upon entering a 3D game (Air Ride, Top Ride, or City Trial).
// Players, riders, their machines, and the map have all been instantiated.
void On3DLoadEnd()
{
    static const char *const ar_mode_names[] = { "Race", "Time Attack", "Free Run" };
    static const char *const city_mode_names[] = { "Trial", "Stadium", "Free Run" };
    // Gm_IsInCity() only returns true on the CT main map (stage_kind 9/52);
    // stadiums load their own stages and would be misreported as Air Ride.
    // The CT major (MJRKIND_CITY) covers Trial, Free Run, and all stadiums.
    if (Scene_GetCurrentMajor() == MJRKIND_CITY)
    {
        CityMode cm = Gm_GetCityMode();
        if (cm == CITYMODE_STADIUM)
        {
            StadiumKind sk = Gm_GetCurrentStadiumKind();
            const char *sk_name = ((unsigned)sk < STKIND_NUM) ? StadiumKind_Names[sk] : "?";
            OSReport("[Main] Starting City Trial: Stadium (%s) Ground=%d Stage=%d CityMode=%d Stadium=%d(%d) Damage=%d ItemData=%d\n",
                     sk_name, Gr_GetCurrentGrKind(), Gm_GetCurrentStageKind(),
                     Gm_GetCityMode(), Gm_GetCurrentStadiumKind(),
                     Gm_GetCurrentStadiumGroup(), Gm_IsDamageEnabled(), Item_CheckIsLoaded());
        }
        else
        {
            const char *cm_name = ((unsigned)cm < 3) ? city_mode_names[cm] : "?";
            OSReport("[Main] Starting City Trial: %s Ground=%d Stage=%d CityMode=%d Stadium=%d(%d) Damage=%d ItemData=%d\n",
                     cm_name, Gr_GetCurrentGrKind(), Gm_GetCurrentStageKind(),
                     Gm_GetCityMode(), Gm_GetCurrentStadiumKind(),
                     Gm_GetCurrentStadiumGroup(), Gm_IsDamageEnabled(), Item_CheckIsLoaded());
        }
    }
    else
    {
        AirRideMode ar_mode = Gm_GetAirRideMode();
        const char *ar_mode_name = ((unsigned)ar_mode < 3) ? ar_mode_names[ar_mode] : "?";
        OSReport("[Main] Starting Air Ride: %s Ground=%d Stage=%d CityMode=%d Stadium=%d(%d) Damage=%d ItemData=%d\n",
                 ar_mode_name, Gr_GetCurrentGrKind(), Gm_GetCurrentStageKind(),
                 Gm_GetCityMode(), Gm_GetCurrentStadiumKind(),
                 Gm_GetCurrentStadiumGroup(), Gm_IsDamageEnabled(), Item_CheckIsLoaded());
    }

    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) == PKIND_NONE)
            continue;

        GOBJ *rg = Ply_GetRiderGObj(i);
        RiderData *rd = rg->userdata;
        MachineKind machine_kind = rd->starting_machine_idx;
        OSReport("[Main] Player %d using rider [%d] color [%d] riding machine [%d].\n",
                 i + 1, rd->kind, rd->color_idx, machine_kind);
    }

    // Reset the per-scene legendary-assembly one-shot guard (piece archives are
    // preloaded fresh on each scene load).
    GateMachines_On3DLoadEnd();

    // Enemy spawn filtering for ability gating
    GateAbilities_On3DLoadEnd();

    // Item spawn table filtering for non-CT modes (stadium, Air Ride)
    ItemSpawnFilter_On3DLoadEnd();

    PermanentPatch_On3DLoadEnd();

    if (ap_menu_settings.deathlink_enabled)
        DeathLink_On3DLoadEnd();

    if (ap_menu_settings.energylink_enabled)
        EnergyLink_On3DLoadEnd();

    if (ap_menu_settings.traplink_enabled)
        TrapLink_On3DLoadEnd();

    GoalMaxStatsCT_On3DLoadEnd();

    // Big / Small Kirby model scaling (always available - not an optional link).
    KirbyScale_On3DLoadEnd();
}

// Runs after Top Ride gameplay is fully initialized.
// Top Ride uses minor 19 (not 18), so On3DLoadEnd does not fire.
void OnTopRideLoadEnd()
{
    static const char *const tr_mode_names[] = { "Race", "Time Attack", "Free Run" };
    TopRideMode tr_mode = TopRide_GetMode();
    const char *tr_mode_name = ((unsigned)tr_mode < 3) ? tr_mode_names[tr_mode] : "?";
    OSReport("[Main] Top Ride gameplay loaded (mode: %s).\n", tr_mode_name);

    if (ap_menu_settings.energylink_enabled)
        EnergyLink_OnTopRideLoadEnd();

    if (ap_menu_settings.traplink_enabled)
        TrapLink_OnTopRideLoadEnd();

    if (ap_menu_settings.deathlink_enabled)
        DeathLink_OnTopRideLoadEnd();

    // Big / Small Kirby model scaling (always available - not an optional link).
    KirbyScale_OnTopRideLoadEnd();
}

// Runs when pausing the match. The index of the pausing player is passed in as an argument.
void On3DPause(int pause_ply)
{
    OSReport("[Main] Pausing 3D (player %d).\n", pause_ply);
}

// Runs when unpausing the match.
void On3DUnpause(int pause_ply)
{
    OSReport("[Main] Unpausing 3D (player %d).\n", pause_ply);
}

// Runs when exiting a match.
void On3DExit()
{
    OSReport("[Main] Exiting 3D.\n");
}

// Runs every scene change.
// The memory heap is destroyed and recreated every scene change, meaning HSD objects
// such as CObj's (camera) and JObj's (models) will not persist across them.
// This hook can be used to recreate processes/objects that should always be running.
void OnSceneChange()
{
    OSReport("[Main] We are now entering major %d / minor %d\n",
             Scene_GetCurrentMajor(), Scene_GetCurrentMinor());

    APItems_OnSceneChange();

    // Drop Kirby model scaling back to neutral - each scene recreates the Kirby
    // objects at model_scale 1.0, so Big / Small Kirby lasts only until here.
    KirbyScale_OnSceneChange();
}

void OnFrameStart()
{
    // Poll for AP client connection (one-time options transfer)
    APOptions_TransferToSave();

    // Apply the AP location assignment when the client signals new data.
    // ChecklistRewards_ApplyLocations clears location_data_valid and persists,
    // so this fires once per client write (every (re)connection).
    if (ap_data->location_data_valid)
        ChecklistRewards_ApplyLocations();

    // Process client backfill writes and poll meta auto-unlocks.
    CheckDetection_OnFrameStart();

    // The AP checklist tab's custom-check evaluation and blue-theme recolor are
    // driven by the custom_checklist framework mod (OnFrameStart / OnFrameEnd).
}

// Runs every game tick after the frame has been processed.
void OnFrameEnd()
{
}

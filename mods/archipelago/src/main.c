#include <string.h>

#include "os.h"
#include "game.h"
#include "scene.h"
#include "hoshi/mod.h"
#include "hoshi/func.h"
#include "stage.h"
#include "stadium.h"

#include "main.h"
#include "gate_machines.h"
#include "deathlink.h"
#include "city_trial_event.h"
#include "ap_item_handler.h"
#include "kirby_scale.h"
#include "drop_ability.h"
#include "energylink.h"
#include "traplink.h"
#include "fake_patches.h"
#include "patch_item.h"
#include "checklist_rewards.h"
#include "check_detection.h"
#include "ap_checklist.h"
#include "ap_check_detect.h"
#include "gate_stadiums.h"
#include "patch_cap.h"
#include "gate_events.h"
#include "gate_abilities.h"
#include "gate_base_abilities.h"
#include "air_quick_spin.h"
#include "onfoot_zoom.h"
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

APData *ap_data;
APSave *ap_save;
const TextBoxAPI *tb_api = 0;
const CustomMachinesAPI *cm_api = 0;

// The AP client hardcodes an offset for every APData field and reads them by
// address, so a silent layout shift desyncs it with no error anywhere. These pin
// the block boundaries and both edges of every per-checklist-mode array; they and
// the client's offset table move together, never one alone.
_Static_assert(offsetof(APData, options) == 0x030, "APSlotOptions block moved");
_Static_assert(offsetof(APData, options.reveal_checklists) == 0x03C, "OPTION_REVEAL_CHECKLIST_AIRRIDE");
_Static_assert(offsetof(APData, options.reveal_checklists[AP_CHECKLIST_ROW]) == 0x048, "OPTION_REVEAL_CHECKLIST_ARCHIPELAGO");
_Static_assert(offsetof(APData, options.goal) == 0x04C, "OPTION_GOAL_AIRRIDE");
_Static_assert(offsetof(APData, options.goal[AP_CHECKLIST_ROW]) == 0x058, "OPTION_GOAL_ARCHIPELAGO");
_Static_assert(offsetof(APData, options.checklist_amount) == 0x05C, "OPTION_CHECKLIST_AMOUNT_AIRRIDE");
_Static_assert(offsetof(APData, options.goal_checks) == 0x078, "OPTION_GOAL_CHECKS_AIRRIDE");
_Static_assert(offsetof(APData, options.goal_checks[AP_CHECKLIST_ROW]) == 0x0A8, "OPTION_GOAL_CHECKS_ARCHIPELAGO");
_Static_assert(offsetof(APData, options.machine_gating_enabled) == 0x0B8, "gating block moved");
_Static_assert(offsetof(APData, location_data_valid) == 0x0F0, "LOCATION_DATA_VALID");
_Static_assert(offsetof(APData, locations) == 0x0F4, "LOCATIONS_AIRRIDE");
_Static_assert(offsetof(APData, sent_checks) == 0x208, "SENT_CHECKS_AIRRIDE");
_Static_assert(offsetof(APData, sent_checks[AP_CHECKLIST_ROW]) == 0x238, "SENT_CHECKS_ARCHIPELAGO");
_Static_assert(offsetof(APData, client_backfill) == 0x248, "CLIENT_BACKFILL_AIRRIDE");
_Static_assert(offsetof(APData, client_backfill[AP_CHECKLIST_ROW]) == 0x278, "CLIENT_BACKFILL_ARCHIPELAGO");
_Static_assert(offsetof(APData, goal_complete) == 0x288, "GOAL_COMPLETE");
_Static_assert(offsetof(APData, deathlink_menu_enabled) == 0x28C, "DEATHLINK_MENU_ENABLED");

int ap_checklist_mode = GMMODE_NUM;

ModDesc mod_desc = {
    .name = "KARchipelago",
    .author = "DeDeDK",
    .version.major = 1,
    .version.minor = 0,
    .save_size = sizeof(struct APSave),
    .save_ptr = 0,                              // Updated by hoshi at runtime, read-only
    .option_desc = &ModSettings,
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


// Runs immediately after the mod file is loaded. HSD_MemAlloc calls made here
// persist for the whole runtime; anywhere else they last only the current scene.
void OnBoot()
{
    OSReport("[Main] Running OnBoot for %s\n", mod_desc.name);

    ap_data = HSD_MemAlloc(sizeof(APData));
    memset(ap_data, 0, sizeof(APData));
    OSReport("[Main] APData at 0x%08x (%d bytes)\n", (uint)ap_data, sizeof(APData));

    // Static address the Python client polls to find the struct.
    APData **static_ptr = (APData **)0x805d52d4;
    (*static_ptr) = ap_data;

    // Give the shared hoshi memory-card file an Archipelago tile. The art is loaded
    // from disc when the save is created, so no image is baked into this mod. Banner
    // after icon: the icon call clears the tile.
    Hoshi_SetSaveIconFile("KARchipelago", "Save Data", "ApIcon", 1, CARD_STAT_SPEED_MIDDLE);
    Hoshi_SetSaveBannerFile("ApBanner");

    ChecklistRewards_OnBoot();

    // After ChecklistRewards_OnBoot - they patch related code.
    CheckDetection_OnBoot();

    APCheckDetect_OnBoot();
    GateStadiums_OnBoot();
    PatchCap_OnBoot();
    DeathLink_OnBoot();
    GateEvents_OnBoot();
    GateAbilities_OnBoot();
    GateBaseAbilities_OnBoot();
    AirQuickSpin_OnBoot();
    OnFootZoom_OnBoot();
    GateItems_OnBoot();
    GateBoxes_OnBoot();
    GateMachines_OnBoot();
    GateAirRideStages_OnBoot();
    GateTopRideStages_OnBoot();
    GateTopRideItems_OnBoot();
    FakePatches_OnBoot();
    GateColors_OnBoot();
    TrapLink_OnBoot();
    SpawnRate_OnBoot();
    ItemSpawnFilter_OnBoot();
    MainMenu_OnBoot();

    ArchipelagoAPI_Export();
}

// Runs on boot when hoshi creates save data for the mod.
void OnSaveInit()
{
    ap_save = (APSave *)mod_desc.save_ptr;
    OSReport("[Main] save data for %s created!\n", mod_desc.name);
    memset(ap_save, 0, sizeof(*ap_save));

    ChecklistRewards_OnSaveInit();
}

// Open the checklists whose slot option asks for it, one row at a time.
static void APOptions_ApplyRevealChecklists(void)
{
    for (int row = 0; row < CHECKLIST_MODE_NUM; row++)
        if (ap_save->options.reveal_checklists[row])
            RevealChecklist(row);
}

// Optional: absent when custom_machines is not built, leaving the roster at the vanilla
// 26 machines and 20 characters. Deferred past OnBoot because mods boot alphabetically
// and the registry boots after us, and re-tried per call because the title screen asks
// for it before the first save load.
void AP_ResolveCustomMachines(void)
{
    if (cm_api)
        return;

    cm_api = (const CustomMachinesAPI *)Hoshi_ImportMod(
        (char *)CUSTOM_MACHINES_MOD_NAME, CUSTOM_MACHINES_API_MAJOR, CUSTOM_MACHINES_API_MINOR);
    if (cm_api)
    {
        // The registry owns both select screens' packing; this is what makes it
        // offer the unlocked roster rather than the engine's own.
        cm_api->SetAvailabilityFilter(GateMachines_FilterSelectCharacter);
        OSReport("[Main] custom_machines: %d machine(s), %d kinds, %d characters\n",
                 cm_api->GetCount(), cm_api->GetKindCeiling(),
                 cm_api->GetCharacterKindCeiling());
    }
}

// Runs on startup after any save data is loaded, whether or not a memory card is
// inserted or held existing save data.
void OnSaveLoaded()
{
    // Deferred here because mods boot alphabetically and textbox boots after us,
    // so Hoshi_ImportMod would return NULL during our own OnBoot.
    if (!tb_api)
    {
        tb_api = (const TextBoxAPI *)Hoshi_ImportMod(
            (char *)TEXTBOX_MOD_NAME, TEXTBOX_API_MAJOR, TEXTBOX_API_MINOR);
        if (!tb_api)
            OSReport("[Main] failed to import textbox API\n");
    }

    AP_ResolveCustomMachines();

    ap_save = (APSave *)mod_desc.save_ptr;
    ap_save->boot_num++;

    OSReport("[Main] Boot #%d, %d items received, options %s\n",
             ap_save->boot_num, ap_save->item_received_count,
             ap_save->options_received ? "loaded" : "pending");

    ap_data->item_received_index = ap_save->item_received_count;

    ChecklistRewards_OnSaveLoaded();

    // Mirrors sent_checks/goal_complete into shared memory and runs the initial
    // goal evaluation.
    CheckDetection_OnSaveLoaded();

    // Also deferred past OnBoot: the custom_checklist framework mod boots after
    // us, so its API only resolves once every mod has exported.
    APChecklist_Register();

    // Re-applied every boot, not just at option transfer: the vanilla modes' reveal
    // rides along in the game's own clear data, but the AP tab's cells live in RAM
    // and come up blank.
    if (ap_save->options_received)
        APOptions_ApplyRevealChecklists();

    // Hoshi's Mod_CopyFromSave has run by now, so ap_menu_settings reflects the
    // player's persisted toggle choices.
    SyncLinkMenuStateToAPData();

    ap_data->game_ready = 1;
    OSReport("[Main] game_ready set - waiting for AP client connection\n");
}

// For any category whose slot option marks gating as disabled, pre-fill the
// unlock mask with all-1s: the AP world ships no unlock items for ungated
// categories. Bypasses the GateX_UnlockY textbox/log path so connecting doesn't
// flood the screen with popups.
static void APOptions_ApplyUngatedCategories(void)
{
    const APSlotOptions *opts = &ap_save->options;
    if (!opts->machine_gating_enabled)       Unlock_SetMask(AP_UNLOCK_MACHINE,       (1u << MachineKind_Num()) - 1);
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
    if (!opts->base_ability_gating_enabled)  Unlock_SetMask(AP_UNLOCK_BASE_ABILITY,  (1u << BASEABILITY_NUM) - 1);

    // The three TR "New Item" types (Chickie/Who? Paint/Lantern) aren't reachable
    // via the mask - the engine enables them only when their checklist reward is
    // received. Mark TR reward indices 8-10 received so an ungated world gets them.
    if (!opts->topride_item_gating_enabled)
        for (u8 ri = 8; ri <= 10; ri++)
            ap_save->received_checklist_rewards[GMMODE_TOPRIDE] |= (1ULL << ri);

    // Cosmetic rewards are tracked by received_checklist_rewards, not a mask.
    if (!opts->checklist_rewards_gating_enabled)
        ChecklistRewards_GrantAllCosmetic();

    OSReport("[Main] Gating - machines:%d abilities:%d events:%d patches:%d items:%d boxes:%d AR-stages:%d TR-stages:%d TR-items:%d colors:%d stadiums:%d\n",
             opts->machine_gating_enabled, opts->ability_gating_enabled,
             opts->event_gating_enabled, opts->patch_gating_enabled,
             opts->item_gating_enabled, opts->box_gating_enabled,
             opts->airride_stage_gating_enabled, opts->topride_stage_gating_enabled,
             opts->topride_item_gating_enabled, opts->color_gating_enabled,
             opts->stadium_gating_enabled);
    OSReport("[Main] Gating - base abilities:%d\n", opts->base_ability_gating_enabled);
}

// Copy the client's slot options into save data on first detection. Options are
// immutable per AP slot, so this runs once per save file.
static void APOptions_TransferToSave()
{
    if (ap_save->options_received)
        return;
    if (!ap_data->options_valid)
        return;

    OSReport("[Main] AP client connected - transferring slot options to save data\n");
    memcpy(&ap_save->options, &ap_data->options, sizeof(APSlotOptions));
    ap_save->options_received = 1;

    ap_menu_settings.deathlink_enabled = ap_save->options.death_link_enabled;
    ap_menu_settings.energylink_enabled = ap_save->options.energy_link_enabled;
    ap_menu_settings.traplink_enabled = ap_save->options.trap_link_enabled;
    SyncLinkMenuStateToAPData();
    OSReport("[Main] Menu toggles set - DeathLink: %d, EnergyLink: %d, TrapLink: %d\n",
             ap_save->options.death_link_enabled, ap_save->options.energy_link_enabled, ap_save->options.trap_link_enabled);
    OSReport("[Main] Goals - AirRide: %d, TopRide: %d, CityTrial: %d, Archipelago: %d\n",
             ap_save->options.goal[GMMODE_AIRRIDE],
             ap_save->options.goal[GMMODE_TOPRIDE],
             ap_save->options.goal[GMMODE_CITYTRIAL],
             ap_save->options.goal[AP_CHECKLIST_ROW]);
    OSReport("[Main] CityTrial - PatchCap min: %d, max: %d\n",
             ap_save->options.city_trial_patch_cap_min,
             ap_save->options.city_trial_patch_cap_max);
    OSReport("[Main] RevealChecklists - AirRide: %d, TopRide: %d, CityTrial: %d, Archipelago: %d\n",
             ap_save->options.reveal_checklists[GMMODE_AIRRIDE],
             ap_save->options.reveal_checklists[GMMODE_TOPRIDE],
             ap_save->options.reveal_checklists[GMMODE_CITYTRIAL],
             ap_save->options.reveal_checklists[AP_CHECKLIST_ROW]);

    APOptions_ApplyRevealChecklists();

    APOptions_ApplyUngatedCategories();

    Hoshi_WriteSave();
    OSReport("[Main] AP slot options saved to memory card\n");
}

void OnMainMenuLoad()
{
    OSReport("[Main] Entering the main menu.\n");
}

// Runs when entering the player select menu (Air Ride or City Trial).
void OnPlayerSelectLoad()
{
    OSReport("[Main] Entering player select (minor %d).\n", Scene_GetCurrentMinor());

    // City Trial colors persist from prior sessions and have no init block to
    // hook like AR/TR, so validate on every CSS load.
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

    GateMachines_On3DLoadEnd();
    GateAbilities_On3DLoadEnd();
    ItemSpawnFilter_On3DLoadEnd();
    PermanentPatch_On3DLoadEnd();

    if (ap_menu_settings.deathlink_enabled)
        DeathLink_On3DLoadEnd();

    if (ap_menu_settings.energylink_enabled)
        EnergyLink_On3DLoadEnd();

    if (ap_menu_settings.traplink_enabled)
        TrapLink_On3DLoadEnd();

    GoalMaxStatsCT_On3DLoadEnd();
    APCheckDetect_On3DLoadEnd();
    KirbyScale_On3DLoadEnd();
    DropAbility_On3DLoadEnd();
}

// Top Ride uses minor 19 (not 18), so On3DLoadEnd does not fire for it.
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

    KirbyScale_OnTopRideLoadEnd();
    DropAbility_OnTopRideLoadEnd();
}

void On3DPause(int pause_ply)
{
    OSReport("[Main] Pausing 3D (player %d).\n", pause_ply);
}

void On3DUnpause(int pause_ply)
{
    OSReport("[Main] Unpausing 3D (player %d).\n", pause_ply);
}

void On3DExit()
{
    OSReport("[Main] Exiting 3D.\n");

    // Stadium_ExitMinor has finished latching GameData.stadium_results by this
    // point, so the round's placements and times are final and readable here.
    APCheckDetect_On3DExit();
}

// The memory heap is destroyed and recreated every scene change, so HSD objects
// (CObjs, JObjs) do not persist across it. Recreate always-running procs here.
void OnSceneChange()
{
    OSReport("[Main] We are now entering major %d / minor %d\n",
             Scene_GetCurrentMajor(), Scene_GetCurrentMinor());

    APItems_OnSceneChange();
    KirbyScale_OnSceneChange();
}

void OnFrameStart()
{
    APOptions_TransferToSave();

    // ChecklistRewards_ApplyLocations clears location_data_valid and persists, so
    // this fires once per client write, i.e. on every (re)connection.
    if (ap_data->location_data_valid)
        ChecklistRewards_ApplyLocations();

    CheckDetection_OnFrameStart();
}

void OnFrameEnd()
{
}

#include "os.h"
#include "game.h"
#include "scene.h"
#include "inline.h"
#include "code_patch/code_patch.h"
#include "hoshi/settings.h"
#include "hoshi/mod.h"
#include "hoshi/func.h"
#include "stage.h"
#include "stadium.h"

#include "main.h"
#include "textbox.h"
#include "deathlink.h"
#include "city_trial_event.h"
#include "ap_item_handler.h"
#include "energylink.h"
#include "traplink.h"
#include "spawn_item.h"
#include "patch_item.h"
#include "checklist_rewards.h"
#include "check_detection.h"
#include "stadium_lock.h"
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
#include "spawn_enemy.h"
#include "item_spawn_filter.h"
#include "custom_events_api.h"
#include "energylink_spend.h"
#include "debug_menu.h"

// Define global variables
APData *ap_data;
APMenuSettings ap_menu_settings;
APSave *ap_save;
CustomEventsAPI *custom_events;

// Menu toggle callbacks
static const char *stc_off_on[] = {"Off", "On"};

static void OnToggleDeathLink(int val)      { OSReport("[Main] DeathLink toggled %s\n", stc_off_on[val]); }
static void OnToggleEnergyLink(int val)     { OSReport("[Main] EnergyLink toggled %s\n", stc_off_on[val]); }
static void OnToggleAutoCharge(int val)     { OSReport("[Main] EnergyLink AutoCharge toggled %s\n", stc_off_on[val]); }
static void OnToggleTrapLink(int val)       { OSReport("[Main] TrapLink toggled %s\n", stc_off_on[val]); }
static void OnToggleTextBox(int val)        { OSReport("[Main] AP Message Textbox toggled %s\n", stc_off_on[val]); }
static void OnToggleCTPermanent(int val)    { OSReport("[Main] CT Permanent Patches toggled %s\n", stc_off_on[val]); }
static void OnToggleARPermanent(int val)    { OSReport("[Main] AR Permanent Patches toggled %s\n", stc_off_on[val]); }

// Submenu: controls whether accumulated permanent stat patches are re-applied
// at the start of each round/race. Receiving AP permanent-patch items still
// increments save counters unconditionally — the toggles only gate round-start
// application. Default: both On, matching the historical behavior before the
// toggles existed.
static MenuDesc permanent_patches_menu = {
    .option_num = 2,
    .options = {
        &(OptionDesc){
            .name = "City Trial",
            .description = "Apply accumulated permanent patches at the start of each City Trial round",
            .kind = OPTKIND_VALUE,
            .val = &ap_menu_settings.ct_permanent_patches_enabled,
            .value_num = 2,
            .value_names = (char *[]){
                "Off",
                "On",
            },
            .on_change = OnToggleCTPermanent,
        },
        &(OptionDesc){
            .name = "Air Ride",
            .description = "Apply accumulated permanent patches at the start of each Air Ride race",
            .kind = OPTKIND_VALUE,
            .val = &ap_menu_settings.ar_permanent_patches_enabled,
            .value_num = 2,
            .value_names = (char *[]){
                "Off",
                "On",
            },
            .on_change = OnToggleARPermanent,
        },
    },
};

// Creates a menu that appears in the in-game Settings menu.
// Menus may be nested by setting the OptionDesc::kind to OPTKIND_MENU
OptionDesc ModSettings = {
    .name = "Archipelago Settings",
    .description = "Interface with mod settings here.",
    .kind = OPTKIND_MENU,
    .menu_ptr = &(MenuDesc){
        .option_num = 6,
        .options = {
            &(OptionDesc){
                .name = "Death Link",
                .description = "Enable or Disable Death Link",
                .kind = OPTKIND_VALUE,
                .val = &ap_menu_settings.deathlink_enabled,
                .value_num = 2,
                .value_names = (char *[]){
                    "Off",
                    "On",
                },
                .on_change = OnToggleDeathLink,
            },
            &(OptionDesc){
                .name = "Energy Link",
                .description = "Energy Link settings and item shop.",
                .kind = OPTKIND_MENU,
                .menu_ptr = &(MenuDesc){
                    .option_num = 3,
                    .options = {
                        &(OptionDesc){
                            .name = "Enabled",
                            .description = "Enable or Disable Energy Link",
                            .kind = OPTKIND_VALUE,
                            .val = &ap_menu_settings.energylink_enabled,
                            .value_num = 2,
                            .value_names = (char *[]){
                                "Off",
                                "On",
                            },
                            .on_change = OnToggleEnergyLink,
                        },
                        &(OptionDesc){
                            .name = "Auto-Charge",
                            .description = "Automatically spend energy to fill machine charge meter",
                            .kind = OPTKIND_VALUE,
                            .val = &ap_menu_settings.energylink_autocharge,
                            .value_num = 2,
                            .value_names = (char *[]){
                                "Off",
                                "On",
                            },
                            .on_change = OnToggleAutoCharge,
                        },
                        &(OptionDesc){
                            .name = "Spend",
                            .description = "Purchase items with pooled energy.",
                            .kind = OPTKIND_MENU,
                            .menu_ptr = &energylink_spend_menu,
                        },
                    },
                },
            },
            &(OptionDesc){
                .name = "Trap Link",
                .description = "Enable or Disable Trap Link",
                .kind = OPTKIND_VALUE,
                .val = &ap_menu_settings.traplink_enabled,
                .value_num = 2,
                .value_names = (char *[]){
                    "Off",
                    "On",
                },
                .on_change = OnToggleTrapLink,
            },
            &(OptionDesc){
                .name = "AP Message Textbox",
                .description = "Enable or Disable the in-game textbox for Archipelago Messages",
                .kind = OPTKIND_VALUE,
                .val = &ap_menu_settings.textbox_enabled,
                .value_num = 2,
                .value_names = (char *[]){
                    "Off",
                    "On",
                },
                .on_change = OnToggleTextBox,
            },
            &(OptionDesc){
                .name = "Permanent Patches",
                .description = "Control whether permanent patches are re-applied at round start",
                .kind = OPTKIND_MENU,
                .menu_ptr = &permanent_patches_menu,
            },
            &(OptionDesc){
                .name = "Debug",
                .description = "Debug menu for toggling gates and giving items.",
                .kind = OPTKIND_MENU,
                .menu_ptr = &debug_menu,
            },
        },
    },
};

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
    .OnTopRideLoad = OnTopRideLoad,
};


// Runs immediately after the mod file is loaded.
// Calls to HSD_MemAlloc in THIS function specifically will persist throughout the entire runtime of the game.
// All calls to HSD_MemAlloc from elsewhere will return an allocation that exists only within the current scene.
void OnBoot()
{
    OSReport("[Main] Running OnBoot for %s\n", mod_desc.name);

    // Default menu toggle values for first-boot (no hoshi save entry yet).
    // Hoshi's Mod_CopyFromSave overwrites these if it finds a saved hash.
    // Defaults match pre-toggle behavior so existing installs keep working.
    ap_menu_settings.ct_permanent_patches_enabled = 1;
    ap_menu_settings.ar_permanent_patches_enabled = 1;

    // Persistent allocation of ap_data
    ap_data = HSD_MemAlloc(sizeof(APData));
    memset(ap_data, 0, sizeof(APData));
    OSReport("[Main] APData at 0x%08x (%d bytes)\n", (uint)ap_data, sizeof(APData));

    // Place pointer to this allocation at a static address so the Python client can find it
    APData **static_ptr = (APData **)0x805d52d4;
    (*static_ptr) = ap_data;

    // Replace ClearChecker_CheckUnlocked with AP bitfield hook
    ChecklistRewards_OnBoot();

    // Replace ClearChecker_SetNewUnlock with the check-detection wrapper.
    // Must run after ChecklistRewards_OnBoot since they touch related code.
    CheckDetection_OnBoot();

    // Patches for stadium unlocks
    StadiumLock_OnBoot();

    // Patches for patch cap
    PatchCap_OnBoot();

    // Patches for deathlink
    DeathLink_OnBoot();

    // Patches for city trial event gating
    GateEvents_OnBoot();

    // Patches for copy ability gating
    GateAbilities_OnBoot();

    // Patches for item spawn gating (legendary pieces)
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
    SpawnItem_OnBoot();

    // Patches for Kirby color gating
    GateColors_OnBoot();

    // Null-safe patches for standalone enemy spawning
    SpawnEnemy_OnBoot();

    // Traplink send hooks
    TrapLink_OnBoot();

    // Item spawn table filtering hooks (covers all gate categories)
    ItemSpawnFilter_OnBoot();
}

// Runs on boot when hoshi creates save data for the mod.
// Initialize default save file values here.
void OnSaveInit()
{
    ap_save = (APSave *)mod_desc.save_ptr;
    OSReport("[Main] save data for %s created!\n", mod_desc.name);
    memset(ap_save, 0, sizeof(*ap_save));
}

// Runs on startup after any save data is loaded into memory.
// This callback is executed regardless of if a memory card is inserted or contained existing save data.
void OnSaveLoaded()
{
    ap_save = (APSave *)mod_desc.save_ptr;
    ap_save->boot_num++;

    // Import custom events API (deferred from OnBoot — all mods are loaded by now)
    if (!custom_events)
    {
        custom_events = Hoshi_ImportMod("custom_events",
                                        CUSTOM_EVENTS_API_MAJOR,
                                        CUSTOM_EVENTS_API_MINOR);
        if (custom_events)
        {
            custom_events->SetWeightFilter(GateEvents_GetWeightFilter());
            OSReport("[Main] Custom events API imported, weight filter installed\n");
        }
        else
        {
            OSReport("[Main] Custom events mod not found, custom events disabled\n");
        }
    }
    OSReport("[Main] Boot #%d, %d items received, options %s\n",
             ap_save->boot_num, ap_save->item_received_count,
             ap_save->options_received ? "loaded" : "pending");

    // Sync received count to ap_data so the AP client can read it
    ap_data->item_received_index = ap_save->item_received_count;

    // Apply hoshi-saved debug menu toggle states to save data masks.
    // Must happen before anything that reads gate masks (e.g. StadiumLock).
    DebugMenu_ApplyToSave();

    // Restore reward tables and received checklist rewards from save
    ChecklistRewards_OnSaveLoaded();

    // Mirror sent_checks/goal_complete into shared memory and run initial
    // goal evaluation.
    CheckDetection_OnSaveLoaded();

    // Signal to the AP client that the mod is fully initialized
    ap_data->game_ready = 1;
    OSReport("[Main] game_ready set — waiting for AP client connection\n");
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
    OSReport("[Main] AP client connected — transferring slot options to save data\n");
    memcpy(&ap_save->options, &ap_data->options, sizeof(APSlotOptions));
    ap_save->options_received = 1;

    // Set initial menu toggle values from AP slot options
    ap_menu_settings.deathlink_enabled = ap_save->options.death_link_enabled;
    ap_menu_settings.energylink_enabled = ap_save->options.energy_link_enabled;
    ap_menu_settings.traplink_enabled = ap_save->options.trap_link_enabled;
    OSReport("[Main] Menu toggles set — DeathLink: %d, EnergyLink: %d, TrapLink: %d\n",
             ap_save->options.death_link_enabled, ap_save->options.energy_link_enabled, ap_save->options.trap_link_enabled);
    OSReport("[Main] Goals — AirRide: %d, TopRide: %d, CityTrial: %d\n",
             ap_save->options.goal[GMMODE_AIRRIDE],
             ap_save->options.goal[GMMODE_TOPRIDE],
             ap_save->options.goal[GMMODE_CITYTRIAL]);
    OSReport("[Main] CityTrial — ProgressivePatchCaps: %d (start: %d), ProgressiveStadiums: %d\n",
             ap_save->options.city_trial_progressive_patch_caps,
             ap_save->options.city_trial_patch_cap_amount,
             ap_save->options.city_trial_progressive_stadiums);
    OSReport("[Main] RevealChecklists: %d\n", ap_save->options.reveal_checklists);

    if (ap_save->options.reveal_checklists)
        RevealAllChecklists();

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

    // City Trial select: filter locked machines and validate colors.
    // CT colors persist from prior sessions (no init block to hook like AR/TR),
    // so we validate here on every CSS load.
    if (Scene_GetCurrentMinor() == MNRKIND_CITYPLYSELECT)
    {
        GateMachines_FilterSelectList();
        GateColors_ForceDefaultColors();
    }
}

// Runs before the game is initialized.
void On3DLoadStart()
{

}

// Runs upon entering a 3D game (Air Ride, Top Ride, or City Trial).
// Players, riders, their machines, and the map have all been instantiated.
void On3DLoadEnd()
{
    char *mode_name = Gm_IsInCity() ? "City Trial" : "Air Ride";
    OSReport("[Main] Starting %s: Ground=%d Stage=%d CityMode=%d Stadium=%d(%d) Damage=%d\n",
             mode_name, Gm_GetCurrentGrKind(), Gm_GetCurrentStageKind(),
             Gm_GetCityMode(), Gm_GetCurrentStadiumKind(),
             Gm_GetCurrentStadiumGroup(), Gm_IsDamageEnabled());

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

    if (Gm_GetCurrentGrKind() == GRKIND_CITY1)
        GateEvents_LogEnabledEvents();

    // Enemy spawn filtering for ability gating
    GateAbilities_On3DLoadEnd();

    // Item spawn table filtering for non-CT modes (stadium, Air Ride)
    ItemSpawnFilter_On3DLoadEnd();

    // Round-start permanent patch re-application, per-mode menu toggle.
    // City Trial stadium also counts as "in city" for Gm_IsInCity.
    if (Gm_IsInCity())
    {
        if (ap_menu_settings.ct_permanent_patches_enabled)
            PermanentPatch_On3DLoadEnd();
    }
    else
    {
        if (ap_menu_settings.ar_permanent_patches_enabled)
            PermanentPatch_On3DLoadEnd();
    }

    if (ap_menu_settings.deathlink_enabled)
        DeathLink_On3DLoadEnd();

    if (ap_menu_settings.energylink_enabled)
        EnergyLink_On3DLoadEnd();

    if (ap_menu_settings.traplink_enabled)
        TrapLink_On3DLoadEnd();
}

// Runs after Top Ride gameplay is fully initialized.
// Top Ride uses minor 19 (not 18), so On3DLoadEnd does not fire.
void OnTopRideLoad()
{
    OSReport("[Main] Top Ride gameplay loaded.\n");

    if (ap_menu_settings.energylink_enabled)
        EnergyLink_OnTopRideLoad();

    if (ap_menu_settings.traplink_enabled)
        TrapLink_OnTopRideLoad();
}

// Runs when pausing the match. The index of the pausing player is passed in as an argument.
void On3DPause(int pause_ply)
{
}

// Runs when unpausing the match.
void On3DUnpause(int pause_ply)
{
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

    CreateTextBox_OnSceneChange();

    APItems_OnSceneChange();
}

void OnFrameStart()
{
    // Poll for AP client connection (one-time options transfer)
    APOptions_TransferToSave();

    // Process client backfill writes and poll meta auto-unlocks.
    CheckDetection_OnFrameStart();

    GameData *gd = Gm_GetGameData();

    if (gd->update.pause_kind & (1 << PAUSEKIND_SYS) && !(gd->update.pause_kind_prev & (1 << PAUSEKIND_SYS)))
        OSReport("[Main] Game is paused via debug mode!\n");

    if (gd->update.pause_kind & (1 << PAUSEKIND_GAME) && !(gd->update.pause_kind_prev & (1 << PAUSEKIND_GAME)))
        OSReport("[Main] Game is paused via in-game!\n");

    // debug pad handlers
    HSD_Pad *pad = &stc_engine_pads[0];
    if (pad->down & PAD_BUTTON_DPAD_LEFT)
    {
        GOBJ *mg = Ply_GetMachineGObj(0);
        if (mg)
        {
            MachineData *md = mg->userdata;
            ItemKind kind = ITKIND_ACCELFAKE + HSD_Randi(ITKIND_WEIGHTFAKE - ITKIND_ACCELFAKE + 1);
            Vec3 spawn_pos = {
                .X = md->pos.X + md->forward.X * 30.0f,
                .Y = md->pos.Y + md->forward.Y * 30.0f,
                .Z = md->pos.Z + md->forward.Z * 30.0f
            };
            ItemDesc desc;
            Item_InitDesc(&desc, kind, 1.0f, 0, &spawn_pos, &md->up, &md->forward, -1, -1, 1, 3, -1, -1);
            Item_Create(&desc);
            OSReport("[Main] Debug: spawned patch item %d at (%.1f, %.1f, %.1f)\n",
                     kind, spawn_pos.X, spawn_pos.Y, spawn_pos.Z);
        }
    }
    if ((pad->down & PAD_BUTTON_DPAD_RIGHT) && ap_data->incoming_item_id == 0)
    {
        ap_data->incoming_item_id = AP_ITEM_METEOR_TRAP;
        OSReport("[Main] Debug: wrote AP_ITEM_METEOR_TRAP to mailbox\n");
    }
    if (pad->down & PAD_BUTTON_DPAD_DOWN)
    {
        if (custom_events && custom_events->Do(CUSTOM_EVKIND_WADDLE_DEE_SWARM))
            OSReport("[Main] Debug: triggered Waddle Dee Swarm\n");
    }
    if (pad->down & PAD_BUTTON_DPAD_UP)
    {

    }

}

// Runs every game tick after the frame has been processed.
void OnFrameEnd()
{
    
}

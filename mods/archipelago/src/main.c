#include "os.h"
#include "game.h"
#include "inline.h"
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
#include "stadium_lock.h"
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
#include "weather_control.h"
#include "spawn_enemy.h"
#include "custom_events.h"
#include "airride_speed.h"
#include "energylink_spend.h"
#include "debug_menu.h"

// Define global variables
ArchipelagoData *archipelago_data;
APMenuSettings hoshi_menu_settings;
KARSave *save_data;

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
                .val = &hoshi_menu_settings.deathlink_enabled,
                .value_num = 2,
                .value_names = (char *[]){
                    "Off",
                    "On",
                },
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
                            .val = &hoshi_menu_settings.energylink_enabled,
                            .value_num = 2,
                            .value_names = (char *[]){
                                "Off",
                                "On",
                            },
                        },
                        &(OptionDesc){
                            .name = "Auto-Charge",
                            .description = "Automatically spend energy to fill machine charge meter",
                            .kind = OPTKIND_VALUE,
                            .val = &hoshi_menu_settings.energylink_autocharge,
                            .value_num = 2,
                            .value_names = (char *[]){
                                "Off",
                                "On",
                            },
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
                .val = &hoshi_menu_settings.traplink_enabled,
                .value_num = 2,
                .value_names = (char *[]){
                    "Off",
                    "On",
                },
            },
            &(OptionDesc){
                .name = "AP Message Textbox",
                .description = "Enable or Disable the in-game textbox for Archipelago Messages",
                .kind = OPTKIND_VALUE,
                .val = &hoshi_menu_settings.textbox_enabled,
                .value_num = 2,
                .value_names = (char *[]){
                    "Off",
                    "On",
                },
            },
            &(OptionDesc){
                .name = "City Trial Weather",
                .description = "Choose the sky/lighting preset for City Trial",
                .kind = OPTKIND_VALUE,
                .val = &hoshi_menu_settings.weather_control,
                .value_num = 18,
                .value_names = (char *[]){
                    "Shuffle",
                    "Day",
                    "Midnight",
                    "Light Fog",
                    "Dusk 2",
                    "Dusky Clouds",
                    "Dark Vignette",
                    "Day 2",
                    "Blue Sky",
                    "Pink Sky",
                    "Dense Fog",
                    "Foggy",
                    "Dusk",
                    "Night",
                    "Gray Sky",
                    "Dark Purple",
                    "Red Vignette",
                    "Dark Low Vis",
                },
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
    .save_size = sizeof(struct KARSave),        // Size of the save data your mod uses.
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
};

// Runs immediately after the mod file is loaded.
// Calls to HSD_MemAlloc in THIS function specifically will persist throughout the entire runtime of the game.
// All calls to HSD_MemAlloc from elsewhere will return an allocation that exists only within the current scene.
void OnBoot()
{
    OSReport("Running OnBoot for %s\n", mod_desc.name);

    // Persistent allocation of archipelago_data
    archipelago_data = HSD_MemAlloc(sizeof(ArchipelagoData));
    memset(archipelago_data, 0, sizeof(ArchipelagoData));
    OSReport("ArchipelagoData allocated at 0x%08x (%d bytes)\n",
             (uint)archipelago_data, sizeof(ArchipelagoData));

    // Place pointer to this allocation at a static address so the Python client can find it
    ArchipelagoData **static_ptr = (ArchipelagoData **)0x805d52d4;
    (*static_ptr) = archipelago_data;
    OSReport("Static pointer at 0x805d52d4 set to 0x%08x\n", (uint)archipelago_data);

    // Replace ClearChecker_CheckUnlocked with AP bitfield hook
    ChecklistRewards_OnBoot();

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

    // Patches for Kirby color gating
    GateColors_OnBoot();

    // Hook for City Trial weather/sky control
    WeatherControl_OnBoot();

    // Null-safe patches for standalone enemy spawning
    SpawnEnemy_OnBoot();

    // Custom event state handler wrappers
    CustomEvents_OnBoot();

    // Traplink send hooks
    TrapLink_OnBoot();
}

// Runs on boot when hoshi creates save data for the mod.
// Initialize default save file values here.
void OnSaveInit()
{
    save_data = (KARSave *)mod_desc.save_ptr;
    OSReport("save data for %s created!\n", mod_desc.name);
    memset(save_data, 0, sizeof(*save_data));

    ChecklistRewards_OnSaveInit();
}

// Debug: unlock exactly 1 random item per gate category for standalone testing.
// Called from OnSaveLoaded when no AP client options have been received.
static void DebugSimulateLocationData(void); // forward declaration

// Runs on startup after any save data is loaded into memory.
// This callback is executed regardless of if a memory card is inserted or contained existing save data.
void OnSaveLoaded()
{
    save_data = (KARSave *)mod_desc.save_ptr;
    save_data->boot_num++;
    OSReport("%s present for [%d] boot cycles\n", mod_desc.name, save_data->boot_num);
    OSReport("items received: [%d]\n", save_data->item_received_count);

    // Sync received count to archipelago_data so the AP client can read it
    archipelago_data->item_received_index = save_data->item_received_count;
    OSReport("Synced item_received_index = %d\n", save_data->item_received_count);
    OSReport("AP slot options %s in save data\n",
             save_data->options_received ? "found" : "not yet received");

    // Apply hoshi-saved debug menu toggle states to save data masks.
    // Must happen before anything that reads gate masks (e.g. StadiumLock).
    DebugMenu_ApplyToSave();

    // Restore reward tables and received checklist rewards from save
    ChecklistRewards_OnSaveLoaded();

    OSReport("Debug gate masks applied:\n");
    OSReport("  machines=0x%08x abilities=0x%04x events=0x%08x\n",
             save_data->machine_unlocked_mask, save_data->ability_unlocked_mask,
             save_data->event_unlocked_mask);
    OSReport("  patches=0x%04x items=0x%08x boxes=0x%02x\n",
             save_data->patch_unlocked_mask, save_data->item_unlocked_mask,
             save_data->box_unlocked_mask);
    OSReport("  ar_stages=0x%04x tr_stages=0x%04x tr_items=0x%08x\n",
             save_data->airride_stage_unlocked_mask, save_data->topride_stage_unlocked_mask,
             save_data->topride_item_unlocked_mask);
    OSReport("  colors=0x%02x stadiums=0x%08x\n",
             save_data->color_unlocked_mask, save_data->stadium_unlocked_mask);

    // Debug: reveal all checklists and simulate location data for testing
    RevealAllChecklists();
    DebugSimulateLocationData();
    ChecklistRewards_ApplyLocations();  // Apply immediately (normally happens on next frame)

    // Signal to the AP client that the mod is fully initialized
    archipelago_data->game_ready = 1;
    OSReport("game_ready set — waiting for AP client connection\n");
}

// Check if the AP client has written slot options to ArchipelagoData.
// On first detection, copy them to save data (one-time transfer).
// Options are immutable per AP slot, so this only runs once per save file.
static void APOptions_TransferToSave()
{
    // Already received options for this slot
    if (save_data->options_received)
        return;
    // Client hasn't written options yet
    if (!archipelago_data->options_valid)
        return;

    // One-time transfer: copy options to save data
    OSReport("AP client connected — transferring slot options to save data\n");
    memcpy(&save_data->options, &archipelago_data->options, sizeof(APSlotOptions));
    save_data->options_received = 1;

    // Set initial menu toggle values from AP slot options
    hoshi_menu_settings.deathlink_enabled = save_data->options.death_link;
    hoshi_menu_settings.energylink_enabled = save_data->options.energy_link;
    hoshi_menu_settings.traplink_enabled = save_data->options.traplink;
    OSReport("Menu toggles set — DeathLink: %d, EnergyLink: %d, TrapLink: %d\n",
             save_data->options.death_link, save_data->options.energy_link, save_data->options.traplink);
    OSReport("Goals — CityTrial: %d, AirRide: %d, TopRide: %d\n",
             save_data->options.city_trial_goal, save_data->options.air_ride_goal, save_data->options.top_ride_goal);
    OSReport("CityTrial — PermanentPatches: %d, ProgressivePatchCaps: %d (start: %d), ProgressiveStadiums: %d\n",
             save_data->options.city_trial_permanent_patches,
             save_data->options.city_trial_progressive_patch_caps,
             save_data->options.city_trial_patch_cap_amount,
             save_data->options.city_trial_progressive_stadiums);
    OSReport("RevealChecklists: %d\n", save_data->options.reveal_checklists);

    if (save_data->options.reveal_checklists)
        RevealAllChecklists();

    Hoshi_WriteSave();
    OSReport("AP slot options saved to memory card\n");
}

// Runs when entering the main menu.
void OnMainMenuLoad()
{
    OSReport("Entering the main menu.\n");

    // Check for AP slot options (one-time transfer to save)
    APOptions_TransferToSave();

    // Pre-fix all color fields so they're correct before any select
    // screen initializes. This mirrors what CT's OnPlayerSelectLoad
    // does, ensuring AR icon/color data is valid from the start.
    GateColors_ForceDefaultColors();
}

// Runs when entering the player select menu (Air Ride or City Trial).
void OnPlayerSelectLoad()
{
    OSReport("Entering player select (minor %d).\n", Scene_GetCurrentMinor());

    // Filter locked machines from the City Trial select screen
    if (Scene_GetCurrentMinor() == MNRKIND_CITYPLYSELECT)
        GateMachines_FilterSelectList();

    // Force any locked Kirby colors to default
    GateColors_ForceDefaultColors();
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
    OSReport("Now starting %s game on GroundKind [%d]. \nStageKind: [%d]. \nCurrent CityMode: [%d]. \nCurrent StadiumKind: [%d]. \nCurrent Stadium Group: [%d].\n Damage Enabled: [%d].\n",
             mode_name, Gm_GetCurrentGrKind(), Gm_GetCurrentStageKind(), Gm_GetCityMode(),
             Gm_GetCurrentStadiumKind(), Gm_GetCurrentStadiumGroup(), Gm_IsDamageEnabled());

    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) == PKIND_NONE)
            continue;

        GOBJ *rg = Ply_GetRiderGObj(i);
        RiderData *rd = rg->userdata;
        MachineKind machine_kind = rd->starting_machine_idx;
        OSReport("Player %d using rider [%d] color [%d] riding machine [%d].\n",
                 i + 1, rd->kind, rd->color_idx, machine_kind);
    }

    // Initialize custom event SIS text entries when in City Trial
    if (Gm_GetCurrentGrKind() == GRKIND_CITY1)
    {
        CustomEvents_InitSis();
        GateEvents_LogEnabledEvents();
    }

    GateAbilities_On3DLoadEnd();
    PermanentPatch_On3DLoadEnd();
    AirRideSpeed_On3DLoadEnd();

    if (hoshi_menu_settings.deathlink_enabled)
        DeathLink_On3DLoadEnd();

    if (hoshi_menu_settings.energylink_enabled)
        EnergyLink_On3DLoadEnd();

    if (hoshi_menu_settings.traplink_enabled)
        TrapLink_On3DLoadEnd();
}

// Runs when pausing the match. The index of the pausing player is passed in as an argument.
void On3DPause(int pause_ply)
{
    OSReport("Player [%d] has paused the game.\n", pause_ply + 1);
}

// Runs when unpausing the match.
void On3DUnpause(int pause_ply)
{
    OSReport("Resuming the game.\n");
}

// Runs when exiting a match.
void On3DExit()
{
    OSReport("Exiting 3D.\n");
}

// Runs every scene change.
// The memory heap is destroyed and recreated every scene change, meaning HSD objects
// such as CObj's (camera) and JObj's (models) will not persist across them.
// This hook can be used to recreate processes/objects that should always be running.
void OnSceneChange()
{
    OSReport("We are now entering major %d / minor %d\n",
             Scene_GetCurrentMajor(), Scene_GetCurrentMinor());

    CreateTextBox_OnSceneChange();

    APItems_OnSceneChange();
}

// Number of standalone AP items (IDs 1 through AP_ITEM_ALL_DOWN inclusive).
// Used only by the debug D-pad controls below.
#define DEBUG_STANDALONE_COUNT 10

// Simulate the AP client sending location data by filling the ArchipelagoData
// location arrays with a random shuffle. Rewards are distributed:
//   ~33% same-mode (reward stays in its own mode's checklist)
//   ~33% cross-mode (reward placed in a different mode's checklist)
//   ~33% remote (sent to another world, no local checkbox)
// The existing OnFrameStart logic detects location_data_valid and calls
// ChecklistRewards_ApplyLocations() on the next frame.
static void DebugSimulateLocationData()
{
    static const int counts[GMMODE_NUM] = {
        [GMMODE_AIRRIDE]   = REWARD_COUNT_AIRRIDE,
        [GMMODE_TOPRIDE]   = REWARD_COUNT_TOPRIDE,
        [GMMODE_CITYTRIAL] = REWARD_COUNT_CITYTRIAL,
    };
    u16 *loc_arrays[GMMODE_NUM] = {
        archipelago_data->location_airride,
        archipelago_data->location_topride,
        archipelago_data->location_citytrial,
    };

    // Build per-mode shuffled pools of clear_kinds (0-119)
    u8 pools[GMMODE_NUM][120];
    int pool_idxs[GMMODE_NUM] = {0, 0, 0};
    for (int m = 0; m < GMMODE_NUM; m++)
    {
        for (int i = 0; i < 120; i++)
            pools[m][i] = (u8)i;
        for (int i = 119; i > 0; i--)
        {
            int j = HSD_Randi(i + 1);
            u8 tmp = pools[m][i];
            pools[m][i] = pools[m][j];
            pools[m][j] = tmp;
        }
    }

    for (int mode = 0; mode < GMMODE_NUM; mode++)
    {
        int count = counts[mode];
        int local_count = 0, cross_count = 0;

        for (int i = 0; i < count; i++)
        {
            int roll = HSD_Randi(3);
            if (roll == 0 && pool_idxs[mode] < 120)
            {
                // Same-mode: reward stays in its own checklist
                u8 ck = pools[mode][pool_idxs[mode]++];
                loc_arrays[mode][i] = ((u16)mode << 8) | ck;
                local_count++;
            }
            else if (roll == 1)
            {
                // Cross-mode: reward placed in a different mode's checklist
                int target = (mode + 1 + HSD_Randi(2)) % GMMODE_NUM;
                if (pool_idxs[target] < 120)
                {
                    u8 ck = pools[target][pool_idxs[target]++];
                    loc_arrays[mode][i] = ((u16)target << 8) | ck;
                    cross_count++;
                }
                else
                {
                    loc_arrays[mode][i] = 0xFFFF;
                }
            }
            else
            {
                // Remote
                loc_arrays[mode][i] = 0xFFFF;
            }
        }
        OSReport("  Mode %d: %d same, %d cross, %d remote\n",
                 mode, local_count, cross_count,
                 count - local_count - cross_count);
    }

    archipelago_data->location_data_valid = 1;
    OSReport("Debug: location data written, will apply next frame\n");
}

// Runs every game tick, even when the game is paused normally or via debug mode.
void OnFrameStart()
{
    GameData *gd = Gm_GetGameData();

    if (gd->update.pause_kind & (1 << PAUSEKIND_SYS) && !(gd->update.pause_kind_prev & (1 << PAUSEKIND_SYS)))
        OSReport("Game is paused via debug mode!\n");

    if (gd->update.pause_kind & (1 << PAUSEKIND_GAME) && !(gd->update.pause_kind_prev & (1 << PAUSEKIND_GAME)))
        OSReport("Game is paused via in-game!\n");

    // Debug controls — only write to the mailbox when it is empty
    if (archipelago_data->incoming_item_id == 0)
    {
        if (Pad_GetDown(0) & PAD_BUTTON_DPAD_LEFT)
        {
            // Debug: spawn a random enemy near player 0 with spline path-following
            GOBJ *mg = Ply_GetMachineGObj(0);
            if (mg)
                SpawnEnemy_Random(mg, 1);
        }
        else if (Pad_GetDown(0) & PAD_BUTTON_DPAD_RIGHT)
        {
            int total = DEBUG_STANDALONE_COUNT + PATCHKIND_NUM + EVKIND_NUM + ITKIND_NUM;
            int r = HSD_Randi(total);
            uint ap_id;
            if (r < DEBUG_STANDALONE_COUNT)
                ap_id = 1 + r;
            else if (r < DEBUG_STANDALONE_COUNT + PATCHKIND_NUM)
                ap_id = AP_PERM_PATCH_BASE + (r - DEBUG_STANDALONE_COUNT);
            else if (r < DEBUG_STANDALONE_COUNT + PATCHKIND_NUM + EVKIND_NUM)
                ap_id = AP_EVENT_BASE + (r - DEBUG_STANDALONE_COUNT - PATCHKIND_NUM);
            else
                ap_id = AP_ITKIND_BASE + (r - DEBUG_STANDALONE_COUNT - PATCHKIND_NUM - EVKIND_NUM);
            OSReport("Setting incoming_item_id to random AP ID %d from DPAD RIGHT...\n", ap_id);
            archipelago_data->incoming_item_id = ap_id;
        }
        else if (Pad_GetDown(0) & PAD_BUTTON_DPAD_DOWN)
        {
            OSReport("Triggering meteor event via DPAD DOWN...\n");
            archipelago_data->incoming_item_id = AP_EVENT_METEOR;
        }
        else if (Pad_GetDown(0) & PAD_BUTTON_DPAD_UP)
        {
            archipelago_data->incoming_item_id = AP_ITEM_EVENT_CUSTOM;
        }
    }
}

// Runs every game tick after the frame has been processed.
void OnFrameEnd()
{
    
}

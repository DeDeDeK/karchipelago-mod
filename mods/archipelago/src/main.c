#include "os.h"
#include "game.h"
#include "inline.h"
#include "hoshi/settings.h"
#include "hoshi/mod.h"
#include "stage.h"

#include "main.h"
#include "textbox.h"
#include "deathlink.h"
#include "city_trial_event.h"
#include "item_queue.h"
#include "energylink.h"
#include "traplink.h"
#include "spawn_item.h"

// Define global variables
ArchipelagoData *archipelago_data;
HoshiMenuSettings hoshi_menu_settings;
TemplateSave *save_data;

// Creates a menu that appears in the in-game Settings menu.
// Menus may be nested by setting the OptionDesc::kind to OPTKIND_MENU
OptionDesc ModSettings = {
    .name = "Archipelago Settings",
    .description = "Interface with mod settings here.",
    .kind = OPTKIND_MENU,
    .menu_ptr = &(MenuDesc){
        .option_num = 5,
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
                .name = "Energy Link Spend",
                .description = "Spend your energy here.",
                .kind = OPTKIND_MENU,
                .menu_ptr = &(MenuDesc){
                    .option_num = 1,
                    .options = {
                        &(OptionDesc){
                            .name = "Energylink Spend",
                            .description = "Spend energy",
                            .kind = OPTKIND_NUM,
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
        },
    },
};

ModDesc mod_desc = {
    .name = "KARchipelago",                     // Name of the mod.
    .author = "DeDeDK",                         // Creator of the mod.
    .version.major = 1,                         // Version of the mod.
    .version.minor = 0,
    .save_size = sizeof(struct TemplateSave),   // (optional) Size of the save data your mod uses. A pointer to the saved data is passed into OnSaveInit.
    .save_ptr = 0,                              // Pointer to the mod's save data. Updated by hoshi at runtime. read-only! hoshi will write this pointer during installation.
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

    // persistent allocation of archipelago_data
    archipelago_data = HSD_MemAlloc(sizeof(ArchipelagoData));
    memset(archipelago_data, 0, sizeof(ArchipelagoData));

    // place pointer to this allocation at a static address so python can access it externally
    ArchipelagoData **static_ptr = (ArchipelagoData **)0x805d52d4;
    (*static_ptr) = archipelago_data;

    DeathLink_OnBoot();
}

// Runs on boot when hoshi creates save data for the mod.
// Initialize default save file values here.
void OnSaveInit()
{
    save_data = (TemplateSave *)mod_desc.save_ptr;
    OSReport("save data for %s created!\n", mod_desc.name);
    save_data->boot_num = 0;
    save_data->item_received_index = 0;
}

// Runs on startup after any save data is loaded into memory.
// This callback is executed regardless of if a memory card is inserted or contained existing save data.
void OnSaveLoaded()
{
    save_data = (TemplateSave *)mod_desc.save_ptr;
    save_data->boot_num++;
    OSReport("%s present for [%d] boot cycles\n", mod_desc.name, save_data->boot_num);
    OSReport("item received index is [%d]\n", save_data->item_received_index);

    // Sync received index to archipelago_data so the AP client can read it
    archipelago_data->item_received_index = save_data->item_received_index;
}

// Runs when entering the main menu.
void OnMainMenuLoad()
{
    OSReport("Entering the main menu.\n");
}

// Runs when entering the player select menu.
// Currently only executes when entering city trial player select.
void OnPlayerSelectLoad()
{
    OSReport("Entering the city trial player select menu.\n");
}

// executes before the game is initialized
void On3DLoadStart() {

}

// Runs upon entering a 3D game. Can be either Air Ride or City Trial. Must be explicity checked using Gm_IsInCity().
// Players, riders, their machines, and the map have all been instantiated by the time this is executed.
// executes after the game is initialized (riders, machines, stage, etc are all instantiated)
void On3DLoadEnd()
{
    // determine the game mode
    char *mode_name = Gm_IsInCity() ? "City Trial" : "Air Ride";
    OSReport("Now starting %s game on GroundKind [%d]. \nStageKind: [%d]. \nCurrent CityMode: [%d]. \nCurrent StadiumKind: [%d]. \nCurrent Stadium Group: [%d].\n Damage Enabled: [%d].\n", mode_name, Gm_GetCurrentGrKind(), Gm_GetCurrentStageKind(), Gm_GetCityMode(), Gm_GetCurrentStadiumKind(), Gm_GetCurrentStadiumGroup(), Gm_IsDamageEnabled());
    // loop across all 5 potential players
    for (int i = 0; i < 5; i++)
    {
        // skip non-present players
        if (Ply_GetPKind(i) == PKIND_NONE)
            continue;

        // get this rider's data
        GOBJ *rg = Ply_GetRiderGObj(i);
        RiderData *rd = rg->userdata;

        // get this rider's machine kind
        MachineKind machine_kind = rd->starting_machine_idx;

        // log some data on them
        OSReport("Player %d using rider [%d] color [%d] riding machine [%d].\n",
                 i + 1,
                 rd->kind,
                 rd->color_idx,
                 machine_kind);
    }

    // DeathLink
    if (hoshi_menu_settings.deathlink_enabled) {
        DeathLink_On3DLoadEnd();
    }

    // EnergyLink
    if (hoshi_menu_settings.energylink_enabled) {
        EnergyLink_On3DLoadEnd();
    }

    // TrapLink
    if (hoshi_menu_settings.traplink_enabled) {
        TrapLink_On3DLoadEnd();
    }

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
    // Log out the current scene information
    OSReport("We are now entering major %d / minor %d\n", Scene_GetCurrentMajor(), Scene_GetCurrentMinor());

    // Textbox
    if (hoshi_menu_settings.textbox_enabled) {
        CreateTextBox_OnSceneChange();
    }

    // Item Queue
    ItemQueue_OnSceneChange();
}

// Runs every game tick, even when the game is paused normally or via debug mode.
void OnFrameStart()
{
    GameData *gd = Gm_GetGameData();

    if (gd->update.pause_kind & (1 << PAUSEKIND_SYS) && !(gd->update.pause_kind_prev & (1 << PAUSEKIND_SYS)))
        OSReport("Game is paused via debug mode!\n");

    if (gd->update.pause_kind & (1 << PAUSEKIND_GAME) && !(gd->update.pause_kind_prev & (1 << PAUSEKIND_GAME)))
        OSReport("Game is paused via in-game!\n");

    if (Pad_GetDown(0) & PAD_BUTTON_DPAD_LEFT) {
        // Simulate AP client writing to the mailbox (blue box = 100 + ITKIND_BOXBLUE)
        OSReport("Writing test item (blue box) to mailbox from DPAD LEFT...\n");
        TextBox_Enqueue("Writing test item (blue box) to mailbox...\n");
        archipelago_data->incoming_item_id = 100 + ITKIND_BOXBLUE;
    }

    if (Pad_GetDown(0) & PAD_BUTTON_DPAD_RIGHT) {
        ItemKind random_kind = Gm_GetRandomItem(BOXKIND_ALL, ITGROUP_ALL, 0);
        OSReport("Spawning random item kind %d from DPAD RIGHT...\n", random_kind);
        SpawnItemHumans(random_kind);
    }

    if (Pad_GetDown(0) & PAD_BUTTON_DPAD_DOWN) {
        OSReport("Setting deathlink receive from DPAD DOWN...\n");
        TextBox_Enqueue("Setting deathlink receive from DPAD DOWN...");
        archipelago_data->deathlink_receive = 1;
    }

    if (Pad_GetDown(0) & PAD_BUTTON_DPAD_UP) {
        if (Gm_IsInCity()) {
            GOBJ *mg = Ply_GetMachineGObj(0);
            if (mg) {
                MachineData *md = mg->userdata;
                OSReport("Giving candy to P0...\n");
                Rider_SetCandyTimer(md->rider_gobj, 300);
            }
        }
    }
}

void OnFrameEnd() {
    
}
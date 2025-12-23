#include "os.h"
#include "game.h"
#include "inline.h"

#include "main.h"
#include "patches.h"
#include "hoshi/settings.h"
#include "hoshi/mod.h"

#include "textbox.h"
#include "deathlink.h"

// Shared Archipelago data
ArchipelagoData archipelago_data;

// Creates a menu that appears in the in-game Settings menu.
// Menus may be nested by setting the OptionDesc::kind to OPTKIND_MENU
OptionDesc ModSettings = {
    .name = "Archipelago Settings",
    .description = "Interface with mod settings here.",
    .kind = OPTKIND_MENU,
    .menu_ptr = &(MenuDesc){
        .option_num = 4,
        .options = {
            &(OptionDesc){
                .name = "Death Link",
                .description = "Enable or Disable Death Link",
                .kind = OPTKIND_VALUE,
                .val = &archipelago_data.deathlink_enabled,
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
                .val = &archipelago_data.energylink_enabled,
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
                .name = "AP Message Textbox",
                .description = "Enable or Disable the in-game textbox for Archipelago Messages",
                .kind = OPTKIND_VALUE,
                .val = &archipelago_data.textbox_enabled,
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
    .name = "KARchipelago",                    // Name of the mod.
    .author = "DeDeDK",                     // Creator of the mod.
    .version.major = 1,                         // Version of the mod.
    .version.minor = 0,
    .save_size = sizeof(struct TemplateSave),   // (optional) Size of the save data your mod uses. A pointer to the saved data is passed into OnSaveInit.
    .save_ptr = 0,                              // Pointer to the mod's save data. Updated by hoshi at runtime.
    .option_desc = &ModSettings,                // Link to the settings menu
    .OnBoot = OnBoot,
    .OnSaveInit = OnSaveInit,
    .OnSaveLoaded = OnSaveLoaded,
    .OnMainMenuLoad = OnMainMenuLoad,
    .OnPlayerSelectLoad = OnPlayerSelectLoad,
    .On3DLoad = On3DLoad,
    .On3DPause = On3DPause,
    .On3DUnpause = On3DUnpause,
    .On3DExit = On3DExit,
    .OnSceneChange = OnSceneChange,
};

// Runs immediately after the mod file is loaded.
// Calls to HSD_MemAlloc in THIS function specifically will persist throughout the entire runtime of the game.
// All calls to HSD_MemAlloc from elsewhere will return an allocation that exists only within the current scene.
void OnBoot()
{
    OSReport("Hello from boot\n");

    // Apply any patches
    Patches_Apply();
    DeathLinkPatchesApply();
}

// Runs on boot when hoshi creates save data for the mod.
// Initialize default save file values here.
void OnSaveInit()
{
    TemplateSave *save = (TemplateSave *)mod_desc.save_ptr;
    OSReport("save data for ", mod_desc.name, " created!\n");
    save->boot_num = 0;
    save->item_received_index = 0;
}

// Runs on startup after any save data is loaded into memory.
// This callback is executed regardless of if a memory card is inserted or contained existing save data.
void OnSaveLoaded()
{
    TemplateSave *save = (TemplateSave *)mod_desc.save_ptr;
    save->boot_num++;
    OSReport(mod_desc.name, " present for [%d] boot cycles\n", save->boot_num);
    OSReport("item received index is [%d]\n", save->item_received_index);
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

// Runs upon entering a 3D game. Can be either Air Ride or City Trial. Must be explicity checked using Gm_IsInCity().
// Players, riders, their machines, and the map have all been instantiated by the time this is executed.
void On3DLoad()
{
    // determine the game mode
    char *mode_name = Gm_IsInCity() ? "City Trial" : "Air Ride";
    OSReport("Now starting %s game on map [%d]. StageKind: [%d]\n", mode_name, Gm_GetCurrentGrKind(), Gm_GetCurrentStageKind());

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
    if (archipelago_data.textbox_enabled) {
        CreateTextBox_OnSceneChange();
    }

    // Deathlink
    DeathLink_OnSceneChange();
}

// Runs every game tick, even when the game is paused normally or via debug mode.
void OnFrame()
{
    GameData *gd = Gm_GetGameData();

    if (gd->update.pause_kind & (1 << PAUSEKIND_SYS) && !(gd->update.pause_kind_prev & (1 << PAUSEKIND_SYS)))
        OSReport("Game is paused via debug mode!\n");

    if (gd->update.pause_kind & (1 << PAUSEKIND_GAME) && !(gd->update.pause_kind_prev & (1 << PAUSEKIND_GAME)))
        OSReport("Game is paused via in-game!\n");
}

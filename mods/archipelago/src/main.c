#include "hsd.h"
#include "os.h"
#include "audio.h"
#include "game.h"
#include "inline.h"

#include "main.h"
#include "patches.h"
#include "hoshi/settings.h"

#include "textbox.h"
#include "deathlink.h"

#define MOD_NAME "KARchipelago"

// pointer to an array allocated at runtime in OnBoot.
ArchipelagoData *archipelago_data;

// Global variables for menu bindings. These are automatically loaded in by Hoshi on SaveLoaded
int deathlink_enabled = 0;
int energylink_enabled = 0;

// Special Mod globals
char ModName[] = MOD_NAME;      
char ModAuthor[] = "DeDeDK";    
char ModVersion[] = "v1.0";     
int ModSaveSize = sizeof(struct TemplateSave); // (optional) Size of the save data your mod uses. A pointer to the saved data is passed into OnSaveInit.
TemplateSave *ModSave;                         // Pointer to the mod's save data. Updated by hoshi at runtime.

// variables from other files
extern Text textbox_text;

// Creates a menu that appears in the in-game Settings menu.
// Menus may be nested by setting the OptionDesc::kind to OPTKIND_MENU
OptionDesc ModSettings = {
    .name = "Archipelago Settings",
    .description = "Interface with mod settings here.",
    .kind = OPTKIND_MENU,
    .menu_ptr = &(MenuDesc){
        .option_num = 3,
        .options = {
            &(OptionDesc){
                .name = "Death Link",
                .description = "Enable or Disable Death Link",
                .kind = OPTKIND_VALUE,
                .val = &deathlink_enabled,
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
                .val = &energylink_enabled,
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
                            .name = "Main Menu",
                            .description = "Enter the game's main menu",
                            .kind = OPTKIND_SCENE,
                            .major_idx = MJRKIND_MENU,
                        },
                    },
                },
            },
        },
    },
};

// Runs immediately after the mod file is loaded.
// Calls to HSD_MemAlloc in THIS function specifically will persist throughout the entire runtime of the game.
// All calls to HSD_MemAlloc from elsewhere will return an allocation that exists only within the current scene.
void OnBoot()
{
    OSReport("Hello from boot\n");

    // persistently allocate memory for shared archipelago data
    archipelago_data = HSD_MemAlloc(sizeof(ArchipelagoData)); 

    Patches_Apply();
    DeathLinkPatchesApply();
}

// Runs on boot when hoshi creates save data for the mod.
// Initialize default save file values here.
void OnSaveInit()
{
    OSReport("save data for " MOD_NAME " created!\n");
    ModSave->boot_num = 0;
    ModSave->item_received_index = 0;
}

// Runs on startup after any save data is loaded into memory.
// This callback is executed regardless of if a memory card is inserted or contained existing save data.
void OnSaveLoaded()
{
    ModSave->boot_num++;
    OSReport(MOD_NAME " present for [%d] boot cycles\n", ModSave->boot_num);
    OSReport("item received index is [%d]\n", ModSave->item_received_index);
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

    //CreateTextBox_OnSceneChange();
}

// Runs every game tick, even when the game is paused normally or via debug mode.
void OnFrame()
{
    GameData *gd = Gm_GetGameData();

    if (gd->update.pause_kind & (1 << PAUSEKIND_SYS) && !(gd->update.pause_kind_prev & (1 << PAUSEKIND_SYS)))
        OSReport("Game is paused via debug mode!\n");

    if (gd->update.pause_kind & (1 << PAUSEKIND_GAME) && !(gd->update.pause_kind_prev & (1 << PAUSEKIND_GAME)))
        OSReport("Game is paused via in-game!\n");

    //DeathLinkOnFrame();
}

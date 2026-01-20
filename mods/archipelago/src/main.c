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
        .option_num = 4,
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
    OSReport("Hello from boot\n");

    // Persistently allocate archipelago_data
    // persistent allocation of archipelago_data
    archipelago_data = HSD_MemAlloc(sizeof(ArchipelagoData));

    // place pointer to this allocation at a static address so python can access it
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

    // Debug: Check the reward lookup structure
    OSReport("\n=== Reward Lookup Table Debug ===\n");
    OSReport("stc_reward_lookup address: 0x%08x\n", (u32)stc_reward_lookup);

    // Dump raw memory at 0x805d51d0
    OSReport("Raw memory at 0x805d51d0:\n");
    u8 *raw_mem = (u8 *)0x805d51d0;
    for (int i = 0; i < 32; i++) {
        if (i % 16 == 0) OSReport("  %08x: ", 0x805d51d0 + i);
        OSReport("%02x ", raw_mem[i]);
        if (i % 16 == 15) OSReport("\n");
    }

    // Try calling the function directly to see what happens
    OSReport("\nTesting ClearChecker_GetClearKindFromRewardKind:\n");
    for (int reward = 0; reward < 5; reward++) {
        int clear_kind = ClearChecker_GetClearKindFromRewardKind(GMMODE_CITYTRIAL, reward);
        OSReport("  City Trial Reward %d -> Clear %d\n", reward, clear_kind);
    }

    // Analyze all game modes
    const char *mode_names[] = {"Air Ride", "Top Ride", "City Trial"};

    for (int gm = 0; gm < GMMODE_NUM; gm++) {
        int num_rewards = ClearChecker_GetRewardNum(gm);
        int unlocked_count = 0;
        int visible_count = 0;
        int filler_count = 0;
        int flagged_count = 0;

        OSReport("\n=== %s Checklist ===\n", mode_names[gm]);
        OSReport("Total reward checkboxes: %d\n", num_rewards);

        for (int reward_kind = 0; reward_kind < num_rewards; reward_kind++) {
            int clear_kind = ClearChecker_GetClearKindFromRewardKind(gm, reward_kind);
            u8 clear_data = ClearChecker_GetClearData(gm, clear_kind);

            // Decode flags using updated field names
            int is_visible = (clear_data & 0x10) != 0;
            int has_reward = (clear_data & 0x08) != 0;
            int is_unlocked = (clear_data & 0x04) != 0;
            int is_filler = (clear_data & 0x02) != 0;
            int is_flagged_unlock = (clear_data & 0x01) != 0;

            // Update counters
            if (is_unlocked) unlocked_count++;
            if (is_visible) visible_count++;
            if (is_filler) filler_count++;
            if (is_flagged_unlock) flagged_count++;
            
            // Determine box type
            const char *box_type = "Unknown";
            if (is_filler && has_reward) {
                box_type = "Purple/Filler";
            } else if (has_reward && is_unlocked) {
                box_type = "Red/Reward";
            } else if (is_unlocked) {
                box_type = "Green/Complete";
            } else if (is_visible) {
                box_type = "Visible/Locked";
            }

            OSReport("  Reward %2d -> Clear %3d [%s%s%s%s%s] 0x%02x %s\n",
                reward_kind, clear_kind,
                is_visible ? "V" : "-",        // V = Visible
                has_reward ? "R" : "-",        // R = has Reward
                is_unlocked ? "U" : "-",       // U = Unlocked
                is_filler ? "F" : "-",         // F = Filler
                is_flagged_unlock ? "N" : "-", // N = New/flagged
                clear_data,
                box_type);
        }

        OSReport("Stats: %d unlocked, %d visible, %d filler, %d flagged\n",
                 unlocked_count, visible_count, filler_count, flagged_count);
    }
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
        OSReport("Queueing ability item from DPAD LEFT...\n");
        TextBox_Enqueue("Queueing ability item from DPAD LEFT...\n");
        APItem item = {0, ITEM_KIND_ABILITY, ITEM_CLASSIFICATION_PROGRESSION};
        Item_Enqueue(item);
    }

    if (Pad_GetDown(0) & PAD_BUTTON_DPAD_RIGHT) {
        // OSReport("Queueing event item from DPAD RIGHT...\n");
        // TextBox_Enqueue("Queueing event item from DPAD RIGHT...\n");
        // APItem item = {0, ITEM_KIND_CITY_TRIAL_EVENT, ITEM_CLASSIFICATION_PROGRESSION};
        // Item_Enqueue(item);
        OSReport("Triggering patch drop...\n");
        TextBox_Enqueue("Triggering patch drop...\n");
        if (Ply_GetPKind(0) == PKIND_HMN) {
            GOBJ *rg = Ply_GetRiderGObj(0);
            RiderData *rd = rg->userdata;
            GOBJ *mg = Ply_GetMachineGObj(0);
            MachineData *md = mg->userdata;
            if (mg) {
                int mode = HSD_Randi(3);
                OSReport("Calling patch drop with mode %d...\n", mode);
                Rider_DropPatches(rd, rd->stats.values, mode);
            }
        }
    }

    if (Pad_GetDown(0) & PAD_BUTTON_DPAD_DOWN) {
        OSReport("Setting deathlink receive from DPAD DOWN...\n");
        TextBox_Enqueue("Setting deathlink receive from DPAD DOWN...");
        archipelago_data->deathlink_receive = 1;
    }

    if (Pad_GetDown(0) & PAD_BUTTON_DPAD_UP) {
        // OSReport("Triggering patch item from DPAD...\n");
        // TextBox_Enqueue("Triggering patch item from DPAD...\n");
        // APItem item = {0, ITEM_KIND_PATCH, ITEM_CLASSIFICATION_PROGRESSION};
        // Item_Enqueue(item);
        
        // test for spawning an item
        if (Ply_GetPKind(0) == PKIND_HMN) {
            GOBJ *mg = Ply_GetMachineGObj(0);
            MachineData *md = mg->userdata;
            if (mg) {
                Vec3 spawn_pos = {
                    .X = md->pos.X,
                    .Y = md->pos.Y,
                    .Z = md->pos.Z
                };
                ItemDesc item_desc;
                ItemKind it_kind = HSD_Randi(ITKIND_NUM - 1);
                Item_InitDesc(&item_desc, it_kind, 1.0, 0, &spawn_pos, &md->up, &md->forward, -1, -1);
                GOBJ *item_gobj = Item_Create(&item_desc);
                ItemData *id = item_gobj->userdata;
                Machine_OnTouchItem(md, id);
           }
        }
    }
}

void OnFrameEnd() {
    
}
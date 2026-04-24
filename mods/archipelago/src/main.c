#include <string.h>

#include "os.h"
#include "game.h"
#include "scene.h"
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
#include "spawn_rate.h"
#include "item_spawn_filter.h"
#include "settings_menu.h"
#include "main_menu.h"
#include "debug_menu.h"
#include "goal_max_stats_ct.h"

// Define global variables.
APData *ap_data;
APSave *ap_save;

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
    SpawnItem_OnBoot();

    // Patches for Kirby color gating
    GateColors_OnBoot();

    // Null-safe patches for standalone enemy spawning
    SpawnEnemy_OnBoot();

    // Traplink send hooks
    TrapLink_OnBoot();

    // Item spawn rate scaling hooks (City Trial + Top Ride)
    SpawnRate_OnBoot();

    // Item spawn table filtering hooks (covers all gate categories)
    ItemSpawnFilter_OnBoot();

    // Replace main-menu demo rider/machine (Kirby on Warp Star -> Dedede on Wheelie)
    MainMenu_OnBoot();
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
    ap_save = (APSave *)mod_desc.save_ptr;
    ap_save->boot_num++;

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

    // Pull masks back into debug menu toggle state so the display reflects
    // everything just re-granted by ChecklistRewards_OnSaveLoaded (AP-received
    // rewards re-apply their gate unlocks but don't update color_state[] and
    // friends). Without this, the debug menu would show granted items as locked.
    DebugMenu_RefreshStateFromMasks();

    // Publish restored link-toggle state. Hoshi's Mod_CopyFromSave has run
    // by now, so ap_menu_settings reflects the player's persisted choices.
    SyncLinkMenuStateToAPData();

    // Signal to the AP client that the mod is fully initialized
    ap_data->game_ready = 1;
    OSReport("[Main] game_ready set - waiting for AP client connection\n");
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
    SyncLinkMenuStateToAPData();
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
    OSReport("[Main] Starting %s: Ground=%d Stage=%d CityMode=%d Stadium=%d(%d) Damage=%d ItemData=%d\n",
             mode_name, Gm_GetCurrentGrKind(), Gm_GetCurrentStageKind(),
             Gm_GetCityMode(), Gm_GetCurrentStadiumKind(),
             Gm_GetCurrentStadiumGroup(), Gm_IsDamageEnabled(), Item_CheckIsLoaded());

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

    GateEvents_On3DLoadEnd();

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

    CreateTextBox_OnSceneChange();

    APItems_OnSceneChange();
}

void OnFrameStart()
{
    // Poll for AP client connection (one-time options transfer)
    APOptions_TransferToSave();

    // Process client backfill writes and poll meta auto-unlocks.
    CheckDetection_OnFrameStart();

    // debug pad handlers
    HSD_Pad *pad = &stc_engine_pads[0];
    if (pad->down & PAD_BUTTON_DPAD_LEFT)
    {
        // Grant a random remote (non-local) checklist reward
        int total = REWARD_COUNT_AIRRIDE + REWARD_COUNT_TOPRIDE + REWARD_COUNT_CITYTRIAL;
        int pick = HSD_Randi(total);
        GameMode mode;
        u8 ri;
        if (pick < REWARD_COUNT_AIRRIDE)
        {
            mode = GMMODE_AIRRIDE;
            ri = pick;
        }
        else if (pick < REWARD_COUNT_AIRRIDE + REWARD_COUNT_TOPRIDE)
        {
            mode = GMMODE_TOPRIDE;
            ri = pick - REWARD_COUNT_AIRRIDE;
        }
        else
        {
            mode = GMMODE_CITYTRIAL;
            ri = pick - REWARD_COUNT_AIRRIDE - REWARD_COUNT_TOPRIDE;
        }

        if (ap_save->shuffled_rewards[mode][ri] != 0xFFFF)
            OSReport("[Main] Debug: reward %d:%d is local, skipping\n", mode, ri);
        else
        {
            ChecklistRewards_Grant(mode, ri);
            OSReport("[Main] Debug: granted remote checklist reward mode=%d ri=%d\n", mode, ri);
        }
    }
    if ((pad->down & PAD_BUTTON_DPAD_RIGHT) && ap_data->incoming_item_id == 0)
    {
        ap_data->incoming_item_id = AP_ITEM_METEOR_TRAP;
        OSReport("[Main] Debug: wrote AP_ITEM_METEOR_TRAP to mailbox\n");
    }
    if (pad->down & PAD_BUTTON_DPAD_DOWN)
    {
        ap_data->deathlink_receive = 1;
        OSReport("[Main] Debug: triggered deathlink_receive\n");
    }
    if (pad->down & PAD_BUTTON_DPAD_UP)
    {
        ap_data->traplink_receive = 1;
        OSReport("[Main] Debug: triggered traplink_receive\n");
    }
    if (pad->down & PAD_TRIGGER_Z)
    {
        // In a checklist menu, Z unlocks the currently hovered cell as if
        // the objective had been completed in-game. Z is unused by vanilla
        // checklist navigation (A/X/Y/L/R all cycle screens or place fillers).
        // ClearChecker_SetNewUnlock is REPLACEFUNC'd by check_detection, so
        // the AP check fires and goal evaluation runs. The hovered
        // (mode, clear_kind) is captured by the UpdateCellInfo hook in
        // checklist_rewards.c.
        u8 mode, k;
        if (ChecklistRewards_GetHoveredCell(&mode, &k))
        {
            GameClearData *cd = gmGetClearcheckerTypeP(mode);
            OSReport("[Main] Debug: Z unlock mode=%d clear_kind=%d\n", mode, k);
            ChecklistRewards_LogPlacement(mode, k);
            ClearChecker_SetNewUnlock(mode, k);
            // SetNewUnlock bails when Checklist_IsCacheValid (always true in
            // menus), so RecordCheck ran but no clear[] bits got written. Set
            // the end-state bits directly — the reveal animation only plays
            // on post-mode summary screens anyway, so there's nothing to
            // preserve.
            cd->clear[k].is_new = 1;
            cd->clear[k].is_unlocked = 1;
            cd->clear[k].is_visible = 1;

            // When the "Auto-Grant on Z Unlock" debug toggle is on, also
            // simulate AP item receipt by granting whatever reward is placed
            // at this cell. Remote placements have no local reward to grant
            // (ResolveCell returns 0), so toggle-on is a no-op for them.
            if (DebugMenu_ShouldAutoGrantOnUnlock())
            {
                u8 src_mode, src_ri;
                if (ChecklistRewards_ResolveCell(mode, k, &src_mode, &src_ri))
                {
                    OSReport("[Main] Debug: auto-granting reward mode=%d ri=%d\n",
                             src_mode, src_ri);
                    ChecklistRewards_Grant(src_mode, src_ri);
                    Hoshi_WriteSave();
                }
            }
        }
        else
        {
            OSReport("[Main] Debug: Z pressed but no cell hovered yet (move cursor first)\n");
        }
    }

}

// Runs every game tick after the frame has been processed.
void OnFrameEnd()
{
    
}

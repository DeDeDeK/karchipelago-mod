#include <string.h>
#include <stdio.h>

#include "os.h"
#include "game.h"
#include "scene.h"
#include "hoshi/mod.h"
#include "hoshi/func.h"
#include "stage.h"
#include "stadium.h"
#include "rider.h"
#include "inline.h"

#include "main.h"
#include "version.h"
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
#include "ap_checks.h"
#include "ap_goal.h"
#include "ap_checklist.h"
#include "ap_check_detect.h"
#include "gate_ap_star.h"
#include "gate_stadiums.h"
#include "patch_cap.h"
#include "gate_events.h"
#include "gate_abilities.h"
#include "gate_base_abilities.h"
#include "air_quick_spin.h"
#include "onfoot_zoom.h"
#include "gate_boxes.h"
#include "gate_items.h"
#include "gate_airride_stages.h"
#include "gate_topride_stages.h"
#include "gate_topride_items.h"
#include "gate_colors.h"
#include "spawn_rate.h"
#include "item_spawn_filter.h"
#include "settings_menu.h"
#include "main_menu.h"
#include "goal_max_stats_ct.h"
#include "ap_text.h"
#include "ap_patches.h"

APData *ap_data;
APSave *ap_save;
const CustomMachinesAPI *cm_api = 0;

// Stands in when textbox is absent from the build. Every tb_api-> site in this mod reads
// a color or enqueues a message, and most pass the color as an argument that is evaluated
// before the caller's own message-enabled gate runs, so a null tb_api would fault at far
// more places than could be guarded. The stub turns all of them into a dropped message.
static int TextBoxStub_Enqueue(const char *format, ...) { return 0; }
static int TextBoxStub_EnqueueSegments(const TextSegment *segs, int seg_count) { return 0; }
static int TextBoxStub_EnqueueColoredNoun(const char *prefix, const char *noun, GXColor noun_color,
                                          const char *suffix) { return 0; }
static int TextBoxStub_EnqueueColoredNounFmt(const char *prefix, const char *noun, GXColor noun_color,
                                             const char *suffix_format, ...) { return 0; }
static int TextBoxStub_IsReady(void) { return 0; }

// Widest of the palettes the API hands out, so any index a caller uses lands inside it.
static const GXColor tb_stub_palette[COPYKIND_NUM];

static const TextBoxAPI tb_stub = {
    .Enqueue               = TextBoxStub_Enqueue,
    .EnqueueSegments       = TextBoxStub_EnqueueSegments,
    .EnqueueColoredNoun    = TextBoxStub_EnqueueColoredNoun,
    .EnqueueColoredNounFmt = TextBoxStub_EnqueueColoredNounFmt,
    .IsReady               = TextBoxStub_IsReady,
    .AbilityColors         = tb_stub_palette,
    .KirbyColors           = tb_stub_palette,
    .ModeColors            = tb_stub_palette,
    .PatchColors           = tb_stub_palette,
    .BoxColors             = tb_stub_palette,
};

const TextBoxAPI *tb_api = &tb_stub;

// The AP client hardcodes an offset for every APData field and reads them by
// address, so a silent layout shift desyncs it with no error anywhere. Every field the
// client addresses is pinned, not just the block boundaries - a reorder inside a run of
// same-width fields is otherwise invisible. These and the client's offset table move
// together, never one alone.
_Static_assert(offsetof(APData, energy_balance) == 0x000, "ENERGY_BALANCE");
_Static_assert(offsetof(APData, energy_deposit_total) == 0x008, "ENERGY_DEPOSIT_TOTAL");
_Static_assert(offsetof(APData, energy_withdraw_total) == 0x00C, "ENERGY_WITHDRAW_TOTAL");
_Static_assert(offsetof(APData, deathlink_receive) == 0x010, "DEATHLINK_RECEIVE");
_Static_assert(offsetof(APData, deathlink_send) == 0x014, "DEATHLINK_SEND");
_Static_assert(offsetof(APData, traplink_receive) == 0x018, "TRAPLINK_RECEIVE");
_Static_assert(offsetof(APData, traplink_send) == 0x01C, "TRAPLINK_SEND");
_Static_assert(offsetof(APData, incoming_item_id) == 0x020, "INCOMING_ITEM_ID");
_Static_assert(offsetof(APData, item_received_index) == 0x024, "ITEM_RECEIVED_INDEX");
_Static_assert(offsetof(APData, game_ready) == 0x028, "GAME_READY");
_Static_assert(offsetof(APData, options_valid) == 0x02C, "OPTIONS_VALID");
_Static_assert(offsetof(APData, options) == 0x030, "APSlotOptions block moved");
_Static_assert(offsetof(APData, options.death_link_enabled) == 0x030, "OPTION_DEATH_LINK_ENABLED");
_Static_assert(offsetof(APData, options.energy_link_enabled) == 0x034, "OPTION_ENERGY_LINK_ENABLED");
_Static_assert(offsetof(APData, options.trap_link_enabled) == 0x038, "OPTION_TRAP_LINK_ENABLED");
_Static_assert(offsetof(APData, options.reveal_checklists) == 0x03C, "OPTION_REVEAL_CHECKLIST_AIRRIDE");
_Static_assert(offsetof(APData, options.reveal_checklists[AP_CHECKLIST_ROW]) == 0x048, "OPTION_REVEAL_CHECKLIST_ARCHIPELAGO");
_Static_assert(offsetof(APData, options.goal) == 0x04C, "OPTION_GOAL_AIRRIDE");
_Static_assert(offsetof(APData, options.goal[AP_CHECKLIST_ROW]) == 0x058, "OPTION_GOAL_ARCHIPELAGO");
_Static_assert(offsetof(APData, options.checklist_amount) == 0x05C, "OPTION_CHECKLIST_AMOUNT_AIRRIDE");
_Static_assert(offsetof(APData, options.checklist_amount[AP_CHECKLIST_ROW]) == 0x068, "OPTION_CHECKLIST_AMOUNT_ARCHIPELAGO");
_Static_assert(offsetof(APData, options.city_trial_patch_cap_min) == 0x06C, "OPTION_CT_PATCH_CAP_MIN");
_Static_assert(offsetof(APData, options.city_trial_patch_cap_max) == 0x070, "OPTION_CT_PATCH_CAP_MAX");
_Static_assert(offsetof(APData, options.spawn_rate_min) == 0x074, "OPTION_SPAWN_RATE_MIN");
_Static_assert(offsetof(APData, options.goal_checks) == 0x078, "OPTION_GOAL_CHECKS_AIRRIDE");
_Static_assert(offsetof(APData, options.goal_checks[AP_CHECKLIST_ROW]) == 0x0A8, "OPTION_GOAL_CHECKS_ARCHIPELAGO");
_Static_assert(offsetof(APData, options.machine_gating_enabled) == 0x0B8, "OPTION_MACHINE_GATING_ENABLED");
_Static_assert(offsetof(APData, options.ability_gating_enabled) == 0x0BC, "OPTION_ABILITY_GATING_ENABLED");
_Static_assert(offsetof(APData, options.event_gating_enabled) == 0x0C0, "OPTION_EVENT_GATING_ENABLED");
_Static_assert(offsetof(APData, options.patch_gating_enabled) == 0x0C4, "OPTION_PATCH_GATING_ENABLED");
_Static_assert(offsetof(APData, options.item_gating_enabled) == 0x0C8, "OPTION_ITEM_GATING_ENABLED");
_Static_assert(offsetof(APData, options.box_gating_enabled) == 0x0CC, "OPTION_BOX_GATING_ENABLED");
_Static_assert(offsetof(APData, options.airride_stage_gating_enabled) == 0x0D0, "OPTION_AIRRIDE_STAGE_GATING_ENABLED");
_Static_assert(offsetof(APData, options.topride_stage_gating_enabled) == 0x0D4, "OPTION_TOPRIDE_STAGE_GATING_ENABLED");
_Static_assert(offsetof(APData, options.topride_item_gating_enabled) == 0x0D8, "OPTION_TOPRIDE_ITEM_GATING_ENABLED");
_Static_assert(offsetof(APData, options.color_gating_enabled) == 0x0DC, "OPTION_COLOR_GATING_ENABLED");
_Static_assert(offsetof(APData, options.stadium_gating_enabled) == 0x0E0, "OPTION_STADIUM_GATING_ENABLED");
_Static_assert(offsetof(APData, options.base_ability_gating_enabled) == 0x0E4, "OPTION_BASE_ABILITY_GATING_ENABLED");
_Static_assert(offsetof(APData, options.checklist_reward_placed_types) == 0x0E8, "OPTION_CHECKLIST_REWARD_PLACED_TYPES");
_Static_assert(offsetof(APData, options.goal_forced_gates) == 0x0EC, "OPTION_GOAL_FORCED_GATES");
_Static_assert(offsetof(APData, options.ap_patches) == 0x0F0, "OPTION_AP_PATCHES");
_Static_assert(sizeof(APSlotOptions) == 0x0C8, "APSlotOptions is 200 bytes including its tail padding");
_Static_assert(offsetof(APData, location_data_valid) == 0x0F8, "LOCATION_DATA_VALID");
_Static_assert(offsetof(APData, locations) == 0x0FC, "LOCATIONS_AIRRIDE");
_Static_assert(offsetof(APData, locations[GMMODE_TOPRIDE]) == 0x158, "LOCATIONS_TOPRIDE");
_Static_assert(offsetof(APData, locations[GMMODE_CITYTRIAL]) == 0x1B4, "LOCATIONS_CITYTRIAL");
_Static_assert(offsetof(APData, sent_checks) == 0x210, "SENT_CHECKS_AIRRIDE");
_Static_assert(offsetof(APData, sent_checks[GMMODE_TOPRIDE]) == 0x220, "SENT_CHECKS_TOPRIDE");
_Static_assert(offsetof(APData, sent_checks[GMMODE_CITYTRIAL]) == 0x230, "SENT_CHECKS_CITYTRIAL");
_Static_assert(offsetof(APData, sent_checks[AP_CHECKLIST_ROW]) == 0x240, "SENT_CHECKS_ARCHIPELAGO");
_Static_assert(offsetof(APData, client_backfill) == 0x250, "CLIENT_BACKFILL_AIRRIDE");
_Static_assert(offsetof(APData, client_backfill[GMMODE_TOPRIDE]) == 0x260, "CLIENT_BACKFILL_TOPRIDE");
_Static_assert(offsetof(APData, client_backfill[GMMODE_CITYTRIAL]) == 0x270, "CLIENT_BACKFILL_CITYTRIAL");
_Static_assert(offsetof(APData, client_backfill[AP_CHECKLIST_ROW]) == 0x280, "CLIENT_BACKFILL_ARCHIPELAGO");
_Static_assert(offsetof(APData, goal_complete) == 0x290, "GOAL_COMPLETE");
_Static_assert(offsetof(APData, goal_satisfied_mask) == 0x291, "GOAL_SATISFIED_MASK");
_Static_assert(CHECKLIST_MODE_NUM <= 8, "goal_satisfied_mask is one byte");
_Static_assert(offsetof(APData, deathlink_menu_enabled) == 0x294, "DEATHLINK_MENU_ENABLED");
_Static_assert(offsetof(APData, energylink_menu_enabled) == 0x298, "ENERGYLINK_MENU_ENABLED");
_Static_assert(offsetof(APData, traplink_menu_enabled) == 0x29C, "TRAPLINK_MENU_ENABLED");
_Static_assert(offsetof(APData, text_pending) == 0x2A0, "TEXT_PENDING");
_Static_assert(offsetof(APData, text_menu_mask) == 0x2A4, "TEXT_MENU_MASK");
_Static_assert(offsetof(APData, text_msg) == 0x2A8, "TEXT_MSG");
_Static_assert(offsetof(APData, ap_patch_checks) == 0x3A8, "AP_PATCH_CHECKS");
_Static_assert(offsetof(APData, ap_patch_backfill) == 0x3E8, "AP_PATCH_BACKFILL");
_Static_assert(offsetof(APData, backfill_valid) == 0x428, "BACKFILL_VALID");

int ap_checklist_mode = GMMODE_NUM;
int ap_regrant_quiet = 0;

ModDesc mod_desc = {
    .name = "KARchipelago",
    .author = "DeDeDK",
    .affects_gameplay = 1,
    // Hoshi reads these to decide whether a backed-up save block is still readable,
    // so they track APSave's shape, not the exported API's.
    .version.major = APSAVE_VERSION_MAJOR,
    .version.minor = APSAVE_VERSION_MINOR,
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
    .OnTopRideLoadEnd = OnTopRideLoadEnd,
};


// Runs immediately after the mod file is loaded. HSD_MemAlloc calls made here
// persist for the whole runtime; anywhere else they last only the current scene.
void OnBoot()
{
    OSReport("[Main] KARchipelago %s\n", KARCHIPELAGO_VERSION);

    ap_data = HSD_MemAlloc(sizeof(APData));
    memset(ap_data, 0, sizeof(APData));
    OSReport("[Main] APData at 0x%08x (%d bytes)\n", (uint)ap_data, sizeof(APData));

    APData **anchor = (APData **)AP_DATA_ANCHOR;
    (*anchor) = ap_data;

    // Give the shared hoshi memory-card file an Archipelago tile. The art is loaded
    // from disc when the save is created, so no image is baked into this mod. Banner
    // after icon: the icon call clears the tile.
    Hoshi_SetSaveIconFile("KARchipelago", "Save Data", "ApIcon", 1, CARD_STAT_SPEED_MIDDLE);
    Hoshi_SetSaveBannerFile("ApBanner");

    ChecklistRewards_OnBoot();

    // After ChecklistRewards_OnBoot: both patch Checklist_ProcessUnlock, and the
    // reward hooks have to be in place before the unlock interceptions run.
    APChecks_OnBoot();
    APGoal_OnBoot();

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
    ApPatches_OnBoot();

    ArchipelagoAPI_Export();
}

void OnSaveInit()
{
    ap_save = (APSave *)mod_desc.save_ptr;
    OSReport("[Main] Save block created (%d bytes)\n", sizeof(APSave));
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

static void AP_ResolveCustomMachines(void)
{
    cm_api = (const CustomMachinesAPI *)Hoshi_ImportMod(
        (char *)CUSTOM_MACHINES_MOD_NAME, CUSTOM_MACHINES_API_MAJOR, CUSTOM_MACHINES_API_MINOR);
    if (!cm_api)
        return;

    // The registry owns both select screens' packing and the City Trial field spawn
    // roll; these are what make it offer the unlocked roster rather than the
    // engine's own.
    cm_api->SetAvailabilityFilter(GateMachines_FilterSelectCharacter);
    cm_api->SetSpawnWeightFilter(GateMachines_SpawnWeight);

    // It owns the engine's only KO recorder call too, which is where the Destruction
    // Derby check that counts KO'd Kirbys reads the victim from.
    cm_api->AddDeathHandler(APCheckDetect_AddDeath);
    OSReport("[Main] custom_machines: %d machine(s), %d kinds, %d characters\n",
             cm_api->GetCount(), cm_api->GetKindCeiling(),
             cm_api->GetCharacterKindCeiling());
}

// Runs on startup after any save data is loaded, whether or not a memory card is
// inserted or held existing save data.
void OnSaveLoaded()
{
    ap_save = (APSave *)mod_desc.save_ptr;

    if (tb_api == &tb_stub)
    {
        const TextBoxAPI *imported = (const TextBoxAPI *)Hoshi_ImportMod(
            (char *)TEXTBOX_MOD_NAME, TEXTBOX_API_MAJOR, TEXTBOX_API_MINOR);
        if (imported)
            tb_api = imported;
        else
            OSReport("[Main] textbox missing from this build: notifications are dropped\n");
    }

    AP_ResolveCustomMachines();
    GateApStar_Resolve();

    // First point past every mod's OnBoot, so a still-unresolved import here really
    // does mean the registry is not in this build. It owns the widened kind space
    // and the seams that widening breaks - both select screens' packing, the City
    // Trial spawn roll, the assembly cutscene - all of which this mod gates through
    // filters it takes, so without it machines go ungated rather than half-gated.
    if (!cm_api)
        OSReport("[Main] custom_machines missing from this build: MACHINE GATING IS OFF\n");

    ap_save->boot_num++;

    OSReport("[Main] Boot #%d, %d items received, options %s\n",
             ap_save->boot_num, ap_save->item_received_count,
             ap_save->options_received ? "loaded" : "pending");

    ap_data->item_received_index = ap_save->item_received_count;

    // Ahead of the reward regrant, which marks cells on the AP tab and so needs its
    // clear data to exist.
    APChecklist_Register();

    ChecklistRewards_OnSaveLoaded();

    // Without this the client reads zeros after a reboot and re-sends the whole
    // AP Patch category as if nothing had been collected.
    ApPatches_OnSaveLoaded();

    // Mirrors sent_checks/goal_complete into shared memory and runs the initial
    // goal evaluation.
    APChecks_OnSaveLoaded();

    // Re-applied every boot, not just at option transfer: the vanilla modes' reveal
    // rides along in the game's own clear data, but the AP tab's cells live in RAM
    // and come up blank.
    if (ap_save->options_received)
        APOptions_ApplyRevealChecklists();

    // Hoshi's Mod_CopyFromSave has run by now, so ap_menu_settings reflects the
    // player's persisted toggle choices.
    SyncMenuStateToAPData();

    ap_data->game_ready = 1;
    OSReport("[Main] game_ready set - waiting for AP client connection\n");
}

static void AppendCsv(char *buf, int *pos, const char *name)
{
    if (*pos)
    {
        buf[(*pos)++] = ',';
        buf[(*pos)++] = ' ';
    }
    while (*name)
        buf[(*pos)++] = *name++;
    buf[*pos] = '\0';
}

// buf holds the count goal's threshold, so callers need one buffer per goal named
// in the same line.
static const char *GoalName(const APSlotOptions *opts, int row, char *buf)
{
    static const char *const names[] = {
        [GOAL_100_CHECKLIST]      = "100 squares",
        [GOAL_N_CHECKLIST]        = "N squares",
        [GOAL_CHECKLIST_LIST]     = "listed squares",
        [GOAL_HYDRA_AND_DRAGOON]  = "Hydra + Dragoon",
        [GOAL_BEAT_KING_DEDEDE]   = "beat King Dedede",
        [GOAL_MAX_STATS_CT]       = "max stats",
        [GOAL_ASSEMBLE_AP_STAR]   = "assemble AP Star",
        [GOAL_ALL_LEGENDARIES_CT] = "all legendaries",
        [GOAL_NONE]               = "none",
    };
    u32 goal = opts->goal[row];

    if (goal >= sizeof(names) / sizeof(names[0]))
        return "?";
    if (goal == GOAL_N_CHECKLIST)
    {
        sprintf(buf, "%d squares", opts->checklist_amount[row]);
        return buf;
    }
    return names[goal];
}

static const char *ChecklistRowName(int row)
{
    static const char *const names[CHECKLIST_MODE_NUM] = {
        [GMMODE_AIRRIDE]     = "Air Ride",
        [GMMODE_TOPRIDE]     = "Top Ride",
        [GMMODE_CITYTRIAL]   = "City Trial",
        [AP_CHECKLIST_ROW]   = AP_CHECKLIST_NAME,
    };
    return names[row];
}

// For any category whose slot option marks gating as disabled, pre-fill the unlock
// mask with all-1s: the AP world ships no unlock items for ungated categories. This
// bypasses the GateX_UnlockY textbox path, so connecting does not flood the screen.
static void APOptions_ApplyUngatedCategories(void)
{
    const APSlotOptions *opts = &ap_save->options;

    // A goal whose win condition is one in-game feat is free at connect when the
    // category holding that feat is ungated, so the AP world keeps its unlocks in
    // the pool and marks them here. They stay locked until their item arrives.
    u32 item_mask = (1u << ITUNLOCK_NUM) - 1;
    if (opts->goal_forced_gates & GOALGATE_LEGENDARY_PIECES)
        item_mask &= ~LEGENDARY_PIECE_ITEM_BITS;
    u32 stadium_mask = (1u << STKIND_NUM) - 1;
    if (opts->goal_forced_gates & GOALGATE_VS_KING_DEDEDE)
        stadium_mask &= ~(1u << STKIND_VSKINGDEDEDE);
    u32 star_piece_mask = AP_STAR_PIECE_ITEM_BITS;
    if (opts->goal_forced_gates & GOALGATE_AP_STAR_PIECES)
        star_piece_mask = 0;

    if (!opts->machine_gating_enabled)       Unlock_SetMask(AP_UNLOCK_MACHINE,       (1u << AP_MACHINE_BIT_NUM) - 1);
    if (!opts->ability_gating_enabled)       Unlock_SetMask(AP_UNLOCK_ABILITY,       (1u << COPYKIND_NUM) - 1);
    if (!opts->event_gating_enabled)         Unlock_SetMask(AP_UNLOCK_EVENT,         (1u << EVKIND_NUM) - 1);
    if (!opts->patch_gating_enabled)         Unlock_SetMask(AP_UNLOCK_PATCH,         (1u << PATCHKIND_NUM) - 1);
    if (!opts->item_gating_enabled)          Unlock_SetMask(AP_UNLOCK_ITEM,          item_mask);
    if (!opts->item_gating_enabled)          Unlock_SetMask(AP_UNLOCK_AP_STAR_PIECE, star_piece_mask);
    if (!opts->box_gating_enabled)           Unlock_SetMask(AP_UNLOCK_BOX,           (1u << BOXKIND_NUM) - 1);
    if (!opts->airride_stage_gating_enabled) Unlock_SetMask(AP_UNLOCK_AIRRIDE_STAGE, (1u << AIRRIDE_NUM) - 1);
    if (!opts->topride_stage_gating_enabled) Unlock_SetMask(AP_UNLOCK_TOPRIDE_STAGE, (1u << TOPRIDE_NUM) - 1);
    if (!opts->topride_item_gating_enabled)  Unlock_SetMask(AP_UNLOCK_TOPRIDE_ITEM,  (1u << TRITEM_NUM) - 1);
    if (!opts->color_gating_enabled)         Unlock_SetMask(AP_UNLOCK_COLOR,         (1u << KIRBYCOLOR_NUM) - 1);
    if (!opts->stadium_gating_enabled)       Unlock_SetMask(AP_UNLOCK_STADIUM,       stadium_mask);
    if (!opts->base_ability_gating_enabled)  Unlock_SetMask(AP_UNLOCK_BASE_ABILITY,  (1u << BASEABILITY_NUM) - 1);

    // The three TR "New Item" types (Chickie/Who? Paint/Lantern) aren't reachable
    // via the mask - the engine enables them only when their checklist reward is
    // received. Mark TR reward indices 8-10 received so an ungated world gets them.
    if (!opts->topride_item_gating_enabled)
        for (u8 ri = 8; ri <= 10; ri++)
            ap_save->received_checklist_rewards[GMMODE_TOPRIDE] |= (1ULL << ri);

    // Placeable rewards are tracked by received_checklist_rewards, not a gate mask.
    ChecklistRewards_GrantUnplaced(opts->checklist_reward_placed_types);

    static const char *const gate_names[] = {
        "machines", "abilities", "events", "patches", "items", "boxes",
        "AR stages", "TR stages", "TR items", "colors", "stadiums",
        "base abilities",
    };
    const u8 gate_flags[] = {
        opts->machine_gating_enabled, opts->ability_gating_enabled,
        opts->event_gating_enabled, opts->patch_gating_enabled,
        opts->item_gating_enabled, opts->box_gating_enabled,
        opts->airride_stage_gating_enabled, opts->topride_stage_gating_enabled,
        opts->topride_item_gating_enabled, opts->color_gating_enabled,
        opts->stadium_gating_enabled, opts->base_ability_gating_enabled,
    };

    char list[224];
    int n = 0;
    for (int i = 0; i < (int)(sizeof(gate_flags) / sizeof(gate_flags[0])); i++)
        if (!gate_flags[i])
            AppendCsv(list, &n, gate_names[i]);
    OSReport("[Main] Gating on, except: %s\n", n ? list : "nothing");

    n = 0;
    if (opts->goal_forced_gates & GOALGATE_LEGENDARY_PIECES) AppendCsv(list, &n, "legendary pieces");
    if (opts->goal_forced_gates & GOALGATE_VS_KING_DEDEDE)   AppendCsv(list, &n, "Vs. King Dedede");
    if (opts->goal_forced_gates & GOALGATE_AP_STAR_PIECES)   AppendCsv(list, &n, "AP Star spheres");
    if (n)
        OSReport("[Main] Gating forced by the goal: %s\n", list);
}

// Options are immutable per AP slot, so the copy runs once per save file. Every client
// write is still acknowledged by clearing options_valid with the menu mirrors already
// republished - the client treats that clear as the signal that the mirrors are the
// player's choices rather than the save's defaults.
static void APOptions_TransferToSave()
{
    if (!ap_data->options_valid)
        return;

    int first_transfer = !ap_save->options_received;
    if (first_transfer)
    {
        OSReport("[Main] AP client connected - slot options transferred to save\n");
        memcpy(&ap_save->options, &ap_data->options, sizeof(APSlotOptions));
        ap_save->options_received = 1;

        ap_menu_settings.deathlink_enabled = ap_save->options.death_link_enabled;
        ap_menu_settings.energylink_enabled = ap_save->options.energy_link_enabled;
        ap_menu_settings.traplink_enabled = ap_save->options.trap_link_enabled;
    }

    SyncMenuStateToAPData();
    ap_data->options_valid = 0;

    if (!first_transfer)
        return;

    const APSlotOptions *opts = &ap_save->options;
    char list[224];
    int n = 0;
    if (opts->death_link_enabled)  AppendCsv(list, &n, "DeathLink");
    if (opts->energy_link_enabled) AppendCsv(list, &n, "EnergyLink");
    if (opts->trap_link_enabled)   AppendCsv(list, &n, "TrapLink");
    OSReport("[Main] Links on: %s\n", n ? list : "none");

    char goals[CHECKLIST_MODE_NUM][24];
    OSReport("[Main] Goals - AirRide: %s, TopRide: %s, CityTrial: %s, %s: %s\n",
             GoalName(opts, GMMODE_AIRRIDE, goals[GMMODE_AIRRIDE]),
             GoalName(opts, GMMODE_TOPRIDE, goals[GMMODE_TOPRIDE]),
             GoalName(opts, GMMODE_CITYTRIAL, goals[GMMODE_CITYTRIAL]),
             AP_CHECKLIST_NAME, GoalName(opts, AP_CHECKLIST_ROW, goals[AP_CHECKLIST_ROW]));

    n = 0;
    for (int r = 0; r < CHECKLIST_MODE_NUM; r++)
        if (opts->reveal_checklists[r])
            AppendCsv(list, &n, ChecklistRowName(r));
    OSReport("[Main] CT patch cap %d-%d, checklists pre-revealed: %s\n",
             opts->city_trial_patch_cap_min, opts->city_trial_patch_cap_max,
             n ? list : "none");

    APOptions_ApplyRevealChecklists();

    APOptions_ApplyUngatedCategories();

    Hoshi_WriteSave();
    OSReport("[Main] AP slot options saved to memory card\n");
}

// The 12 gating flags in APUnlockCategory order. AP_UNLOCK_AP_STAR_PIECE has no flag
// of its own - the spheres follow the item category.
static u32 *GatingFlag(APUnlockCategory cat)
{
    APSlotOptions *o = &ap_save->options;

    switch (cat)
    {
    case AP_UNLOCK_MACHINE:       return &o->machine_gating_enabled;
    case AP_UNLOCK_ABILITY:       return &o->ability_gating_enabled;
    case AP_UNLOCK_EVENT:         return &o->event_gating_enabled;
    case AP_UNLOCK_PATCH:         return &o->patch_gating_enabled;
    case AP_UNLOCK_ITEM:          return &o->item_gating_enabled;
    case AP_UNLOCK_BOX:           return &o->box_gating_enabled;
    case AP_UNLOCK_AIRRIDE_STAGE: return &o->airride_stage_gating_enabled;
    case AP_UNLOCK_TOPRIDE_STAGE: return &o->topride_stage_gating_enabled;
    case AP_UNLOCK_TOPRIDE_ITEM:  return &o->topride_item_gating_enabled;
    case AP_UNLOCK_COLOR:         return &o->color_gating_enabled;
    case AP_UNLOCK_STADIUM:       return &o->stadium_gating_enabled;
    case AP_UNLOCK_BASE_ABILITY:  return &o->base_ability_gating_enabled;
    default:                      return 0;
    }
}

// Normalized, because the menu row that renders it indexes a two-entry name table
// with no bounds check of its own.
int APOptions_DebugGetGating(APUnlockCategory cat)
{
    u32 *flag = GatingFlag(cat);
    return (!flag || *flag) ? 1 : 0;
}

void APOptions_DebugSetGating(APUnlockCategory cat, int enabled)
{
    u32 *flag = GatingFlag(cat);
    if (flag)
        *flag = enabled ? 1u : 0u;
}

void APOptions_GetPatchCapRange(int *out_min, int *out_max)
{
    if (out_min)
        *out_min = (int)ap_save->options.city_trial_patch_cap_min;
    if (out_max)
        *out_max = (int)ap_save->options.city_trial_patch_cap_max;
}

int APOptions_GetSpawnRateMin(void)
{
    return (int)ap_save->options.spawn_rate_min;
}

// One bound each: the menu offers fixed steps and shows the nearest at or below the
// live value, so writing both from the two rows would round the untouched one down.
void APOptions_DebugSetPatchCapMin(int min)
{
    ap_save->options.city_trial_patch_cap_min = (u32)min;
}

void APOptions_DebugSetPatchCapMax(int max)
{
    ap_save->options.city_trial_patch_cap_max = (u32)max;
}

void APOptions_DebugSetSpawnRateMin(int percent)
{
    ap_save->options.spawn_rate_min = (u32)percent;
}

// The pre-fill only ever sets bits, so a category whose gating was turned back on
// would keep the all-ones mask its ungated pass gave it. Clearing every mask first
// is what makes this a fresh connect rather than an addition to the last one.
// received_checklist_rewards is deliberately left alone: it also holds rewards the
// player genuinely received, and clearing those is the Checks page's job.
void APOptions_DebugReapply(void)
{
    // Every flag reads 0 before a connect, which the pre-fill treats as ungated, so
    // this unlocks the whole seed rather than reproducing any particular one.
    if (!ap_save->options_received)
        OSReport("[Main] No slot options received - every category re-applies as ungated\n");

    for (int cat = 0; cat < AP_UNLOCK_NUM; cat++)
        Unlock_SetMask((APUnlockCategory)cat, 0);

    APOptions_ApplyRevealChecklists();
    APOptions_ApplyUngatedCategories();

    Hoshi_WriteSave();
}

static const char *const unlock_cat_names[AP_UNLOCK_NUM] = {
    [AP_UNLOCK_MACHINE]       = "machines",
    [AP_UNLOCK_ABILITY]       = "abilities",
    [AP_UNLOCK_EVENT]         = "events",
    [AP_UNLOCK_PATCH]         = "patch types",
    [AP_UNLOCK_ITEM]          = "CT items",
    [AP_UNLOCK_BOX]           = "boxes",
    [AP_UNLOCK_AIRRIDE_STAGE] = "AR stages",
    [AP_UNLOCK_TOPRIDE_STAGE] = "TR stages",
    [AP_UNLOCK_TOPRIDE_ITEM]  = "TR items",
    [AP_UNLOCK_COLOR]         = "colors",
    [AP_UNLOCK_STADIUM]       = "stadiums",
    [AP_UNLOCK_BASE_ABILITY]  = "base abilities",
    [AP_UNLOCK_AP_STAR_PIECE] = "AP Star spheres",
};

// Bits worth printing per category.
static int UnlockCatBits(APUnlockCategory cat)
{
    static const u8 bits[AP_UNLOCK_NUM] = {
        [AP_UNLOCK_MACHINE]       = AP_MACHINE_BIT_NUM,
        [AP_UNLOCK_ABILITY]       = COPYKIND_NUM,
        [AP_UNLOCK_EVENT]         = EVKIND_NUM,
        [AP_UNLOCK_PATCH]         = PATCHKIND_NUM,
        [AP_UNLOCK_ITEM]          = ITUNLOCK_NUM,
        [AP_UNLOCK_BOX]           = BOXKIND_NUM,
        [AP_UNLOCK_AIRRIDE_STAGE] = AIRRIDE_NUM,
        [AP_UNLOCK_TOPRIDE_STAGE] = TOPRIDE_NUM,
        [AP_UNLOCK_TOPRIDE_ITEM]  = TRITEM_NUM,
        [AP_UNLOCK_COLOR]         = KIRBYCOLOR_NUM,
        [AP_UNLOCK_STADIUM]       = STKIND_NUM,
        [AP_UNLOCK_BASE_ABILITY]  = BASEABILITY_NUM,
        [AP_UNLOCK_AP_STAR_PIECE] = AP_STAR_PIECE_NUM,
    };

    return bits[cat];
}

void APDebug_ReportState(void)
{
    OSReport("[Main] Boot %d, %d items received, %d queued, options received %d\n",
             ap_save->boot_num, ap_save->item_received_count,
             ap_save->unprocessed_count, ap_save->options_received);

    for (int cat = 0; cat < AP_UNLOCK_NUM; cat++)
    {
        int bits = UnlockCatBits((APUnlockCategory)cat);
        OSReport("[Main] %s gated %d, mask %s\n", unlock_cat_names[cat],
                 APOptions_DebugGetGating((APUnlockCategory)cat),
                 MaskBits(Unlock_GetMask((APUnlockCategory)cat), bits));
    }

    OSReport("[Main] Patch cap %d (%d received, seed range %d-%d), spawn rate floor %d%%\n",
             PatchCap_GetCap(), ap_save->patch_cap_count,
             ap_save->options.city_trial_patch_cap_min,
             ap_save->options.city_trial_patch_cap_max,
             ap_save->options.spawn_rate_min);

    char perm[64];
    int n = 0;
    for (int i = 0; i < PATCHKIND_NUM; i++)
        n += sprintf(&perm[n], "%s%d", i ? " " : "", ap_save->permanent_patches[i]);
    OSReport("[Main] Permanent patches (PatchKind order): %s\n", perm);

    char goal_buf[24];
    for (int r = 0; r < CHECKLIST_MODE_NUM; r++)
        OSReport("[Main] %s: goal %s, %d squares done, announced %d\n",
                 ChecklistRowName(r), GoalName(&ap_save->options, r, goal_buf),
                 APChecks_PopcountRow(r), ap_save->goal_announced[r]);

    OSReport("[Main] goal_complete %d, satisfied %s, max stats CT %d\n",
             ap_save->goal_complete,
             MaskBits(ap_data->goal_satisfied_mask, CHECKLIST_MODE_NUM),
             ap_save->max_stats_ct_achieved);

    OSReport("[Main] AP Patches %d collected of %d, %d left\n",
             ApPatches_CollectedCount(), ApPatches_GetCount(), ApPatches_Remaining());

    OSReport("[Main] Energy %lld MJ, %u deposited, %u withdrawn this boot\n",
             ap_data->energy_balance, ap_data->energy_deposit_total,
             ap_data->energy_withdraw_total);

    OSReport("[Main] Check progress: %d/%d All Ups, %d/%d purple SR1 wins, race colors %s\n",
             ap_save->checks.allup_collect_total, AP_ALLUP_TOTAL_NEED,
             ap_save->checks.purple_sr1_wins, AP_PURPLE_SR1_NEED,
             MaskBits(ap_save->checks.race_color_mask, KIRBYCOLOR_NUM));
}

// Permanent patches already applied to a rider stay on it for the rest of the round -
// the apply latch is per-scene - so the effect of this shows from the next round load.
void APDebug_ResetProgression(void)
{
    ap_save->patch_cap_count = 0;
    ap_save->spawn_rate_level = 0;
    for (int i = 0; i < PATCHKIND_NUM; i++)
        ap_save->permanent_patches[i] = 0;

    ap_save->checks.allup_collect_total = 0;
    ap_save->checks.purple_sr1_wins = 0;
    ap_save->checks.race_color_mask = 0;
    ap_save->max_stats_ct_achieved = 0;

    ap_save->item_received_count = 0;
    ap_save->unprocessed_count = 0;
    ap_data->item_received_index = 0;

    Hoshi_WriteSave();
    OSReport("[Main] Progression reset to pre-connect: patch cap, spawn rate, "
             "permanent patches, check progress and the item queue\n");
}

void OnMainMenuLoad()
{
    OSReport("[Main] Entering the main menu\n");
}

void OnPlayerSelectLoad()
{
    OSReport("[Main] Entering player select (minor %d)\n", Scene_GetCurrentMinor());

    // City Trial colors persist from prior sessions and have no init block to
    // hook like AR/TR, so validate on every CSS load.
    if (Scene_GetCurrentMinor() == MNRKIND_CITYPLYSELECT)
        GateColors_ValidateCityTrialColors();
}

// Runs before a 3D game is initialized. Early enough to hold a custom item out
// of the round's registry, which is written at CityItemSpawn_Init.
void On3DLoadStart()
{
    GoalMaxStatsCT_On3DLoadStart();
    ApPatches_On3DLoadStart();
}

// Runs upon entering a 3D game (Air Ride, Top Ride, or City Trial).
// Players, riders, their machines, and the map have all been instantiated.
void On3DLoadEnd()
{
    static const char *const ar_mode_names[] = { "Race", "Time Attack", "Free Run" };
    static const char *const city_mode_names[] = { "Trial", "Stadium", "Free Run" };
    const char *mode_label;
    const char *what;

    // Gm_IsInCity() only returns true on the CT main map (stage_kind 9/52);
    // stadiums load their own stages and would be misreported as Air Ride.
    // The CT major (MJRKIND_CITY) covers Trial, Free Run, and all stadiums.
    if (Scene_GetCurrentMajor() == MJRKIND_CITY)
    {
        CityMode cm = Gm_GetCityMode();
        what = "City Trial";
        if (cm == CITYMODE_STADIUM)
        {
            StadiumKind sk = Gm_GetCurrentStadiumKind();
            mode_label = ((unsigned)sk < STKIND_NUM) ? StadiumKind_Names[sk] : "?";
        }
        else
            mode_label = ((unsigned)cm < 3) ? city_mode_names[cm] : "?";
    }
    else
    {
        AirRideMode ar_mode = Gm_GetAirRideMode();
        what = "Air Ride";
        mode_label = ((unsigned)ar_mode < 3) ? ar_mode_names[ar_mode] : "?";
    }

    OSReport("[Main] Starting %s: %s Ground=%d Stage=%d CityMode=%d Stadium=%d(%d) Damage=%d ItemData=%d\n",
             what, mode_label, Gr_GetCurrentGrKind(), Gm_GetCurrentStageKind(),
             Gm_GetCityMode(), Gm_GetCurrentStadiumKind(),
             Gm_GetCurrentStadiumGroup(), Gm_IsDamageEnabled(), Item_CheckIsLoaded());

    char roster[160];
    int n = 0;
    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) == PKIND_NONE)
            continue;

        GOBJ *rg = Ply_GetRiderGObj(i);
        RiderData *rd = rg->userdata;
        n += sprintf(&roster[n], "%sP%d r%d/c%d/m%d", n ? ", " : "",
                     i + 1, rd->kind, rd->color_idx, rd->starting_machine_idx);
    }
    if (n)
        OSReport("[Main] Players - %s\n", roster);

    if (Gm_IsAutoDemo())
        OSReport("[Main] Title attract demo round - no check counts toward the seed\n");

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
    ApPatches_On3DLoadEnd();
    KirbyScale_On3DLoadEnd();
    DropAbility_On3DLoadEnd();
}

// Top Ride uses minor 19 (not 18), so On3DLoadEnd does not fire for it.
void OnTopRideLoadEnd()
{
    static const char *const tr_mode_names[] = { "Race", "Time Attack", "Free Run" };
    TopRideMode tr_mode = TopRide_GetMode();
    const char *tr_mode_name = ((unsigned)tr_mode < 3) ? tr_mode_names[tr_mode] : "?";
    OSReport("[Main] Starting Top Ride: %s\n", tr_mode_name);

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
    OSReport("[Main] Paused by player %d\n", pause_ply + 1);
}

void On3DUnpause(int pause_ply)
{
    OSReport("[Main] Unpaused by player %d\n", pause_ply + 1);
}

void On3DExit()
{
    OSReport("[Main] Exiting 3D\n");

    // Stadium_ExitMinor has finished latching GameData.stadium_results by this
    // point, so the round's placements and times are final and readable here.
    APCheckDetect_On3DExit();
    ApPatches_On3DExit();
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
    if (!ap_save)
        return;

    APOptions_TransferToSave();

    // ChecklistRewards_ApplyLocations clears location_data_valid and persists, so
    // this fires once per client write, i.e. on every (re)connection.
    if (ap_data->location_data_valid)
        ChecklistRewards_ApplyLocations();

    // One flag publishes both backfill arrays, so neither is read half-written.
    if (ap_data->backfill_valid)
    {
        APChecks_ApplyBackfill();
        ApPatches_ApplyBackfill();
        ap_data->backfill_valid = 0;
    }

    APCheckDetect_OnFrameStart();
    APText_OnFrameStart();
}

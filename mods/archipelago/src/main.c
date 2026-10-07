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
#include "topride.h"
#include "inline.h"

#include "main.h"
#include "version.h"
#include "ap_options.h"
#include "gate_machines.h"
#include "deathlink.h"
#include "ap_item_handler.h"
#include "kirby_scale.h"
#include "drop_ability.h"
#include "energylink.h"
#include "traplink.h"
#include "fake_patches.h"
#include "permanent_patch.h"
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
#include "ap_title.h"
#include "goal_max_stats_ct.h"
#include "ap_text.h"
#include "ap_patches.h"

APData *ap_data;
APSave *ap_save;
const CustomMachinesAPI *cm_api = 0;

// Stands in when textbox is absent, so no tb_api-> site needs a null guard.
static int TextBoxStub_Enqueue(const char *format, ...) { return 0; }
static int TextBoxStub_EnqueueSegments(const TextSegment *segs, int seg_count) { return 0; }
static int TextBoxStub_EnqueueColoredNoun(const char *prefix, const char *noun, GXColor noun_color,
                                          const char *suffix) { return 0; }
static int TextBoxStub_EnqueueColoredNounFmt(const char *prefix, const char *noun, GXColor noun_color,
                                             const char *suffix_format, ...) { return 0; }

// Widest of the palettes the API hands out, so any index a caller uses lands inside it.
static const GXColor tb_stub_palette[COPYKIND_NUM];

static const TextBoxAPI tb_stub = {
    .Enqueue               = TextBoxStub_Enqueue,
    .EnqueueSegments       = TextBoxStub_EnqueueSegments,
    .EnqueueColoredNoun    = TextBoxStub_EnqueueColoredNoun,
    .EnqueueColoredNounFmt = TextBoxStub_EnqueueColoredNounFmt,
    .AbilityColors         = tb_stub_palette,
    .KirbyColors           = tb_stub_palette,
    .ModeColors            = tb_stub_palette,
    .PatchColors           = tb_stub_palette,
    .BoxColors             = tb_stub_palette,
};

const TextBoxAPI *tb_api = &tb_stub;

_Static_assert(CHECKLIST_MODE_NUM <= 8, "goal_satisfied_mask is one byte");

int ap_checklist_mode = GMMODE_NUM;
int ap_regrant_quiet = 0;

static void OnBoot(void);
static void OnSaveInit(void);
static void OnSaveLoaded(void);
static void OnMainMenuLoad(void);
static void OnPlayerSelectLoad(void);
static void On3DLoadEnd(void);
static void On3DPause(int pause_ply);
static void On3DUnpause(int pause_ply);
static void On3DExit(void);
static void OnSceneChange(void);
static void OnTopRideLoadEnd(void);
static void OnFrameStart(void);

ModDesc mod_desc = {
    .name = ARCHIPELAGO_MOD_NAME,
    .author = "DeDeDK",
    .affects_gameplay = 1,
    // hoshi checks an importer's API major against this.
    .version.major = ARCHIPELAGO_API_MAJOR,
    .version.minor = ARCHIPELAGO_API_MINOR,
    .save_size = sizeof(struct APSave),
    .save_ptr = 0,
    .option_desc = &ModSettings,
    .OnBoot = OnBoot,
    .OnSaveInit = OnSaveInit,
    .OnSaveLoaded = OnSaveLoaded,
    .OnMainMenuLoad = OnMainMenuLoad,
    .OnPlayerSelectLoad = OnPlayerSelectLoad,
    .On3DLoadStart = APPatches_On3DLoadStart,
    .On3DLoadEnd = On3DLoadEnd,
    .On3DPause = On3DPause,
    .On3DUnpause = On3DUnpause,
    .On3DExit = On3DExit,
    .OnSceneChange = OnSceneChange,
    .OnFrameStart = OnFrameStart,
    .OnTopRideLoadEnd = OnTopRideLoadEnd,
};

int AP_AttachHumanRiderProcs(void (*proc)(GOBJ *))
{
    int attached = 0;
    for (int i = 0; i < PLY_NUM; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;
        GOBJ *rg = Ply_GetRiderGObj(i);
        if (!rg)
            continue;
        GObj_AddProc(rg, proc, RDPRI_HITCOLL + 1);
        attached++;
    }
    return attached;
}

// HSD_MemAlloc made here persists for the whole run; anywhere else it lasts a scene.
static void OnBoot(void)
{
    OSReport("[Main] KARchipelago %s\n", KARCHIPELAGO_VERSION);

    ap_data = HSD_MemAlloc(sizeof(APData));
    memset(ap_data, 0, sizeof(APData));
    OSReport("[Main] APData at 0x%08x (%d bytes)\n", (uint)ap_data, sizeof(APData));

    APData **anchor = (APData **)AP_DATA_ANCHOR;
    (*anchor) = ap_data;

    // The art is read from disc when the save is created. Banner after icon: the icon
    // call clears the tile.
    Hoshi_SetSaveIconFile("KARchipelago", "Save Data", "ApIcon", 1, CARD_STAT_SPEED_MIDDLE);
    Hoshi_SetSaveBannerFile("ApBanner");

    // Both of these patch Checklist_ProcessUnlock; the reward hooks go in first.
    ChecklistRewards_OnBoot();
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
    APTitle_OnBoot();
    APPatches_OnBoot();

    ArchipelagoAPI_Export();
}

static void APSave_Init(void)
{
    memset(ap_save, 0, sizeof(*ap_save));
    ap_save->stamp = APSAVE_STAMP;
    ChecklistRewards_OnSaveInit();
}

// Runs every boot on hoshi's default block, before the card is read.
static void OnSaveInit(void)
{
    ap_save = (APSave *)mod_desc.save_ptr;
    APSave_Init();
}

static void AP_ResolveCustomMachines(void)
{
    cm_api = (const CustomMachinesAPI *)Hoshi_ImportMod(
        (char *)CUSTOM_MACHINES_MOD_NAME, CUSTOM_MACHINES_API_MAJOR, CUSTOM_MACHINES_API_MINOR);
    if (!cm_api)
    {
        OSReport("[Main] custom_machines missing from this build: MACHINE GATING IS OFF\n");
        return;
    }

    cm_api->SetAvailabilityFilter(GateMachines_FilterSelectCharacter);
    cm_api->SetSpawnWeightFilter(GateMachines_SpawnWeight);
    cm_api->AddDeathHandler(APCheckDetect_AddDeath);
    OSReport("[Main] custom_machines: %d machine(s), %d kinds, %d characters\n",
             cm_api->GetCount(), cm_api->GetKindCeiling(),
             cm_api->GetCharacterKindCeiling());
}

// Runs once the card is read (or skipped). The first point past every mod's OnBoot, so
// an import still missing here is missing from the build.
static void OnSaveLoaded(void)
{
    ap_save = (APSave *)mod_desc.save_ptr;
    if (ap_save->stamp != APSAVE_STAMP)
    {
        OSReport("[Main] Save block stamp mismatch, reinitialized\n");
        APSave_Init();
    }

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

    OSReport("[Main] %d items received, options %s\n",
             ap_save->item_received_count, ap_save->options_received ? "loaded" : "pending");

    ap_data->item_received_index = ap_save->item_received_count;

    // Ahead of the reward regrant, which marks cells on the AP tab.
    APChecklist_Register();

    ChecklistRewards_OnSaveLoaded();
    APPatches_OnSaveLoaded();
    APChecks_OnSaveLoaded();

    if (ap_save->options_received)
        APOptions_ApplyRevealChecklists();

    // Mod_CopyFromSave has run, so ap_menu_settings holds the persisted toggles.
    SyncMenuStateToAPData();

    ap_data->game_ready = 1;
    OSReport("[Main] game_ready set, waiting for the AP client\n");
}

static void OnMainMenuLoad(void)
{
    OSReport("[Main] Entered the main menu\n");
}

static void OnPlayerSelectLoad(void)
{
    OSReport("[Main] Entered player select (minor %d)\n", Scene_GetCurrentMinor());

    // City Trial colors persist from prior sessions with no init block to hook, so they
    // are validated on every CSS load.
    if (Scene_GetCurrentMinor() == MNRKIND_CITYPLYSELECT)
        GateColors_ValidateCityTrialColors();
}

// Race / Time Attack / Free Run, for AirRideMode and TopRideMode alike.
static const char *RunModeName(int mode)
{
    static const char *const names[] = { "Race", "Time Attack", "Free Run" };
    return ((unsigned)mode < GetElementsIn(names)) ? names[mode] : "?";
}

// Players, riders, their machines and the map all exist by now.
static void On3DLoadEnd(void)
{
    static const char *const city_mode_names[] = { "Trial", "Stadium", "Free Run" };
    const char *mode_label;
    const char *what;

    // Gm_IsInCity is true only on the CT main map; the CT major covers Trial, Free Run
    // and every stadium.
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
            mode_label = ((unsigned)cm < GetElementsIn(city_mode_names)) ? city_mode_names[cm] : "?";
    }
    else
    {
        what = "Air Ride";
        mode_label = RunModeName(Gm_GetAirRideMode());
    }

    OSReport("[Main] Loaded %s: %s Ground=%d Stage=%d CityMode=%d Stadium=%d(%d) Damage=%d ItemData=%d\n",
             what, mode_label, Gr_GetCurrentGrKind(), Gm_GetCurrentStageKind(),
             Gm_GetCityMode(), Gm_GetCurrentStadiumKind(),
             Gm_GetCurrentStadiumGroup(), Gm_IsDamageEnabled(), Item_CheckIsLoaded());

    char roster[160];
    int n = 0;
    for (int i = 0; i < PLY_NUM; i++)
    {
        if (Ply_GetPKind(i) == PKIND_NONE)
            continue;

        RiderData *rd = Ply_GetRiderGObj(i)->userdata;
        n += sprintf(&roster[n], "%sP%d r%d/c%d/m%d", n ? ", " : "",
                     i + 1, rd->kind, rd->color_idx,
                     CustomMachines_ResolveKind(cm_api, Ply_GetMachineIsBike(i), Ply_GetMachineKind(i)));
    }
    if (n)
        OSReport("[Main] Players - %s\n", roster);

    if (Gm_IsAutoDemo())
        OSReport("[Main] Title attract demo round, no check counts toward the seed\n");

    GateMachines_On3DLoadEnd();
    GateAbilities_On3DLoadEnd();
    ItemSpawnFilter_On3DLoadEnd();
    PermanentPatch_On3DLoadEnd();

    if (ap_menu_settings.deathlink_enabled)
        DeathLink_On3DLoadEnd();

    if (SettingsMenu_EnergyLinkEnabled())
        EnergyLink_On3DLoadEnd();

    if (ap_menu_settings.traplink_enabled)
        TrapLink_On3DLoadEnd();

    GoalMaxStatsCT_On3DLoadEnd();
    APCheckDetect_On3DLoadEnd();
    APPatches_On3DLoadEnd();
    KirbyScale_On3DLoadEnd();

    if (ap_menu_settings.drop_ability_enabled)
        DropAbility_On3DLoadEnd();
}

// Top Ride runs under its own minor, which never reaches On3DLoadEnd.
static void OnTopRideLoadEnd(void)
{
    OSReport("[Main] Loaded Top Ride: %s\n", RunModeName(TopRide_GetMode()));

    if (SettingsMenu_EnergyLinkEnabled())
        EnergyLink_OnTopRideLoadEnd();

    if (ap_menu_settings.traplink_enabled)
        TrapLink_OnTopRideLoadEnd();

    if (ap_menu_settings.deathlink_enabled)
        DeathLink_OnTopRideLoadEnd();

    APCheckDetect_OnTopRideLoadEnd();
    KirbyScale_OnTopRideLoadEnd();

    if (ap_menu_settings.drop_ability_enabled)
        DropAbility_OnTopRideLoadEnd();
}

static void On3DPause(int pause_ply)
{
    OSReport("[Main] Paused by player %d\n", pause_ply + 1);
}

static void On3DUnpause(int pause_ply)
{
    OSReport("[Main] Unpaused by player %d\n", pause_ply + 1);
}

// Stadium_ExitMinor has latched GameData.stadium_results by now.
static void On3DExit(void)
{
    OSReport("[Main] Exited 3D\n");

    APCheckDetect_On3DExit();
    APPatches_On3DExit();
}

static void OnSceneChange(void)
{
    OSReport("[Main] Entered major %d / minor %d\n",
             Scene_GetCurrentMajor(), Scene_GetCurrentMinor());

    KirbyScale_OnSceneChange();
}

static void OnFrameStart(void)
{
    APOptions_OnFrameStart();

    // Cleared by ChecklistRewards_ApplyLocations, so this runs once per client write.
    if (ap_data->location_data_valid)
        ChecklistRewards_ApplyLocations();

    if (ap_data->backfill_valid)
    {
        APChecks_ApplyBackfill();
        APPatches_ApplyBackfill();
        ap_data->backfill_valid = 0;
    }

    APText_OnFrameStart();
    APItems_OnFrameStart();
}

#include "os.h"
#include "game.h"
#include "hoshi/settings.h"
#include "machine.h"
#include "rider.h"
#include "event.h"
#include "item.h"
#include "stage.h"
#include "stadium.h"

#include "archipelago_api.h"
#include "debug_menu.h"

static char *toggle_values[] = {"Disabled", "Enabled"};

static int machine_state[VCKIND_NUM];
static int ability_state[COPYKIND_NUM];
static int event_state[EVKIND_NUM];
static int patch_state[PATCHKIND_NUM];
static int item_state[ITUNLOCK_NUM];
static int box_state[BOXKIND_NUM];
static int ar_stage_state[AIRRIDE_NUM];
static int tr_stage_state[TOPRIDE_NUM];
static int tr_item_state[TRITEM_NUM];
static int color_state[KIRBYCOLOR_NUM];
static int stadium_state[STKIND_NUM];

#define DEF_SYNC(name, cat, arr, count) \
    static void name(int v) { \
        (void)v; \
        if (!ap_api) return; \
        u32 m = 0; \
        for (int i = 0; i < (count); i++) \
            if (arr[i]) m |= ((u32)1 << i); \
        if (ap_api->GetUnlockMask(cat) != m) \
            OSReport("[ApDebug] " #cat " = 0x%X\n", m); \
        ap_api->SetUnlockMask(cat, m); \
    }

DEF_SYNC(SyncMachines,  AP_UNLOCK_MACHINE,         machine_state,  VCKIND_NUM)
DEF_SYNC(SyncAbilities, AP_UNLOCK_ABILITY,         ability_state,  COPYKIND_NUM)
DEF_SYNC(SyncEvents,    AP_UNLOCK_EVENT,           event_state,    EVKIND_NUM)
DEF_SYNC(SyncPatches,   AP_UNLOCK_PATCH,           patch_state,    PATCHKIND_NUM)
DEF_SYNC(SyncItems,     AP_UNLOCK_ITEM,            item_state,     ITUNLOCK_NUM)
DEF_SYNC(SyncBoxes,     AP_UNLOCK_BOX,             box_state,      BOXKIND_NUM)
DEF_SYNC(SyncARStages,  AP_UNLOCK_AIRRIDE_STAGE,   ar_stage_state, AIRRIDE_NUM)
DEF_SYNC(SyncTRStages,  AP_UNLOCK_TOPRIDE_STAGE,   tr_stage_state, TOPRIDE_NUM)
DEF_SYNC(SyncTRItems,   AP_UNLOCK_TOPRIDE_ITEM,    tr_item_state,  TRITEM_NUM)
DEF_SYNC(SyncColors,    AP_UNLOCK_COLOR,           color_state,    KIRBYCOLOR_NUM)
DEF_SYNC(SyncStadiums,  AP_UNLOCK_STADIUM,         stadium_state,  STKIND_NUM)

#define DEF_REFRESH(name, cat, arr, count) \
    static void name(void) { \
        u32 m = ap_api ? ap_api->GetUnlockMask(cat) : 0; \
        for (int i = 0; i < (count); i++) \
            arr[i] = (m & ((u32)1 << i)) ? 1 : 0; \
    }

DEF_REFRESH(RefreshMachines,  AP_UNLOCK_MACHINE,        machine_state,  VCKIND_NUM)
DEF_REFRESH(RefreshAbilities, AP_UNLOCK_ABILITY,        ability_state,  COPYKIND_NUM)
DEF_REFRESH(RefreshEvents,    AP_UNLOCK_EVENT,          event_state,    EVKIND_NUM)
DEF_REFRESH(RefreshPatches,   AP_UNLOCK_PATCH,          patch_state,    PATCHKIND_NUM)
DEF_REFRESH(RefreshItems,     AP_UNLOCK_ITEM,           item_state,     ITUNLOCK_NUM)
DEF_REFRESH(RefreshBoxes,     AP_UNLOCK_BOX,            box_state,      BOXKIND_NUM)
DEF_REFRESH(RefreshARStages,  AP_UNLOCK_AIRRIDE_STAGE,  ar_stage_state, AIRRIDE_NUM)
DEF_REFRESH(RefreshTRStages,  AP_UNLOCK_TOPRIDE_STAGE,  tr_stage_state, TOPRIDE_NUM)
DEF_REFRESH(RefreshTRItems,   AP_UNLOCK_TOPRIDE_ITEM,   tr_item_state,  TRITEM_NUM)
DEF_REFRESH(RefreshColors,    AP_UNLOCK_COLOR,          color_state,    KIRBYCOLOR_NUM)
DEF_REFRESH(RefreshStadiums,  AP_UNLOCK_STADIUM,        stadium_state,  STKIND_NUM)

void DebugMenu_RefreshStateFromMasks(void)
{
    RefreshMachines();
    RefreshAbilities();
    RefreshEvents();
    RefreshPatches();
    RefreshItems();
    RefreshBoxes();
    RefreshARStages();
    RefreshTRStages();
    RefreshTRItems();
    RefreshColors();
    RefreshStadiums();
}

#define DEF_ALL(prefix, cat, arr, count, label) \
    static int prefix##UnlockAll(OptionDesc *self) { \
        (void)self; \
        if (!ap_api) return 1; \
        u32 m = (count >= 32) ? 0xFFFFFFFFu : ((1u << (count)) - 1u); \
        ap_api->SetUnlockMask(cat, m); \
        for (int i = 0; i < (count); i++) arr[i] = 1; \
        OSReport("[ApDebug] Unlock all " label ": " #cat " = 0x%X\n", m); \
        ap_api->Textbox("All " label " unlocked"); \
        return 1; \
    } \
    static int prefix##LockAll(OptionDesc *self) { \
        (void)self; \
        if (!ap_api) return 1; \
        ap_api->SetUnlockMask(cat, 0); \
        for (int i = 0; i < (count); i++) arr[i] = 0; \
        OSReport("[ApDebug] Lock all " label ": " #cat " = 0x0\n"); \
        ap_api->Textbox("All " label " locked"); \
        return 1; \
    }

DEF_ALL(Mch, AP_UNLOCK_MACHINE,        machine_state,  VCKIND_NUM,       "machines")
DEF_ALL(Abl, AP_UNLOCK_ABILITY,        ability_state,  COPYKIND_NUM,     "abilities")
DEF_ALL(Evt, AP_UNLOCK_EVENT,          event_state,    EVKIND_NUM,       "events")
DEF_ALL(Pch, AP_UNLOCK_PATCH,          patch_state,    PATCHKIND_NUM,    "patch types")
DEF_ALL(Itm, AP_UNLOCK_ITEM,           item_state,     ITUNLOCK_NUM,     "items")
DEF_ALL(Box, AP_UNLOCK_BOX,            box_state,      BOXKIND_NUM,      "boxes")
DEF_ALL(Ars, AP_UNLOCK_AIRRIDE_STAGE,  ar_stage_state, AIRRIDE_NUM,      "AR stages")
DEF_ALL(Trs, AP_UNLOCK_TOPRIDE_STAGE,  tr_stage_state, TOPRIDE_NUM,      "TR stages")
DEF_ALL(Tri, AP_UNLOCK_TOPRIDE_ITEM,   tr_item_state,  TRITEM_NUM,       "TR items")
DEF_ALL(Clr, AP_UNLOCK_COLOR,          color_state,    KIRBYCOLOR_NUM,   "colors")
DEF_ALL(Std, AP_UNLOCK_STADIUM,        stadium_state,  STKIND_NUM,       "stadiums")

#define GIVE_FN(name, id) \
    static int name(OptionDesc *self) { \
        (void)self; \
        if (!ap_api) return 1; \
        if (!ap_api->QueueItem(id)) \
            OSReport("[ApDebug] Give item " #id " (%d) failed: queue full\n", id); \
        else \
            OSReport("[ApDebug] Give item " #id " (%d)\n", id); \
        return 1; \
    }

// Stat patches
GIVE_FN(GiveHP,       AP_ITKIND_HP)
GIVE_FN(GiveAccel,    AP_ITKIND_ACCEL)
GIVE_FN(GiveTopSpd,   AP_ITKIND_TOPSPEED)
GIVE_FN(GiveTurn,     AP_ITKIND_TURN)
GIVE_FN(GiveCharge,   AP_ITKIND_CHARGE)
GIVE_FN(GiveGlide,    AP_ITKIND_GLIDE)
GIVE_FN(GiveOffense,  AP_ITKIND_OFFENSE)
GIVE_FN(GiveDefense,  AP_ITKIND_DEFENSE)
GIVE_FN(GiveWeight,   AP_ITKIND_WEIGHT)
GIVE_FN(GiveAllUp,    AP_ITKIND_ALLUP)

// Permanent patches
GIVE_FN(GivePermHP,      AP_PERM_PATCH_HP)
GIVE_FN(GivePermAccel,   AP_PERM_PATCH_ACCEL)
GIVE_FN(GivePermTopSpd,  AP_PERM_PATCH_TOPSPEED)
GIVE_FN(GivePermTurn,    AP_PERM_PATCH_TURN)
GIVE_FN(GivePermCharge,  AP_PERM_PATCH_CHARGE)
GIVE_FN(GivePermGlide,   AP_PERM_PATCH_GLIDE)
GIVE_FN(GivePermOff,     AP_PERM_PATCH_OFFENSE)
GIVE_FN(GivePermDef,     AP_PERM_PATCH_DEFENSE)
GIVE_FN(GivePermWeight,  AP_PERM_PATCH_WEIGHT)
GIVE_FN(GivePermAllUp,   AP_ITEM_PERM_PATCH_ALL_UP)

// Copy abilities
GIVE_FN(GiveCopyBomb,    AP_ITKIND_COPYBOMB)
GIVE_FN(GiveCopyFire,    AP_ITKIND_COPYFIRE)
GIVE_FN(GiveCopyIce,     AP_ITKIND_COPYICE)
GIVE_FN(GiveCopySleep,   AP_ITKIND_COPYSLEEP)
GIVE_FN(GiveCopyWheel,   AP_ITKIND_COPYTIRE)
GIVE_FN(GiveCopyWing,    AP_ITKIND_COPYBIRD)
GIVE_FN(GiveCopyPlasma,  AP_ITKIND_COPYPLASMA)
GIVE_FN(GiveCopyTornado, AP_ITKIND_COPYTORNADO)
GIVE_FN(GiveCopySword,   AP_ITKIND_COPYSWORD)
GIVE_FN(GiveCopyNeedle,  AP_ITKIND_COPYSPIKE)
GIVE_FN(GiveCopyMike,    AP_ITKIND_COPYMIC)

// Food
GIVE_FN(GiveMaximTomato,  AP_ITKIND_FOODMAXIMTOMATO)
GIVE_FN(GiveEnergyDrink,  AP_ITKIND_FOODENERGYDRINK)
GIVE_FN(GiveIceCream,     AP_ITKIND_FOODICECREAM)
GIVE_FN(GiveRiceBall,     AP_ITKIND_FOODRICEBALL)
GIVE_FN(GiveChicken,      AP_ITKIND_FOODCHICKEN)
GIVE_FN(GiveCurry,        AP_ITKIND_FOODCURRY)
GIVE_FN(GiveRamen,        AP_ITKIND_FOODRAMEN)
GIVE_FN(GiveOmelet,       AP_ITKIND_FOODOMELET)
GIVE_FN(GiveHamburger,    AP_ITKIND_FOODHAMBURGER)
GIVE_FN(GiveSushi,        AP_ITKIND_FOODSUSHI)
GIVE_FN(GiveHotDog,       AP_ITKIND_FOODHOTDOG)
GIVE_FN(GiveApple,        AP_ITKIND_FOODAPPLE)

// Special items
GIVE_FN(GiveCandy,       AP_ITKIND_CANDY)
GIVE_FN(GiveSpeedMax,    AP_ITKIND_SPEEDMAX)
GIVE_FN(GiveOffenseMax,  AP_ITKIND_OFFENSEMAX)
GIVE_FN(GiveDefenseMax,  AP_ITKIND_DEFENSEMAX)
GIVE_FN(GiveChargeMax,   AP_ITKIND_CHARGEMAX)

// Legendary pieces
GIVE_FN(GiveDragoonA,  AP_ITKIND_DRAGOON1)
GIVE_FN(GiveDragoonB,  AP_ITKIND_DRAGOON2)
GIVE_FN(GiveDragoonC,  AP_ITKIND_DRAGOON3)
GIVE_FN(GiveHydraX,    AP_ITKIND_HYDRA1)
GIVE_FN(GiveHydraY,    AP_ITKIND_HYDRA2)
GIVE_FN(GiveHydraZ,    AP_ITKIND_HYDRA3)

// City Trial events (trigger as if event tile rolled)
GIVE_FN(GiveEvtDynaBlade,        AP_EVENT_DYNABLADE)
GIVE_FN(GiveEvtTac,              AP_EVENT_TAC)
GIVE_FN(GiveEvtMeteor,           AP_EVENT_METEOR)
GIVE_FN(GiveEvtPillar,           AP_EVENT_PILLAR)
GIVE_FN(GiveEvtRunAmok,          AP_EVENT_RUNAMOK)
GIVE_FN(GiveEvtRestorationArea,  AP_EVENT_RESTORATIONAREA)
GIVE_FN(GiveEvtRailFire,         AP_EVENT_RAILFIRE)
GIVE_FN(GiveEvtSameItem,         AP_EVENT_SAMEITEM)
GIVE_FN(GiveEvtLighthouse,       AP_EVENT_LIGHTHOUSE)
GIVE_FN(GiveEvtSecretChamber,    AP_EVENT_SECRETCHAMBER)
GIVE_FN(GiveEvtPrediction,       AP_EVENT_PREDICTION)
GIVE_FN(GiveEvtMachineFormation, AP_EVENT_MACHINEFORMATION)
GIVE_FN(GiveEvtUFO,              AP_EVENT_UFO)
GIVE_FN(GiveEvtBounce,           AP_EVENT_BOUNCE)
GIVE_FN(GiveEvtFog,              AP_EVENT_FOG)
GIVE_FN(GiveEvtFakePowerups,     AP_EVENT_FAKEPOWERUPS)

// Traps & events
GIVE_FN(Give1HPTrap,        AP_ITEM_1_HP_TRAP)
GIVE_FN(GiveAllDown,        AP_ITEM_ALL_DOWN)
GIVE_FN(GiveDragoon,        AP_ITEM_GIVE_DRAGOON)
GIVE_FN(GiveHydra,          AP_ITEM_GIVE_HYDRA)
GIVE_FN(GiveDropPatchesTrap,AP_ITEM_DROP_PATCHES_TRAP)

// Upgrades
GIVE_FN(GivePatchCap,    AP_ITEM_PATCH_CAP_INCREASE)
GIVE_FN(GiveSpawnRateUp, AP_ITEM_SPAWN_RATE_UP)
GIVE_FN(GiveFillerAR,    AP_ITEM_CHECKBOX_FILLER_AIRRIDE)
GIVE_FN(GiveFillerTR,    AP_ITEM_CHECKBOX_FILLER_TOPRIDE)
GIVE_FN(GiveFillerCT,    AP_ITEM_CHECKBOX_FILLER_CITYTRIAL)

// Top Ride items (spawn at each human Kirby's position for pickup)
GIVE_FN(GiveTRHammer,          AP_TOPRIDE_ITEM_GIVE_HAMMER)
GIVE_FN(GiveTRBigCake,         AP_TOPRIDE_ITEM_GIVE_BIG_CAKE)
GIVE_FN(GiveTRSpeedUp,         AP_TOPRIDE_ITEM_GIVE_SPEED_UP)
GIVE_FN(GiveTRSpeedDown,       AP_TOPRIDE_ITEM_GIVE_SPEED_DOWN)
GIVE_FN(GiveTRSpinner,         AP_TOPRIDE_ITEM_GIVE_SPINNER)
GIVE_FN(GiveTRChargeTank,      AP_TOPRIDE_ITEM_GIVE_CHARGE_TANK)
GIVE_FN(GiveTRInvincibleCandy, AP_TOPRIDE_ITEM_GIVE_INVINCIBLE_CANDY)
GIVE_FN(GiveTRBuzzSaw,         AP_TOPRIDE_ITEM_GIVE_BUZZ_SAW)
GIVE_FN(GiveTRDrill,           AP_TOPRIDE_ITEM_GIVE_DRILL)
GIVE_FN(GiveTRFreezeFan,       AP_TOPRIDE_ITEM_GIVE_FREEZE_FAN)
GIVE_FN(GiveTRMissile,         AP_TOPRIDE_ITEM_GIVE_MISSILE)
GIVE_FN(GiveTRFire,            AP_TOPRIDE_ITEM_GIVE_FIRE)
GIVE_FN(GiveTRPartyBallAlt,    AP_TOPRIDE_ITEM_GIVE_PARTY_BALL_ALT)
GIVE_FN(GiveTRBomb,            AP_TOPRIDE_ITEM_GIVE_BOMB)
GIVE_FN(GiveTRStepBoom,        AP_TOPRIDE_ITEM_GIVE_STEP_BOOM)
GIVE_FN(GiveTRLantern,         AP_TOPRIDE_ITEM_GIVE_LANTERN)
GIVE_FN(GiveTRWalky,           AP_TOPRIDE_ITEM_GIVE_WALKY)
GIVE_FN(GiveTRKracko,          AP_TOPRIDE_ITEM_GIVE_KRACKO)
GIVE_FN(GiveTRWhoPaint,        AP_TOPRIDE_ITEM_GIVE_WHO_PAINT)
GIVE_FN(GiveTRSmokescreen,     AP_TOPRIDE_ITEM_GIVE_SMOKESCREEN)
GIVE_FN(GiveTRChickie,         AP_TOPRIDE_ITEM_GIVE_CHICKIE)
GIVE_FN(GiveTRPartyBall,       AP_TOPRIDE_ITEM_GIVE_PARTY_BALL)

// Energy Link debug
static int GiveEnergy1000(OptionDesc *self)
{
    (void)self;
    if (!ap_api) return 1;
    ap_api->AddEnergy(1000.0f);
    OSReport("[ApDebug] Added 1000 energy\n");
    ap_api->Textbox("Added 1000 energy");
    return 1;
}

// Toggle option (gate enable/disable)
#define G(label, arr, idx, cb) \
    &(OptionDesc){ \
        .name = label, \
        .kind = OPTKIND_VALUE, \
        .val = &arr[idx], \
        .value_num = 2, \
        .value_names = toggle_values, \
        .on_change = cb, \
    }

// Action option
#define A(label, desc, fn) \
    &(OptionDesc){ \
        .name = label, \
        .description = desc, \
        .kind = OPTKIND_ACTION, \
        .on_action = fn, \
    }

// Check detection debug actions
static int CheckDbgClearAll(OptionDesc *self)
{
    (void)self;
    if (!ap_api) return 1;
    ap_api->DebugClearAllSentChecks();
    ap_api->Textbox("Cleared all sent_checks");
    return 1;
}

static int CheckDbgForceMarkAll(OptionDesc *self)
{
    (void)self;
    if (!ap_api) return 1;
    ap_api->DebugForceMarkAllChecks();
    ap_api->Textbox("Force-marked all sent_checks");
    return 1;
}

static int CheckDbgTriggerGoal(OptionDesc *self)
{
    (void)self;
    if (!ap_api) return 1;
    ap_api->DebugTriggerGoalComplete();
    ap_api->Textbox("Goal triggered");
    return 1;
}

static int CheckDbgRevealAll(OptionDesc *self)
{
    (void)self;
    if (!ap_api) return 1;
    ap_api->DebugRevealAllChecklists();
    ap_api->Textbox("All checklists revealed");
    return 1;
}

static int CheckDbgSimulateLocationData(OptionDesc *self)
{
    (void)self;
    if (!ap_api) return 1;
    ap_api->DebugSimulateLocationData();
    ap_api->Textbox("Simulated location data applied");
    return 1;
}

static int CheckDbgClearAllChecklistData(OptionDesc *self)
{
    (void)self;
    if (!ap_api) return 1;
    ap_api->DebugClearAllChecklistData();
    ap_api->Textbox("Cleared all checklist data");
    return 1;
}

// When enabled (default), the Z-button debug checklist unlock also calls
// GrantReward on the reward placed at the unlocked cell, closely simulating
// AP item receipt for standalone testing. When disabled, only the check is
// sent — the connected AP client delivers the item as normal.
static int auto_grant_on_debug_unlock = 1;

int DebugMenu_ShouldAutoGrantOnUnlock(void)
{
    return auto_grant_on_debug_unlock;
}

static void OnAutoGrantChange(int v)
{
    (void)v;
    OSReport("[ApDebug] Auto-grant on Z unlock: %s\n",
             auto_grant_on_debug_unlock ? "Enabled" : "Disabled");
}

// Submenu option
#define S(label, desc, menu_ref) \
    &(OptionDesc){ \
        .name = label, \
        .description = desc, \
        .kind = OPTKIND_MENU, \
        .menu_ptr = &menu_ref, \
    }

// 22 player-rideable machines. Free / Steer Star are the two Top Ride lobby
// "Control Type" choices and are gated by the TR machine-select hooks in
// gate_machines.c. The 4 omitted VCKINDs (WINGKIRBY, WHEELNORMAL, WHEELKIRBY,
// WHEELVSDEDEDE) are transformation forms or stadium CPU-only machines and
// have no player-facing unlock surface.
static MenuDesc machines_menu = {
    .option_num = 24,
    .options = {
        A("Unlock All", "Unlock all machines", MchUnlockAll),
        A("Lock All",   "Lock all machines",   MchLockAll),
        G("Warp Star",         machine_state, VCKIND_WARP,           SyncMachines),
        G("Compact Star",      machine_state, VCKIND_COMPACT,        SyncMachines),
        G("Winged Star",       machine_state, VCKIND_WINGED,         SyncMachines),
        G("Shadow Star",       machine_state, VCKIND_SHADOW,         SyncMachines),
        G("Hydra",             machine_state, VCKIND_HYDRA,          SyncMachines),
        G("Bulk Star",         machine_state, VCKIND_BULK,           SyncMachines),
        G("Slick Star",        machine_state, VCKIND_SLICK,          SyncMachines),
        G("Formula Star",      machine_state, VCKIND_FORMULA,        SyncMachines),
        G("Dragoon",           machine_state, VCKIND_DRAGOON,        SyncMachines),
        G("Wagon Star",        machine_state, VCKIND_WAGON,          SyncMachines),
        G("Rocket Star",       machine_state, VCKIND_ROCKET,         SyncMachines),
        G("Swerve Star",       machine_state, VCKIND_SWERVE,         SyncMachines),
        G("Turbo Star",        machine_state, VCKIND_TURBO,          SyncMachines),
        G("Jet Star",          machine_state, VCKIND_JET,            SyncMachines),
        G("Flight Warp Star",  machine_state, VCKIND_FLIGHT,         SyncMachines),
        G("Free Star",         machine_state, VCKIND_FREE,           SyncMachines),
        G("Steer Star",        machine_state, VCKIND_STEER,          SyncMachines),
        G("Wing Meta Knight",  machine_state, VCKIND_WINGMETAKNIGHT, SyncMachines),
        G("Wheelie Bike",      machine_state, VCKIND_WHEELIEBIKE,    SyncMachines),
        G("Rex Wheelie",       machine_state, VCKIND_REXWHEELIE,     SyncMachines),
        G("Wheelie Scooter",   machine_state, VCKIND_WHEELIESCOOTER, SyncMachines),
        G("Dedede Wheelie",    machine_state, VCKIND_WHEELDEDEDE,    SyncMachines),
    },
};

static MenuDesc abilities_menu = {
    .option_num = 13,
    .options = {
        A("Unlock All", "Unlock all copy abilities", AblUnlockAll),
        A("Lock All",   "Lock all copy abilities",   AblLockAll),
        G("Fire",    ability_state, COPYKIND_FIRE,    SyncAbilities),
        G("Wheel",   ability_state, COPYKIND_WHEEL,   SyncAbilities),
        G("Sleep",   ability_state, COPYKIND_SLEEP,   SyncAbilities),
        G("Sword",   ability_state, COPYKIND_SWORD,   SyncAbilities),
        G("Bomb",    ability_state, COPYKIND_BOMB,    SyncAbilities),
        G("Plasma",  ability_state, COPYKIND_PLASMA,  SyncAbilities),
        G("Needle",  ability_state, COPYKIND_NEEDLE,  SyncAbilities),
        G("Mic",     ability_state, COPYKIND_MIC,     SyncAbilities),
        G("Freeze",  ability_state, COPYKIND_FREEZE,  SyncAbilities),
        G("Tornado", ability_state, COPYKIND_TORNADO, SyncAbilities),
        G("Wing",    ability_state, COPYKIND_BIRD,    SyncAbilities),
    },
};

static MenuDesc events_menu = {
    .option_num = 18,
    .options = {
        A("Unlock All", "Unlock all events", EvtUnlockAll),
        A("Lock All",   "Lock all events",   EvtLockAll),
        G("Dyna Blade",         event_state, EVKIND_DYNABLADE,        SyncEvents),
        G("Tac",                event_state, EVKIND_TAC,              SyncEvents),
        G("Meteor",             event_state, EVKIND_METEOR,           SyncEvents),
        G("Pillar",             event_state, EVKIND_PILLAR,           SyncEvents),
        G("Run Amok",           event_state, EVKIND_RUNAMOK,          SyncEvents),
        G("Restoration Area",   event_state, EVKIND_RESTORATIONAREA,  SyncEvents),
        G("Rail Fire",          event_state, EVKIND_RAILFIRE,         SyncEvents),
        G("All Same Item",      event_state, EVKIND_SAMEITEM,         SyncEvents),
        G("Lighthouse",         event_state, EVKIND_LIGHTHOUSE,       SyncEvents),
        G("Secret Chamber",     event_state, EVKIND_SECRETCHAMBER,    SyncEvents),
        G("Prediction",         event_state, EVKIND_PREDICTION,       SyncEvents),
        G("Machine Formation",  event_state, EVKIND_MACHINEFORMATION, SyncEvents),
        G("UFO",                event_state, EVKIND_UFO,              SyncEvents),
        G("Bounce",             event_state, EVKIND_BOUNCE,           SyncEvents),
        G("Fog",                event_state, EVKIND_FOG,              SyncEvents),
        G("Fake Powerups",      event_state, EVKIND_FAKEPOWERUPS,     SyncEvents),
    },
};

static MenuDesc patches_menu = {
    .option_num = 11,
    .options = {
        A("Unlock All", "Unlock all patch types", PchUnlockAll),
        A("Lock All",   "Lock all patch types",   PchLockAll),
        G("Weight",    patch_state, PATCHKIND_WEIGHT,   SyncPatches),
        G("Accel",     patch_state, PATCHKIND_ACCEL,    SyncPatches),
        G("Top Speed", patch_state, PATCHKIND_TOPSPEED, SyncPatches),
        G("Turn",      patch_state, PATCHKIND_TURN,     SyncPatches),
        G("Charge",    patch_state, PATCHKIND_CHARGE,   SyncPatches),
        G("Glide",     patch_state, PATCHKIND_GLIDE,    SyncPatches),
        G("Offense",   patch_state, PATCHKIND_OFFENSE,  SyncPatches),
        G("Defense",   patch_state, PATCHKIND_DEFENSE,  SyncPatches),
        G("HP",        patch_state, PATCHKIND_HP,       SyncPatches),
    },
};

static MenuDesc items_menu = {
    .option_num = 32,
    .options = {
        A("Unlock All", "Unlock all CT items", ItmUnlockAll),
        A("Lock All",   "Lock all CT items",   ItmLockAll),
        G("All Up",         item_state, ITUNLOCK_ALLUP,           SyncItems),
        G("Speed Max",      item_state, ITUNLOCK_SPEEDMAX,        SyncItems),
        G("Speed Min",      item_state, ITUNLOCK_SPEEDMIN,        SyncItems),
        G("Offense Max",    item_state, ITUNLOCK_OFFENSEMAX,      SyncItems),
        G("Defense Max",    item_state, ITUNLOCK_DEFENSEMAX,      SyncItems),
        G("Charge Max",     item_state, ITUNLOCK_CHARGEMAX,       SyncItems),
        G("No Charge",      item_state, ITUNLOCK_CHARGENONE,      SyncItems),
        G("Candy",          item_state, ITUNLOCK_CANDY,           SyncItems),
        G("Maxim Tomato",   item_state, ITUNLOCK_FOODMAXIMTOMATO, SyncItems),
        G("Energy Drink",   item_state, ITUNLOCK_FOODENERGYDRINK, SyncItems),
        G("Ice Cream",      item_state, ITUNLOCK_FOODICECREAM,    SyncItems),
        G("Rice Ball",      item_state, ITUNLOCK_FOODRICEBALL,    SyncItems),
        G("Chicken",        item_state, ITUNLOCK_FOODCHICKEN,     SyncItems),
        G("Curry",          item_state, ITUNLOCK_FOODCURRY,       SyncItems),
        G("Ramen",          item_state, ITUNLOCK_FOODRAMEN,       SyncItems),
        G("Omelet",         item_state, ITUNLOCK_FOODOMELET,      SyncItems),
        G("Hamburger",      item_state, ITUNLOCK_FOODHAMBURGER,   SyncItems),
        G("Sushi",          item_state, ITUNLOCK_FOODSUSHI,       SyncItems),
        G("Hot Dog",        item_state, ITUNLOCK_FOODHOTDOG,      SyncItems),
        G("Apple",          item_state, ITUNLOCK_FOODAPPLE,       SyncItems),
        G("Fireworks",      item_state, ITUNLOCK_FIREWORKS,       SyncItems),
        G("Panic Spin",     item_state, ITUNLOCK_PANICSPIN,       SyncItems),
        G("Sensor Bomb",    item_state, ITUNLOCK_SENSORBOMB,      SyncItems),
        G("Gordo",          item_state, ITUNLOCK_GORDO,           SyncItems),
        G("Hydra Piece 1",  item_state, ITUNLOCK_HYDRA1,          SyncItems),
        G("Hydra Piece 2",  item_state, ITUNLOCK_HYDRA2,          SyncItems),
        G("Hydra Piece 3",  item_state, ITUNLOCK_HYDRA3,          SyncItems),
        G("Dragoon Piece 1", item_state, ITUNLOCK_DRAGOON1,       SyncItems),
        G("Dragoon Piece 2", item_state, ITUNLOCK_DRAGOON2,       SyncItems),
        G("Dragoon Piece 3", item_state, ITUNLOCK_DRAGOON3,       SyncItems),
    },
};

static MenuDesc boxes_menu = {
    .option_num = 5,
    .options = {
        A("Unlock All", "Unlock all box types", BoxUnlockAll),
        A("Lock All",   "Lock all box types",   BoxLockAll),
        G("Blue Box",  box_state, BOXKIND_BLUE,  SyncBoxes),
        G("Green Box", box_state, BOXKIND_GREEN, SyncBoxes),
        G("Red Box",   box_state, BOXKIND_RED,   SyncBoxes),
    },
};

static MenuDesc ar_stages_menu = {
    .option_num = 11,
    .options = {
        A("Unlock All", "Unlock all AR stages", ArsUnlockAll),
        A("Lock All",   "Lock all AR stages",   ArsLockAll),
        G("Fantasy Meadows",  ar_stage_state, AIRRIDE_FANTASY_MEADOWS,  SyncARStages),
        G("Magma Flows",      ar_stage_state, AIRRIDE_MAGMA_FLOWS,      SyncARStages),
        G("Sky Sands",        ar_stage_state, AIRRIDE_SKY_SANDS,        SyncARStages),
        G("Frozen Hillside",  ar_stage_state, AIRRIDE_FROZEN_HILLSIDE,  SyncARStages),
        G("Beanstalk Park",   ar_stage_state, AIRRIDE_BEANSTALK_PARK,   SyncARStages),
        G("Celestial Valley", ar_stage_state, AIRRIDE_CELESTIAL_VALLEY, SyncARStages),
        G("Machine Passage",  ar_stage_state, AIRRIDE_MACHINE_PASSAGE,  SyncARStages),
        G("Checker Knights",  ar_stage_state, AIRRIDE_CHECKER_KNIGHTS,  SyncARStages),
        G("Nebula Belt",      ar_stage_state, AIRRIDE_NEBULA_BELT,      SyncARStages),
    },
};

static MenuDesc tr_stages_menu = {
    .option_num = 9,
    .options = {
        A("Unlock All", "Unlock all TR stages", TrsUnlockAll),
        A("Lock All",   "Lock all TR stages",   TrsLockAll),
        G("Grass", tr_stage_state, TOPRIDE_GRASS, SyncTRStages),
        G("Sand",  tr_stage_state, TOPRIDE_SAND,  SyncTRStages),
        G("Sky",   tr_stage_state, TOPRIDE_SKY,   SyncTRStages),
        G("Fire",  tr_stage_state, TOPRIDE_FIRE,  SyncTRStages),
        G("Light", tr_stage_state, TOPRIDE_LIGHT, SyncTRStages),
        G("Water", tr_stage_state, TOPRIDE_WATER, SyncTRStages),
        G("Metal", tr_stage_state, TOPRIDE_METAL, SyncTRStages),
    },
};

static MenuDesc tr_items_menu = {
    .option_num = 19,
    .options = {
        A("Unlock All", "Unlock all TR items", TriUnlockAll),
        A("Lock All",   "Lock all TR items",   TriLockAll),
        G("Hammer",            tr_item_state, TRITEM_HAMMER,           SyncTRItems),
        G("Big Cake",          tr_item_state, TRITEM_BIG_CAKE,         SyncTRItems),
        G("Speed Up",          tr_item_state, TRITEM_SPEED_UP,         SyncTRItems),
        G("Speed Down",        tr_item_state, TRITEM_SPEED_DOWN,       SyncTRItems),
        G("Spinner",           tr_item_state, TRITEM_SPINNER,          SyncTRItems),
        G("Charge Tank",       tr_item_state, TRITEM_CHARGE_TANK,      SyncTRItems),
        G("Invincible Candy",  tr_item_state, TRITEM_INVINCIBLE_CANDY, SyncTRItems),
        G("Buzz Saw",          tr_item_state, TRITEM_BUZZ_SAW,         SyncTRItems),
        G("Drill",             tr_item_state, TRITEM_DRILL,            SyncTRItems),
        G("Missile",           tr_item_state, TRITEM_MISSILE,          SyncTRItems),
        G("Step-boom",         tr_item_state, TRITEM_STEP_BOOM,        SyncTRItems),
        G("Lantern",           tr_item_state, TRITEM_LANTERN,          SyncTRItems),
        G("Kracko",            tr_item_state, TRITEM_KRACKO,           SyncTRItems),
        G("Who? Paint",        tr_item_state, TRITEM_WHO_PAINT,        SyncTRItems),
        G("Smokescreen",       tr_item_state, TRITEM_SMOKESCREEN,      SyncTRItems),
        G("Chickie",           tr_item_state, TRITEM_CHICKIE,          SyncTRItems),
        G("Party Ball",        tr_item_state, TRITEM_PARTY_BALL,       SyncTRItems),
    },
};

static MenuDesc colors_menu = {
    .option_num = 10,
    .options = {
        A("Unlock All", "Unlock all colors", ClrUnlockAll),
        A("Lock All",   "Lock all colors",   ClrLockAll),
        G("Pink",   color_state, KIRBYCOLOR_PINK,   SyncColors),
        G("Yellow", color_state, KIRBYCOLOR_YELLOW, SyncColors),
        G("Blue",   color_state, KIRBYCOLOR_BLUE,   SyncColors),
        G("Red",    color_state, KIRBYCOLOR_RED,    SyncColors),
        G("Green",  color_state, KIRBYCOLOR_GREEN,  SyncColors),
        G("Purple", color_state, KIRBYCOLOR_PURPLE, SyncColors),
        G("Brown",  color_state, KIRBYCOLOR_BROWN,  SyncColors),
        G("White",  color_state, KIRBYCOLOR_WHITE,  SyncColors),
    },
};

static MenuDesc stadiums_menu = {
    .option_num = 26,
    .options = {
        A("Unlock All", "Unlock all stadiums", StdUnlockAll),
        A("Lock All",   "Lock all stadiums",   StdLockAll),
        G("Drag Race 1",         stadium_state, STKIND_DRAG1,          SyncStadiums),
        G("Drag Race 2",         stadium_state, STKIND_DRAG2,          SyncStadiums),
        G("Drag Race 3",         stadium_state, STKIND_DRAG3,          SyncStadiums),
        G("Drag Race 4",         stadium_state, STKIND_DRAG4,          SyncStadiums),
        G("Air Glider",          stadium_state, STKIND_AIRGLIDER,      SyncStadiums),
        G("Target Flight",       stadium_state, STKIND_TARGETFLIGHT,   SyncStadiums),
        G("High Jump",           stadium_state, STKIND_HIGHJUMP,       SyncStadiums),
        G("Kirby Melee 1",       stadium_state, STKIND_MELEE1,         SyncStadiums),
        G("Kirby Melee 2",       stadium_state, STKIND_MELEE2,         SyncStadiums),
        G("Destruction Derby 1", stadium_state, STKIND_DESTRUCTION1,   SyncStadiums),
        G("Destruction Derby 2", stadium_state, STKIND_DESTRUCTION2,   SyncStadiums),
        G("Destruction Derby 3", stadium_state, STKIND_DESTRUCTION3,   SyncStadiums),
        G("Destruction Derby 4", stadium_state, STKIND_DESTRUCTION4,   SyncStadiums),
        G("Destruction Derby 5", stadium_state, STKIND_DESTRUCTION5,   SyncStadiums),
        G("Single Race 1",       stadium_state, STKIND_SINGLERACE1,    SyncStadiums),
        G("Single Race 2",       stadium_state, STKIND_SINGLERACE2,    SyncStadiums),
        G("Single Race 3",       stadium_state, STKIND_SINGLERACE3,    SyncStadiums),
        G("Single Race 4",       stadium_state, STKIND_SINGLERACE4,    SyncStadiums),
        G("Single Race 5",       stadium_state, STKIND_SINGLERACE5,    SyncStadiums),
        G("Single Race 6",       stadium_state, STKIND_SINGLERACE6,    SyncStadiums),
        G("Single Race 7",       stadium_state, STKIND_SINGLERACE7,    SyncStadiums),
        G("Single Race 8",       stadium_state, STKIND_SINGLERACE8,    SyncStadiums),
        G("Single Race 9",       stadium_state, STKIND_SINGLERACE9,    SyncStadiums),
        G("VS King Dedede",      stadium_state, STKIND_VSKINGDEDEDE,   SyncStadiums),
    },
};

static MenuDesc give_stat_patches_menu = {
    .option_num = 10,
    .options = {
        A("HP Patch",        "Give HP patch",        GiveHP),
        A("Accel Patch",     "Give Accel patch",     GiveAccel),
        A("Top Speed Patch", "Give Top Speed patch", GiveTopSpd),
        A("Turn Patch",      "Give Turn patch",      GiveTurn),
        A("Charge Patch",    "Give Charge patch",    GiveCharge),
        A("Glide Patch",     "Give Glide patch",     GiveGlide),
        A("Offense Patch",   "Give Offense patch",   GiveOffense),
        A("Defense Patch",   "Give Defense patch",    GiveDefense),
        A("Weight Patch",    "Give Weight patch",     GiveWeight),
        A("All Up",          "Give All Up",           GiveAllUp),
    },
};

static MenuDesc give_perm_patches_menu = {
    .option_num = 10,
    .options = {
        A("Perm HP",        "Give permanent HP patch",        GivePermHP),
        A("Perm Accel",     "Give permanent Accel patch",     GivePermAccel),
        A("Perm Top Speed", "Give permanent Top Speed patch", GivePermTopSpd),
        A("Perm Turn",      "Give permanent Turn patch",      GivePermTurn),
        A("Perm Charge",    "Give permanent Charge patch",    GivePermCharge),
        A("Perm Glide",     "Give permanent Glide patch",     GivePermGlide),
        A("Perm Offense",   "Give permanent Offense patch",   GivePermOff),
        A("Perm Defense",   "Give permanent Defense patch",   GivePermDef),
        A("Perm Weight",    "Give permanent Weight patch",    GivePermWeight),
        A("Perm All Up",    "Give permanent All Up",          GivePermAllUp),
    },
};

static MenuDesc give_abilities_menu = {
    .option_num = 11,
    .options = {
        A("Bomb",    "Give Bomb ability",    GiveCopyBomb),
        A("Fire",    "Give Fire ability",    GiveCopyFire),
        A("Ice",     "Give Ice ability",     GiveCopyIce),
        A("Sleep",   "Give Sleep ability",   GiveCopySleep),
        A("Wheel",   "Give Wheel ability",   GiveCopyWheel),
        A("Wing",    "Give Wing ability",    GiveCopyWing),
        A("Plasma",  "Give Plasma ability",  GiveCopyPlasma),
        A("Tornado", "Give Tornado ability", GiveCopyTornado),
        A("Sword",   "Give Sword ability",   GiveCopySword),
        A("Needle",  "Give Needle ability",  GiveCopyNeedle),
        A("Mike",    "Give Mike ability",    GiveCopyMike),
    },
};

static MenuDesc give_food_menu = {
    .option_num = 12,
    .options = {
        A("Maxim Tomato",  "Give Maxim Tomato",  GiveMaximTomato),
        A("Energy Drink",  "Give Energy Drink",  GiveEnergyDrink),
        A("Ice Cream",     "Give Ice Cream",     GiveIceCream),
        A("Rice Ball",     "Give Rice Ball",     GiveRiceBall),
        A("Chicken",       "Give Chicken",       GiveChicken),
        A("Curry",         "Give Curry",         GiveCurry),
        A("Ramen",         "Give Ramen",         GiveRamen),
        A("Omelet",        "Give Omelet",        GiveOmelet),
        A("Hamburger",     "Give Hamburger",     GiveHamburger),
        A("Sushi",         "Give Sushi",         GiveSushi),
        A("Hot Dog",       "Give Hot Dog",       GiveHotDog),
        A("Apple",         "Give Apple",         GiveApple),
    },
};

static MenuDesc give_special_menu = {
    .option_num = 5,
    .options = {
        A("Candy",       "Give Candy",       GiveCandy),
        A("Speed Max",   "Give Speed Max",   GiveSpeedMax),
        A("Offense Max", "Give Offense Max", GiveOffenseMax),
        A("Defense Max", "Give Defense Max", GiveDefenseMax),
        A("Charge Max",  "Give Charge Max",  GiveChargeMax),
    },
};

static MenuDesc give_legendary_menu = {
    .option_num = 8,
    .options = {
        A("Dragoon Part A", "Give Dragoon Part A", GiveDragoonA),
        A("Dragoon Part B", "Give Dragoon Part B", GiveDragoonB),
        A("Dragoon Part C", "Give Dragoon Part C", GiveDragoonC),
        A("Hydra Part X",   "Give Hydra Part X",   GiveHydraX),
        A("Hydra Part Y",   "Give Hydra Part Y",   GiveHydraY),
        A("Hydra Part Z",   "Give Hydra Part Z",   GiveHydraZ),
        A("Give Dragoon",   "Assemble full Dragoon", GiveDragoon),
        A("Give Hydra",     "Assemble full Hydra",   GiveHydra),
    },
};

static MenuDesc give_events_menu = {
    .option_num = 16,
    .options = {
        A("Dyna Blade",        "Trigger Dyna Blade event",        GiveEvtDynaBlade),
        A("Tac",               "Trigger Tac event",               GiveEvtTac),
        A("Meteor",            "Trigger Meteor event",            GiveEvtMeteor),
        A("Pillar",            "Trigger Pillar event",            GiveEvtPillar),
        A("Run Amok",          "Trigger Run Amok event",          GiveEvtRunAmok),
        A("Restoration Area",  "Trigger Restoration Area event",  GiveEvtRestorationArea),
        A("Rail Fire",         "Trigger Rail Fire event",         GiveEvtRailFire),
        A("All Same Item",     "Trigger All Same Item event",     GiveEvtSameItem),
        A("Lighthouse",        "Trigger Lighthouse event",        GiveEvtLighthouse),
        A("Secret Chamber",    "Trigger Secret Chamber event",    GiveEvtSecretChamber),
        A("Prediction",        "Trigger Prediction event",        GiveEvtPrediction),
        A("Machine Formation", "Trigger Machine Formation event", GiveEvtMachineFormation),
        A("UFO",               "Trigger UFO event",               GiveEvtUFO),
        A("Bounce",            "Trigger Bounce event",            GiveEvtBounce),
        A("Fog",                "Trigger Fog event",              GiveEvtFog),
        A("Fake Powerups",     "Trigger Fake Powerups event",     GiveEvtFakePowerups),
    },
};

static MenuDesc give_traps_menu = {
    .option_num = 3,
    .options = {
        A("1 HP Trap",        "Set HP to 1",                 Give1HPTrap),
        A("All Down",         "All stats down",              GiveAllDown),
        A("Drop Patches Trap","Eject rider patches (CT)",    GiveDropPatchesTrap),
    },
};

static MenuDesc give_upgrades_menu = {
    .option_num = 6,
    .options = {
        A("Patch Cap Increase",  "Increase patch cap",       GivePatchCap),
        A("Spawn Rate Up",       "Increase item spawn rate", GiveSpawnRateUp),
        A("Give 1000 Energy",    "Add 1000 to EnergyLink balance", GiveEnergy1000),
        A("AR Checkbox Filler",  "Fill AR checklist square", GiveFillerAR),
        A("TR Checkbox Filler",  "Fill TR checklist square", GiveFillerTR),
        A("CT Checkbox Filler",  "Fill CT checklist square", GiveFillerCT),
    },
};

static MenuDesc give_topride_items_menu = {
    .option_num = 22,
    .options = {
        A("Hammer",            "Give TR Hammer",           GiveTRHammer),
        A("Big Cake",          "Give TR Big Cake",         GiveTRBigCake),
        A("Speed Up",          "Give TR Speed Up",         GiveTRSpeedUp),
        A("Speed Down",        "Give TR Speed Down",       GiveTRSpeedDown),
        A("Spinner",           "Give TR Spinner",          GiveTRSpinner),
        A("Charge Tank",       "Give TR Charge Tank",      GiveTRChargeTank),
        A("Invincible Candy",  "Give TR Invincible Candy", GiveTRInvincibleCandy),
        A("Buzz Saw",          "Give TR Buzz Saw",         GiveTRBuzzSaw),
        A("Drill",             "Give TR Drill",            GiveTRDrill),
        A("Freeze Fan",        "Give TR Freeze Fan",       GiveTRFreezeFan),
        A("Missile",           "Give TR Missile",          GiveTRMissile),
        A("Fire",              "Give TR Fire",             GiveTRFire),
        A("Party Ball (alt)",  "Give TR Party Ball (alt)", GiveTRPartyBallAlt),
        A("Bomb",              "Give TR Bomb",             GiveTRBomb),
        A("Step-boom",         "Give TR Step-boom",        GiveTRStepBoom),
        A("Lantern",           "Give TR Lantern",          GiveTRLantern),
        A("Walky",             "Give TR Walky",            GiveTRWalky),
        A("Kracko",            "Give TR Kracko",           GiveTRKracko),
        A("Who? Paint",        "Give TR Who? Paint",       GiveTRWhoPaint),
        A("Smokescreen",       "Give TR Smokescreen",      GiveTRSmokescreen),
        A("Chickie",           "Give TR Chickie",          GiveTRChickie),
        A("Party Ball",        "Give TR Party Ball",       GiveTRPartyBall),
    },
};

static MenuDesc give_items_menu = {
    .option_num = 10,
    .options = {
        S("Stat Patches",      "Temporary stat patches",          give_stat_patches_menu),
        S("Permanent Patches", "Permanent stat boosts",           give_perm_patches_menu),
        S("Copy Abilities",    "Give Kirby a copy ability",       give_abilities_menu),
        S("Food",              "Healing items",                   give_food_menu),
        S("Special Items",     "Powerful one-use items",          give_special_menu),
        S("Legendary Pieces",  "Dragoon and Hydra parts",        give_legendary_menu),
        S("Top Ride Items",    "Spawn a Top Ride item for pickup", give_topride_items_menu),
        S("CT Events",         "Trigger a City Trial event",      give_events_menu),
        S("Traps & Events",    "Traps and event triggers",       give_traps_menu),
        S("Upgrades",          "Progression upgrades and fillers", give_upgrades_menu),
    },
};

static MenuDesc checks_menu = {
    .option_num = 7,
    .options = {
        &(OptionDesc){
            .name = "Auto-Grant on Z Unlock",
            .description = "On: Z-unlock also grants the cell's reward (simulate AP). Off: only send the check; let AP client deliver.",
            .kind = OPTKIND_VALUE,
            .val = &auto_grant_on_debug_unlock,
            .value_num = 2,
            .value_names = toggle_values,
            .on_change = OnAutoGrantChange,
        },
        A("Clear All sent_checks",   "Wipe sent_checks bitmask and goal_complete", CheckDbgClearAll),
        A("Force-Mark All",          "Set every sent_checks bit and goal_complete", CheckDbgForceMarkAll),
        A("Trigger goal_complete",   "Set only goal_complete (sent_checks unchanged)", CheckDbgTriggerGoal),
        A("Reveal All Checklists",   "Make every checkbox visible (visual only)",   CheckDbgRevealAll),
        A("Simulate Location Data",  "Fill location arrays with a random shuffle",  CheckDbgSimulateLocationData),
        A("Clear All Checklist Data", "Wipe every checkbox flag, sent_checks, and location shuffle", CheckDbgClearAllChecklistData),
    },
};

static MenuDesc debug_menu = {
    .option_num = 13,
    .options = {
        S("Machines",        "Toggle machine unlock gates",     machines_menu),
        S("Copy Abilities",  "Toggle ability unlock gates",     abilities_menu),
        S("Events",          "Toggle event unlock gates",       events_menu),
        S("Patch Types",     "Toggle patch type unlock gates",  patches_menu),
        S("CT Items",        "Toggle CT item unlock gates",     items_menu),
        S("Box Types",       "Toggle box type unlock gates",    boxes_menu),
        S("AR Stages",       "Toggle Air Ride stage gates",     ar_stages_menu),
        S("TR Stages",       "Toggle Top Ride stage gates",     tr_stages_menu),
        S("TR Items",        "Toggle Top Ride item gates",      tr_items_menu),
        S("Colors",          "Toggle Kirby color gates",        colors_menu),
        S("Stadiums",        "Toggle stadium unlock gates",     stadiums_menu),
        S("Give Items",      "Give items directly (free)",      give_items_menu),
        S("Checks",          "Check detection and goal debug",  checks_menu),
    },
};

OptionDesc DebugMod_RootOption = {
    .name = "Archipelago Debug",
    .description = "Toggle gate unlocks, give items, and exercise check detection.",
    .kind = OPTKIND_MENU,
    .menu_ptr = &debug_menu,
};

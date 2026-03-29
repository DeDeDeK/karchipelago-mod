#include "os.h"
#include "game.h"
#include "hoshi/settings.h"
#include "machine.h"
#include "rider.h"
#include "event.h"
#include "item.h"
#include "stage.h"
#include "stadium.h"

#include "main.h"
#include "debug_menu.h"
#include "ap_item_handler.h"
#include "textbox.h"
#include "gate_items.h"
#include "gate_topride_items.h"
#include "gate_colors.h"


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

#define DEF_SYNC(name, type, field, arr, count) \
    static void name(int v) { \
        (void)v; \
        type m = 0; \
        for (int i = 0; i < (count); i++) \
            if (arr[i]) m |= ((type)1 << i); \
        save_data->field = m; \
    }

DEF_SYNC(SyncMachines,  u32, machine_unlocked_mask,       machine_state,  VCKIND_NUM)
DEF_SYNC(SyncAbilities, u16, ability_unlocked_mask,        ability_state,  COPYKIND_NUM)
DEF_SYNC(SyncEvents,    u32, event_unlocked_mask,          event_state,    EVKIND_NUM)
DEF_SYNC(SyncPatches,   u16, patch_unlocked_mask,          patch_state,    PATCHKIND_NUM)
DEF_SYNC(SyncItems,     u32, item_unlocked_mask,           item_state,     ITUNLOCK_NUM)
DEF_SYNC(SyncBoxes,     u8,  box_unlocked_mask,            box_state,      BOXKIND_NUM)
DEF_SYNC(SyncARStages,  u16, airride_stage_unlocked_mask,  ar_stage_state, AIRRIDE_NUM)
DEF_SYNC(SyncTRStages,  u16, topride_stage_unlocked_mask,  tr_stage_state, TOPRIDE_NUM)
DEF_SYNC(SyncTRItems,   u32, topride_item_unlocked_mask,   tr_item_state,  TRITEM_NUM)
DEF_SYNC(SyncColors,    u8,  color_unlocked_mask,          color_state,    KIRBYCOLOR_NUM)
DEF_SYNC(SyncStadiums,  u32, stadium_unlocked_mask,        stadium_state,  STKIND_NUM)

void DebugMenu_ApplyToSave(void)
{
    SyncMachines(0);
    SyncAbilities(0);
    SyncEvents(0);
    SyncPatches(0);
    SyncItems(0);
    SyncBoxes(0);
    SyncARStages(0);
    SyncTRStages(0);
    SyncTRItems(0);
    SyncColors(0);
    SyncStadiums(0);
}

#define DEF_ALL(prefix, type, field, arr, count, label) \
    static int prefix##UnlockAll(void) { \
        for (int i = 0; i < (count); i++) { \
            save_data->field |= ((type)1 << i); \
            arr[i] = 1; \
        } \
        TextBox_Enqueue("All " label " unlocked"); \
        return 1; \
    } \
    static int prefix##LockAll(void) { \
        save_data->field = 0; \
        for (int i = 0; i < (count); i++) arr[i] = 0; \
        TextBox_Enqueue("All " label " locked"); \
        return 1; \
    }

DEF_ALL(Mch, u32, machine_unlocked_mask,       machine_state,  VCKIND_NUM,       "machines")
DEF_ALL(Abl, u16, ability_unlocked_mask,        ability_state,  COPYKIND_NUM,     "abilities")
DEF_ALL(Evt, u32, event_unlocked_mask,          event_state,    EVKIND_NUM,       "events")
DEF_ALL(Pch, u16, patch_unlocked_mask,          patch_state,    PATCHKIND_NUM,    "patch types")
DEF_ALL(Itm, u32, item_unlocked_mask,           item_state,     ITUNLOCK_NUM,     "items")
DEF_ALL(Box, u8,  box_unlocked_mask,            box_state,      BOXKIND_NUM,      "boxes")
DEF_ALL(Ars, u16, airride_stage_unlocked_mask,  ar_stage_state, AIRRIDE_NUM,      "AR stages")
DEF_ALL(Trs, u16, topride_stage_unlocked_mask,  tr_stage_state, TOPRIDE_NUM,      "TR stages")
DEF_ALL(Tri, u32, topride_item_unlocked_mask,   tr_item_state,  TRITEM_NUM,       "TR items")
DEF_ALL(Clr, u8,  color_unlocked_mask,          color_state,    KIRBYCOLOR_NUM,   "colors")
DEF_ALL(Std, u32, stadium_unlocked_mask,        stadium_state,  STKIND_NUM,       "stadiums")

#define GIVE_FN(name, id) \
    static int name(void) { \
        if (save_data->unprocessed_count >= MAX_RECEIVED_ITEMS) \
            return 1; \
        save_data->unprocessed_items[save_data->unprocessed_count++] = id; \
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
GIVE_FN(GiveAllUp,    AP_ITEM_ALL_UP)

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
GIVE_FN(GiveHydraS,    AP_ITKIND_HYDRA1)
GIVE_FN(GiveHydraL,    AP_ITKIND_HYDRA2)
GIVE_FN(GiveHydraR,    AP_ITKIND_HYDRA3)

// Traps & events
GIVE_FN(GiveMeteorTrap,  AP_ITEM_METEOR_TRAP)
GIVE_FN(Give1HPTrap,     AP_ITEM_1_HP_TRAP)
GIVE_FN(GiveAllDown,     AP_ITEM_ALL_DOWN)
GIVE_FN(GiveCustomEvt,   AP_ITEM_EVENT_CUSTOM)
GIVE_FN(GiveDragoon,     AP_ITEM_GIVE_DRAGOON)
GIVE_FN(GiveHydra,       AP_ITEM_GIVE_HYDRA)

// Upgrades
GIVE_FN(GivePatchCap,   AP_ITEM_PATCH_CAP_INCREASE)
GIVE_FN(GiveARSpeed,    AP_ITEM_AIRRIDE_SPEED_BOOST)
GIVE_FN(GiveFillerAR,   AP_ITEM_CHECKBOX_FILLER_AIRRIDE)
GIVE_FN(GiveFillerTR,   AP_ITEM_CHECKBOX_FILLER_TOPRIDE)
GIVE_FN(GiveFillerCT,   AP_ITEM_CHECKBOX_FILLER_CITYTRIAL)

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

// Submenu option
#define S(label, desc, menu_ref) \
    &(OptionDesc){ \
        .name = label, \
        .description = desc, \
        .kind = OPTKIND_MENU, \
        .menu_ptr = &menu_ref, \
    }

static MenuDesc machines_menu = {
    .option_num = 28,
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
        G("Wing Kirby",        machine_state, VCKIND_WINGKIRBY,      SyncMachines),
        G("Wing Meta Knight",  machine_state, VCKIND_WINGMETAKNIGHT, SyncMachines),
        G("Wheel",             machine_state, VCKIND_WHEELNORMAL,    SyncMachines),
        G("Wheel Kirby",       machine_state, VCKIND_WHEELKIRBY,     SyncMachines),
        G("Wheelie Bike",      machine_state, VCKIND_WHEELIEBIKE,    SyncMachines),
        G("Rex Wheelie",       machine_state, VCKIND_REXWHEELIE,     SyncMachines),
        G("Wheelie Scooter",   machine_state, VCKIND_WHEELIESCOOTER, SyncMachines),
        G("Dedede Wheelie",    machine_state, VCKIND_WHEELDEDEDE,    SyncMachines),
        G("VS Dedede Wheelie", machine_state, VCKIND_WHEELVSDEDEDE,  SyncMachines),
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
        G("Ice",     ability_state, COPYKIND_ICE,     SyncAbilities),
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
    .option_num = 33,
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
        G("Time Bomb",      item_state, ITUNLOCK_TIMEBOMB,        SyncItems),
        G("Gordo",          item_state, ITUNLOCK_GORDO,           SyncItems),
        G("Hydra Piece 1",  item_state, ITUNLOCK_HYDRA1,          SyncItems),
        G("Hydra Piece 2",  item_state, ITUNLOCK_HYDRA2,          SyncItems),
        G("Hydra Piece 3",  item_state, ITUNLOCK_HYDRA3,          SyncItems),
        G("Dragoon Piece 1", item_state, ITUNLOCK_DRAGOON1,       SyncItems),
        G("Dragoon Piece 2", item_state, ITUNLOCK_DRAGOON2,       SyncItems),
        G("Dragoon Piece 3", item_state, ITUNLOCK_DRAGOON3,       SyncItems),
        G("HP Recovery",    item_state, ITUNLOCK_HP,              SyncItems),
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
        G("Celestial Valley", ar_stage_state, AIRRIDE_CELESTIAL_VALLEY, SyncARStages),
        G("Frozen Hillside",  ar_stage_state, AIRRIDE_FROZEN_HILLSIDE,  SyncARStages),
        G("Magma Flows",      ar_stage_state, AIRRIDE_MAGMA_FLOWS,      SyncARStages),
        G("Beanstalk Park",   ar_stage_state, AIRRIDE_BEANSTALK_PARK,   SyncARStages),
        G("Machine Passage",  ar_stage_state, AIRRIDE_MACHINE_PASSAGE,  SyncARStages),
        G("Sky Sands",        ar_stage_state, AIRRIDE_SKY_SANDS,        SyncARStages),
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
    .option_num = 25,
    .options = {
        A("Unlock All", "Unlock all TR items", TriUnlockAll),
        A("Lock All",   "Lock all TR items",   TriLockAll),
        G("Mystery",        tr_item_state, TRITEM_MYSTERY,      SyncTRItems),
        G("Hammer",         tr_item_state, TRITEM_HAMMER,       SyncTRItems),
        G("Grow",           tr_item_state, TRITEM_GROW,         SyncTRItems),
        G("Speed Up",       tr_item_state, TRITEM_SPEEDUP,      SyncTRItems),
        G("Speed Down",     tr_item_state, TRITEM_SPEEDDOWN,    SyncTRItems),
        G("Missile",        tr_item_state, TRITEM_MISSILE,      SyncTRItems),
        G("Charge Boost",   tr_item_state, TRITEM_CHARGEBOOST,  SyncTRItems),
        G("Invincible",     tr_item_state, TRITEM_INVINCIBLE,   SyncTRItems),
        G("Buzz Saw",       tr_item_state, TRITEM_BUZZSAW,      SyncTRItems),
        G("Spear",          tr_item_state, TRITEM_SPEAR,        SyncTRItems),
        G("Freeze",         tr_item_state, TRITEM_FREEZE,       SyncTRItems),
        G("Missile Alt",    tr_item_state, TRITEM_MISSILE_ALT,  SyncTRItems),
        G("Fire",           tr_item_state, TRITEM_FIRE,         SyncTRItems),
        G("Needle",         tr_item_state, TRITEM_NEEDLE,       SyncTRItems),
        G("Bomb",           tr_item_state, TRITEM_BOMB,         SyncTRItems),
        G("Land Mine",      tr_item_state, TRITEM_LANDMINE,     SyncTRItems),
        G("Sensor Bomb",    tr_item_state, TRITEM_SENSORBOMB,   SyncTRItems),
        G("Mike",           tr_item_state, TRITEM_MIKE,         SyncTRItems),
        G("Cracker",        tr_item_state, TRITEM_CRACKER,      SyncTRItems),
        G("Meta Knight",    tr_item_state, TRITEM_METAKNIGHT,   SyncTRItems),
        G("Smoke Screen",   tr_item_state, TRITEM_SMOKESCREEN,  SyncTRItems),
        G("Dizzy",          tr_item_state, TRITEM_DIZZY,        SyncTRItems),
        G("Backward",       tr_item_state, TRITEM_BACKWARD,     SyncTRItems),
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
        A("Hydra Part S",   "Give Hydra Part S",   GiveHydraS),
        A("Hydra Part L",   "Give Hydra Part L",   GiveHydraL),
        A("Hydra Part R",   "Give Hydra Part R",   GiveHydraR),
        A("Give Dragoon",   "Assemble full Dragoon", GiveDragoon),
        A("Give Hydra",     "Assemble full Hydra",   GiveHydra),
    },
};

static MenuDesc give_traps_menu = {
    .option_num = 4,
    .options = {
        A("Meteor Trap",   "Spawn meteor trap",     GiveMeteorTrap),
        A("1 HP Trap",     "Set HP to 1",           Give1HPTrap),
        A("All Down",      "All stats down",         GiveAllDown),
        A("Custom Event",  "Trigger custom event",   GiveCustomEvt),
    },
};

static MenuDesc give_upgrades_menu = {
    .option_num = 5,
    .options = {
        A("Patch Cap Increase",  "Increase patch cap",       GivePatchCap),
        A("AR Speed Boost",      "Air Ride speed boost",     GiveARSpeed),
        A("AR Checkbox Filler",  "Fill AR checklist square", GiveFillerAR),
        A("TR Checkbox Filler",  "Fill TR checklist square", GiveFillerTR),
        A("CT Checkbox Filler",  "Fill CT checklist square", GiveFillerCT),
    },
};

static MenuDesc give_items_menu = {
    .option_num = 8,
    .options = {
        S("Stat Patches",      "Temporary stat patches",          give_stat_patches_menu),
        S("Permanent Patches", "Permanent stat boosts",           give_perm_patches_menu),
        S("Copy Abilities",    "Give Kirby a copy ability",       give_abilities_menu),
        S("Food",              "Healing items",                   give_food_menu),
        S("Special Items",     "Powerful one-use items",          give_special_menu),
        S("Legendary Pieces",  "Dragoon and Hydra parts",        give_legendary_menu),
        S("Traps & Events",    "Traps and event triggers",       give_traps_menu),
        S("Upgrades",          "Progression upgrades and fillers", give_upgrades_menu),
    },
};

MenuDesc debug_menu = {
    .option_num = 12,
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
    },
};

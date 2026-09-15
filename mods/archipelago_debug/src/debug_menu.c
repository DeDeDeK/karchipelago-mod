#include "os.h"
#include "game.h"
#include "hoshi/settings.h"
#include "machine.h"
#include "rider.h"
#include "event.h"
#include "item.h"
#include "stage.h"
#include "stadium.h"
#include "inline.h"

#include "archipelago_api.h"
#include "debug_menu.h"

static char *toggle_values[] = {"Disabled", "Enabled"};

static int machine_state[AP_MACHINE_BIT_NUM];
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
static int base_ability_state[BASEABILITY_NUM];
static int star_piece_state[AP_STAR_PIECE_NUM];

// Seed sizes the AP Patch count row offers.
static char *ap_patch_count_values[] = {"Off", "8", "64", "512"};
static const int ap_patch_count_map[] = {0, 8, 64, AP_PATCH_MAX};
static int ap_patch_count_state;
// The row this option last mirrored. DebugSetApPatchCount rewrites the seed's own
// count and trims every collected bit past it, so the boot-time on_change sweep
// must not replay a bucketed value back over a seed that sits between rows.
static int ap_patch_count_synced = -1;

// Nearest row at or below the live count, so a seed's own value shows as the
// closest offered size rather than snapping the option back to Off.
static void RefreshApPatchCount(void)
{
    int n = ap_api ? ap_api->GetApPatchCount() : 0;
    ap_patch_count_state = 0;
    for (int i = 1; i < (int)(sizeof(ap_patch_count_map) / sizeof(ap_patch_count_map[0])); i++)
        if (n >= ap_patch_count_map[i])
            ap_patch_count_state = i;
    ap_patch_count_synced = ap_patch_count_state;
}

// A toggle rebuilds its whole category mask from the local array, so a bit the mask
// gained elsewhere - an AP delivery, a Give action, a granted reward - would be wiped
// by the next unrelated toggle. Only bits that differ from the last synced value are
// the menu's; the rest keep whatever the mask holds now.
static u32 synced_mask[AP_UNLOCK_NUM];

#define DEF_SYNC(name, cat, arr, count) \
    static void name(int v) { \
        (void)v; \
        if (!ap_api) return; \
        int n = (count); \
        u32 built = 0; \
        for (int i = 0; i < n; i++) \
            if (arr[i]) built |= ((u32)1 << i); \
        u32 owned = built ^ synced_mask[cat]; \
        u32 live = ap_api->GetUnlockMask(cat); \
        u32 m = (live & ~owned) | (built & owned); \
        for (int i = 0; i < n; i++) \
            arr[i] = (m & ((u32)1 << i)) ? 1 : 0; \
        synced_mask[cat] = m; \
        if (m == live) return; \
        ap_api->SetUnlockMask(cat, m); \
        OSReport("[ApDebug] " #cat " = %s\n", MaskBits(m, n)); \
    }

DEF_SYNC(SyncMachines,  AP_UNLOCK_MACHINE,         machine_state,  AP_MACHINE_BIT_NUM)
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
DEF_SYNC(SyncBaseAbil,  AP_UNLOCK_BASE_ABILITY,    base_ability_state, BASEABILITY_NUM)
DEF_SYNC(SyncStarPiece, AP_UNLOCK_AP_STAR_PIECE,   star_piece_state,   AP_STAR_PIECE_NUM)

#define DEF_REFRESH(name, cat, arr, count) \
    static void name(void) { \
        u32 m = ap_api ? ap_api->GetUnlockMask(cat) : 0; \
        int n = (count); \
        for (int i = 0; i < n; i++) \
            arr[i] = (m & ((u32)1 << i)) ? 1 : 0; \
        synced_mask[cat] = m; \
    }

DEF_REFRESH(RefreshMachines,  AP_UNLOCK_MACHINE,        machine_state,  AP_MACHINE_BIT_NUM)
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
DEF_REFRESH(RefreshBaseAbil,  AP_UNLOCK_BASE_ABILITY,   base_ability_state, BASEABILITY_NUM)
DEF_REFRESH(RefreshStarPiece, AP_UNLOCK_AP_STAR_PIECE,  star_piece_state,   AP_STAR_PIECE_NUM)

static void RefreshStateFromMasks(void)
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
    RefreshBaseAbil();
    RefreshStarPiece();
    RefreshApPatchCount();
}

#define DEF_ALL(prefix, cat, arr, count, label) \
    static int prefix##UnlockAll(OptionDesc *self) { \
        (void)self; \
        if (!ap_api) return 1; \
        int n = (count); \
        u32 m = (n >= 32) ? 0xFFFFFFFFu : ((1u << n) - 1u); \
        ap_api->SetUnlockMask(cat, m); \
        for (int i = 0; i < n; i++) arr[i] = 1; \
        synced_mask[cat] = m; \
        OSReport("[ApDebug] Unlocked all " label ": " #cat " = %s\n", MaskBits(m, n)); \
        ap_api->Textbox("All " label " unlocked"); \
        return 1; \
    } \
    static int prefix##LockAll(OptionDesc *self) { \
        (void)self; \
        if (!ap_api) return 1; \
        int n = (count); \
        ap_api->SetUnlockMask(cat, 0); \
        for (int i = 0; i < n; i++) arr[i] = 0; \
        synced_mask[cat] = 0; \
        OSReport("[ApDebug] Locked all " label ": " #cat " = %s\n", MaskBits(0, n)); \
        ap_api->Textbox("All " label " locked"); \
        return 1; \
    }

DEF_ALL(Mch, AP_UNLOCK_MACHINE,        machine_state,  AP_MACHINE_BIT_NUM, "machines")
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
DEF_ALL(Bab, AP_UNLOCK_BASE_ABILITY,   base_ability_state, BASEABILITY_NUM, "base abilities")
DEF_ALL(Sph, AP_UNLOCK_AP_STAR_PIECE,  star_piece_state,   AP_STAR_PIECE_NUM, "AP Star spheres")

#define GIVE_FN(name, id) \
    static int name(OptionDesc *self) { \
        (void)self; \
        if (!ap_api) return 1; \
        if (!ap_api->QueueItem(id)) \
            OSReport("[ApDebug] Queue full, dropped " #id " (%d)\n", id); \
        else \
            OSReport("[ApDebug] Queued " #id " (%d)\n", id); \
        return 1; \
    }

GIVE_FN(GiveHP,       AP_ITKIND_HP)
GIVE_FN(GiveBoost,    AP_ITKIND_BOOST)
GIVE_FN(GiveTopSpd,   AP_ITKIND_TOPSPEED)
GIVE_FN(GiveTurn,     AP_ITKIND_TURN)
GIVE_FN(GiveCharge,   AP_ITKIND_CHARGE)
GIVE_FN(GiveGlide,    AP_ITKIND_GLIDE)
GIVE_FN(GiveOffense,  AP_ITKIND_OFFENSE)
GIVE_FN(GiveDefense,  AP_ITKIND_DEFENSE)
GIVE_FN(GiveWeight,   AP_ITKIND_WEIGHT)
GIVE_FN(GiveAllUp,    AP_ITKIND_ALLUP)

GIVE_FN(GivePermHP,      AP_PERM_PATCH_HP)
GIVE_FN(GivePermBoost,   AP_PERM_PATCH_BOOST)
GIVE_FN(GivePermTopSpd,  AP_PERM_PATCH_TOPSPEED)
GIVE_FN(GivePermTurn,    AP_PERM_PATCH_TURN)
GIVE_FN(GivePermCharge,  AP_PERM_PATCH_CHARGE)
GIVE_FN(GivePermGlide,   AP_PERM_PATCH_GLIDE)
GIVE_FN(GivePermOff,     AP_PERM_PATCH_OFFENSE)
GIVE_FN(GivePermDef,     AP_PERM_PATCH_DEFENSE)
GIVE_FN(GivePermWeight,  AP_PERM_PATCH_WEIGHT)
GIVE_FN(GivePermAllUp,   AP_ITEM_PERM_PATCH_ALL_UP)

GIVE_FN(GiveCopyBomb,    AP_ITKIND_COPYBOMB)
GIVE_FN(GiveCopyFire,    AP_ITKIND_COPYFIRE)
GIVE_FN(GiveCopyFreeze,  AP_ITKIND_COPYFREEZE)
GIVE_FN(GiveCopySleep,   AP_ITKIND_COPYSLEEP)
GIVE_FN(GiveCopyWheel,   AP_ITKIND_COPYTIRE)
GIVE_FN(GiveCopyWing,    AP_ITKIND_COPYBIRD)
GIVE_FN(GiveCopyPlasma,  AP_ITKIND_COPYPLASMA)
GIVE_FN(GiveCopyTornado, AP_ITKIND_COPYTORNADO)
GIVE_FN(GiveCopySword,   AP_ITKIND_COPYSWORD)
GIVE_FN(GiveCopyNeedle,  AP_ITKIND_COPYSPIKE)
GIVE_FN(GiveCopyMike,    AP_ITKIND_COPYMIC)

GIVE_FN(GiveUnlockInhale,    AP_BASE_ABILITY_UNLOCK_INHALE)
GIVE_FN(GiveUnlockQuickSpin, AP_BASE_ABILITY_UNLOCK_QUICKSPIN)
GIVE_FN(GiveUnlockCharge,    AP_BASE_ABILITY_UNLOCK_CHARGE)

GIVE_FN(GiveSphereRose,   AP_STAR_PIECE_UNLOCK_ROSE)
GIVE_FN(GiveSphereGreen,  AP_STAR_PIECE_UNLOCK_GREEN)
GIVE_FN(GiveSphereViolet, AP_STAR_PIECE_UNLOCK_VIOLET)
GIVE_FN(GiveSphereTan,    AP_STAR_PIECE_UNLOCK_TAN)
GIVE_FN(GiveSphereBlue,   AP_STAR_PIECE_UNLOCK_BLUE)
GIVE_FN(GiveSphereYellow, AP_STAR_PIECE_UNLOCK_YELLOW)

GIVE_FN(GiveSphereItemRose,   AP_STAR_PIECE_GIVE_ROSE)
GIVE_FN(GiveSphereItemGreen,  AP_STAR_PIECE_GIVE_GREEN)
GIVE_FN(GiveSphereItemViolet, AP_STAR_PIECE_GIVE_VIOLET)
GIVE_FN(GiveSphereItemTan,    AP_STAR_PIECE_GIVE_TAN)
GIVE_FN(GiveSphereItemBlue,   AP_STAR_PIECE_GIVE_BLUE)
GIVE_FN(GiveSphereItemYellow, AP_STAR_PIECE_GIVE_YELLOW)

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

GIVE_FN(GiveCandy,       AP_ITKIND_CANDY)
GIVE_FN(GiveSpeedMax,    AP_ITKIND_SPEEDMAX)
GIVE_FN(GiveOffenseMax,  AP_ITKIND_OFFENSEMAX)
GIVE_FN(GiveDefenseMax,  AP_ITKIND_DEFENSEMAX)
GIVE_FN(GiveChargeMax,   AP_ITKIND_CHARGEMAX)

GIVE_FN(GiveDragoonA,  AP_ITKIND_DRAGOON1)
GIVE_FN(GiveDragoonB,  AP_ITKIND_DRAGOON2)
GIVE_FN(GiveDragoonC,  AP_ITKIND_DRAGOON3)
GIVE_FN(GiveHydraX,    AP_ITKIND_HYDRA1)
GIVE_FN(GiveHydraY,    AP_ITKIND_HYDRA2)
GIVE_FN(GiveHydraZ,    AP_ITKIND_HYDRA3)

// Trigger the event as if its event tile had rolled.
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

GIVE_FN(Give1HPTrap,        AP_ITEM_1_HP_TRAP)
GIVE_FN(GiveAllDown,        AP_ITEM_ALL_DOWN)
GIVE_FN(GiveDragoon,        AP_ITEM_GIVE_DRAGOON)
GIVE_FN(GiveHydra,          AP_ITEM_GIVE_HYDRA)
GIVE_FN(GiveApStar,         AP_ITEM_GIVE_AP_STAR)
GIVE_FN(GiveDropPatchesTrap,AP_ITEM_DROP_PATCHES_TRAP)

GIVE_FN(GiveApAllUp,     AP_ITEM_ALL_UP)
GIVE_FN(GivePatchCap,    AP_ITEM_PATCH_CAP_INCREASE)
GIVE_FN(GiveSpawnRateUp, AP_ITEM_SPAWN_RATE_UP)
GIVE_FN(GiveFillerAR,    AP_ITEM_CHECKBOX_FILLER_AIRRIDE)
GIVE_FN(GiveFillerTR,    AP_ITEM_CHECKBOX_FILLER_TOPRIDE)
GIVE_FN(GiveFillerCT,    AP_ITEM_CHECKBOX_FILLER_CITYTRIAL)
GIVE_FN(GiveFillerAP,    AP_ITEM_CHECKBOX_FILLER_ARCHIPELAGO)

GIVE_FN(GiveBigKirby,    AP_ITEM_BIG_KIRBY)
GIVE_FN(GiveSmallKirby,  AP_ITEM_SMALL_KIRBY)

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

// Every AP unlock item id, as contiguous { base, count } runs tagged with the
// category they gate. Machines take four runs: the AP world ships no item for
// VCKIND_WINGKIRBY, WHEELNORMAL and WHEELKIRBY (copy-ability and enemy forms) or for
// WHEELVSDEDEDE (the Vs. King Dedede stadium's CPU-only machine), and their unlock
// bits never clear. Every other category is one run.
static const struct
{
    u8 cat;
    u16 base;
    u8 count;
} unlock_runs[] = {
    { AP_UNLOCK_STADIUM,       AP_STADIUM_UNLOCK_BASE,           STKIND_NUM                                  },
    { AP_UNLOCK_EVENT,         AP_EVENT_UNLOCK_BASE,             EVKIND_NUM                                  },
    { AP_UNLOCK_ABILITY,       AP_ABILITY_UNLOCK_BASE,           COPYKIND_NUM                                },
    { AP_UNLOCK_BASE_ABILITY,  AP_BASE_ABILITY_UNLOCK_BASE,      BASEABILITY_NUM                             },
    { AP_UNLOCK_PATCH,         AP_PATCH_UNLOCK_BASE,             PATCHKIND_NUM                               },
    { AP_UNLOCK_ITEM,          AP_ITEM_UNLOCK_BASE,              ITUNLOCK_NUM                                },
    { AP_UNLOCK_MACHINE,       AP_MACHINE_UNLOCK_WARP,           VCKIND_STEER - VCKIND_WARP + 1              },
    { AP_UNLOCK_MACHINE,       AP_MACHINE_UNLOCK_WINGMETAKNIGHT, 1                                           },
    { AP_UNLOCK_MACHINE,       AP_MACHINE_UNLOCK_WHEELIEBIKE,    VCKIND_WHEELDEDEDE - VCKIND_WHEELIEBIKE + 1 },
    { AP_UNLOCK_MACHINE,       AP_MACHINE_UNLOCK_AP_STAR,        1                                           },
    { AP_UNLOCK_BOX,          AP_BOX_UNLOCK_BASE,               BOXKIND_NUM                                 },
    { AP_UNLOCK_AIRRIDE_STAGE, AP_STAGE_UNLOCK_AIRRIDE_BASE,     AIRRIDE_NUM                                 },
    { AP_UNLOCK_COLOR,         AP_COLOR_UNLOCK_BASE,             KIRBYCOLOR_NUM                              },
    { AP_UNLOCK_TOPRIDE_STAGE, AP_STAGE_UNLOCK_TOPRIDE_BASE,     TOPRIDE_NUM                                 },
    { AP_UNLOCK_TOPRIDE_ITEM,  AP_TOPRIDE_ITEM_UNLOCK_BASE,      TRITEM_NUM                                  },
    { AP_UNLOCK_AP_STAR_PIECE, AP_STAR_PIECE_UNLOCK_BASE,        AP_STAR_PIECE_NUM                           },
};

// Progression, not unlocks: applied globally or at the next round start rather than
// spawning a pickup, so they belong to no category and only the all-pools draw
// reaches them.
static const struct
{
    u16 base;
    u8 count;
} progression_pools[] = {
    { AP_PERM_PATCH_BASE,         PATCHKIND_NUM },
    { AP_ITEM_PERM_PATCH_ALL_UP,  1             },
    { AP_ITEM_PATCH_CAP_INCREASE, 1             },
    { AP_ITEM_SPAWN_RATE_UP,      1             },
};

// Uniform pick over one category's ids, or over every category plus the progression
// items when cat is -1. Returns -1 when the filter matched nothing.
static int PickUnlockId(int cat)
{
    int run_num = GetElementsIn(unlock_runs);
    int prog_num = GetElementsIn(progression_pools);

    int total = 0;
    for (int i = 0; i < run_num; i++)
        if (cat < 0 || unlock_runs[i].cat == cat)
            total += unlock_runs[i].count;
    if (cat < 0)
        for (int i = 0; i < prog_num; i++)
            total += progression_pools[i].count;
    if (total <= 0)
        return -1;

    int pick = HSD_Randi(total);
    for (int i = 0; i < run_num; i++)
    {
        if (cat >= 0 && unlock_runs[i].cat != cat)
            continue;
        if (pick < unlock_runs[i].count)
            return unlock_runs[i].base + pick;
        pick -= unlock_runs[i].count;
    }
    if (cat < 0)
        for (int i = 0; i < prog_num; i++)
        {
            if (pick < progression_pools[i].count)
                return progression_pools[i].base + pick;
            pick -= progression_pools[i].count;
        }
    return -1;
}

static void QueueDrawn(int id, const char *what)
{
    if (id < 0)
    {
        OSReport("[ApDebug] No %s id to give\n", what);
        return;
    }
    if (ap_api->QueueItem(id))
        OSReport("[ApDebug] Queued %s id=%d\n", what, id);
    else
        OSReport("[ApDebug] Queue full, dropped %s id=%d\n", what, id);
}

void DebugMenu_GiveRandomUnlock(void)
{
    if (!ap_api)
        return;
    QueueDrawn(PickUnlockId(-1), "unlock/progression");
}

#define DEF_GIVE_RANDOM(prefix, cat, label) \
    static int prefix##GiveRandom(OptionDesc *self) { \
        (void)self; \
        if (!ap_api) return 1; \
        QueueDrawn(PickUnlockId(cat), label); \
        return 1; \
    }

DEF_GIVE_RANDOM(Mch, AP_UNLOCK_MACHINE,       "machine")
DEF_GIVE_RANDOM(Abl, AP_UNLOCK_ABILITY,       "ability")
DEF_GIVE_RANDOM(Evt, AP_UNLOCK_EVENT,         "event")
DEF_GIVE_RANDOM(Pch, AP_UNLOCK_PATCH,         "patch type")
DEF_GIVE_RANDOM(Itm, AP_UNLOCK_ITEM,          "CT item")
DEF_GIVE_RANDOM(Box, AP_UNLOCK_BOX,           "box")
DEF_GIVE_RANDOM(Ars, AP_UNLOCK_AIRRIDE_STAGE, "AR stage")
DEF_GIVE_RANDOM(Trs, AP_UNLOCK_TOPRIDE_STAGE, "TR stage")
DEF_GIVE_RANDOM(Tri, AP_UNLOCK_TOPRIDE_ITEM,  "TR item")
DEF_GIVE_RANDOM(Clr, AP_UNLOCK_COLOR,         "color")
DEF_GIVE_RANDOM(Std, AP_UNLOCK_STADIUM,       "stadium")
DEF_GIVE_RANDOM(Bab, AP_UNLOCK_BASE_ABILITY,  "base ability")
DEF_GIVE_RANDOM(Sph, AP_UNLOCK_AP_STAR_PIECE, "AP Star sphere")

// The standalone City Trial gives in the 1-99 block; the rest of that block is
// global or save-only progression.
static const u16 ct_singletons[] = {
    AP_ITEM_ALL_UP,
    AP_ITEM_ALL_DOWN,
    AP_ITEM_GIVE_DRAGOON,
    AP_ITEM_GIVE_HYDRA,
    AP_ITEM_GIVE_AP_STAR,
    AP_ITEM_1_HP_TRAP,
    AP_ITEM_DROP_PATCHES_TRAP,
};

void DebugMenu_GiveRandomModeItem(MajorKind major)
{
    if (!ap_api)
        return;

    int picked;
    const char *mode_name;
    if (major == MJRKIND_CITY)
    {
        // One uniform pool over the three CT give segments: the event range, the
        // full ITKIND range, and the standalone gives.
        int n_evt = EVKIND_NUM;
        int n_it = AP_ITKIND_WEIGHTFAKE - AP_ITKIND_BASE + 1;
        int n_one = GetElementsIn(ct_singletons);
        int r = HSD_Randi(n_evt + n_it + n_one);
        if (r < n_evt)
            picked = AP_EVENT_BASE + r;
        else if (r < n_evt + n_it)
            picked = AP_ITKIND_BASE + (r - n_evt);
        else
            picked = ct_singletons[r - n_evt - n_it];
        mode_name = "CT";
    }
    else if (major == MJRKIND_AIR)
    {
        // Only copy abilities are honored outside CT; every other ITKIND no-ops
        // behind the Gm_IsInCity gate.
        picked = AP_ITKIND_COPYBOMB + HSD_Randi(AP_ITKIND_COPYMIC - AP_ITKIND_COPYBOMB + 1);
        mode_name = "AR";
    }
    else if (major == MJRKIND_TOP)
    {
        picked = AP_TOPRIDE_ITEM_GIVE_BASE +
                 HSD_Randi(AP_TOPRIDE_ITEM_GIVE_PARTY_BALL - AP_TOPRIDE_ITEM_GIVE_BASE + 1);
        mode_name = "TR";
    }
    else
    {
        return;
    }

    if (ap_api->QueueItem(picked))
        OSReport("[ApDebug] Queued %s item id=%d\n", mode_name, picked);
    else
        OSReport("[ApDebug] Queue full, dropped %s item id=%d\n", mode_name, picked);
}

static int GiveEnergy1000(OptionDesc *self)
{
    (void)self;
    if (!ap_api) return 1;
    ap_api->AddEnergy(1000.0f);
    OSReport("[ApDebug] Added 1000 energy\n");
    ap_api->Textbox("Added 1000 energy");
    return 1;
}

// Gate enable/disable toggle. Never saved - the mask is re-derived at save load,
// and the machine rows are renamed at runtime, which would move their save hashes.
#define G(label, arr, idx, cb) \
    &(OptionDesc){ \
        .name = label, \
        .kind = OPTKIND_VALUE, \
        .no_save = 1, \
        .val = &arr[idx], \
        .value_num = 2, \
        .value_names = toggle_values, \
        .on_change = cb, \
    }

#define A(label, desc, fn) \
    &(OptionDesc){ \
        .name = label, \
        .description = desc, \
        .kind = OPTKIND_ACTION, \
        .on_action = fn, \
    }

// Value row that writes AP state, and its two-state form. Never saved: the row is
// re-derived from live state before boot replays every on_change, and a value
// restored off the card would push the menu's idea of the world over the seed's.
#define V(label, desc, var, values, cb) \
    &(OptionDesc){ \
        .name = label, \
        .description = desc, \
        .kind = OPTKIND_VALUE, \
        .no_save = 1, \
        .val = &var, \
        .value_num = GetElementsIn(values), \
        .value_names = values, \
        .on_change = cb, \
    }

#define T(label, desc, var, cb) \
    &(OptionDesc){ \
        .name = label, \
        .description = desc, \
        .kind = OPTKIND_VALUE, \
        .no_save = 1, \
        .val = &var, \
        .value_num = 2, \
        .value_names = toggle_values, \
        .on_change = cb, \
    }

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

static void OnApPatchCountChange(int v)
{
    if (!ap_api || v == ap_patch_count_synced)
        return;
    ap_patch_count_synced = v;
    int n = ap_patch_count_map[v];
    ap_api->DebugSetApPatchCount(n);
    OSReport("[ApDebug] ap_patches = %d, registers on the next round load\n", n);
}

static int ApPatchDbgCollect(OptionDesc *self)
{
    (void)self;
    if (!ap_api) return 1;
    if (ap_api->DebugCollectApPatch())
        ap_api->Textbox("Collected an AP Patch");
    else
        ap_api->Textbox("No AP Patch left to collect");
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

static int RevealChecklistRow(int row, const char *msg)
{
    if (!ap_api) return 1;
    ap_api->DebugRevealChecklist(row);
    ap_api->Textbox(msg);
    return 1;
}

static int CheckDbgRevealAirRide(OptionDesc *self)
{
    (void)self;
    return RevealChecklistRow(GMMODE_AIRRIDE, "Air Ride checklist revealed");
}

static int CheckDbgRevealTopRide(OptionDesc *self)
{
    (void)self;
    return RevealChecklistRow(GMMODE_TOPRIDE, "Top Ride checklist revealed");
}

static int CheckDbgRevealCityTrial(OptionDesc *self)
{
    (void)self;
    return RevealChecklistRow(GMMODE_CITYTRIAL, "City Trial checklist revealed");
}

static int CheckDbgRevealArchipelago(OptionDesc *self)
{
    (void)self;
    return RevealChecklistRow(AP_CHECKLIST_ROW, "Archipelago checklist revealed");
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

static int auto_grant_on_debug_unlock = 0;

int DebugMenu_ShouldAutoGrantOnUnlock(void)
{
    return auto_grant_on_debug_unlock;
}

static void OnAutoGrantChange(int v)
{
    OSReport("[ApDebug] Auto-grant on Z unlock: %s\n", v ? "Enabled" : "Disabled");
}

// Every OPTKIND_VALUE on_change is replayed once at boot, right after the refresh
// pass below installs the live value, so a row that writes AP state latches: only a
// real move past the value it was last synced to does anything. Without that, boot
// would push the menu's idea of the world back over the seed's.

static const char *const checklist_row_names[CHECKLIST_MODE_NUM] = {
    "Air Ride", "Top Ride", "City Trial", "Archipelago",
};

static char *goal_values[] = {
    "100 Squares", "N Squares", "Listed Squares", "Hydra + Dragoon", "Beat King Dedede",
    "Max Stats", "Assemble AP Star", "All Legendaries", "None",
};
static int goal_state[CHECKLIST_MODE_NUM];

// The square count GOAL_N_CHECKLIST needs. One row for all four, since only the goal
// rows set to N read it.
static char *goal_amount_values[] = {"1", "5", "10", "25", "50", "100", "120"};
static const int goal_amount_map[] = {1, 5, 10, 25, 50, 100, 120};
static int goal_amount_state;

// Nearest offered row at or below a live value, so a seed between rows shows as the
// closest size rather than snapping the option to its first entry.
static int NearestRow(const int *map, int num, int live)
{
    int row = 0;
    for (int i = 1; i < num; i++)
        if (live >= map[i])
            row = i;
    return row;
}

// The amount is one row for four goals, so it follows the first row actually on the
// count goal; with none there it keeps whatever it was left at.
static void RefreshGoals(void)
{
    int amount = -1;
    for (int r = 0; r < CHECKLIST_MODE_NUM; r++)
    {
        int row_amount = 0;
        goal_state[r] = ap_api ? ap_api->GetGoal(r, &row_amount) : GOAL_NONE;
        if (goal_state[r] < 0 || goal_state[r] > GOAL_NONE)
            goal_state[r] = GOAL_NONE;
        if (goal_state[r] == GOAL_N_CHECKLIST && amount < 0)
            amount = row_amount;
    }
    if (amount > 0)
        goal_amount_state = NearestRow(goal_amount_map, GetElementsIn(goal_amount_map), amount);
}

// The rows only select. hoshi fires on_change on every D-pad tick, including
// auto-repeat, and goal evaluation is over the whole set - so committing each
// intermediate value would let a row scrolled past a satisfied kind latch
// goal_complete, which is sticky and is reported to the server.
static int GoalDbgApply(OptionDesc *self)
{
    (void)self;
    if (!ap_api) return 1;

    ap_api->DebugSetGoals(goal_state, goal_amount_map[goal_amount_state]);
    for (int r = 0; r < CHECKLIST_MODE_NUM; r++)
        OSReport("[ApDebug] %s goal = %s\n", checklist_row_names[r], goal_values[goal_state[r]]);
    ap_api->Textbox("Goals applied");
    return 1;
}

// Whether the seed ships unlock items for a category. The flags are read at connect,
// so a change only shows after Re-apply Slot Options.
static int gating_state[AP_UNLOCK_NUM];
static int gating_synced[AP_UNLOCK_NUM];

#define DEF_GATING(name, cat) \
    static void name(int v) { \
        if (!ap_api || v == gating_synced[cat]) return; \
        gating_synced[cat] = v; \
        ap_api->DebugSetGating(cat, v); \
        OSReport("[ApDebug] " #cat " gating: %s\n", v ? "Enabled" : "Disabled"); \
    }

DEF_GATING(OnGateMachines,  AP_UNLOCK_MACHINE)
DEF_GATING(OnGateAbilities, AP_UNLOCK_ABILITY)
DEF_GATING(OnGateEvents,    AP_UNLOCK_EVENT)
DEF_GATING(OnGatePatches,   AP_UNLOCK_PATCH)
DEF_GATING(OnGateItems,     AP_UNLOCK_ITEM)
DEF_GATING(OnGateBoxes,     AP_UNLOCK_BOX)
DEF_GATING(OnGateARStages,  AP_UNLOCK_AIRRIDE_STAGE)
DEF_GATING(OnGateTRStages,  AP_UNLOCK_TOPRIDE_STAGE)
DEF_GATING(OnGateTRItems,   AP_UNLOCK_TOPRIDE_ITEM)
DEF_GATING(OnGateColors,    AP_UNLOCK_COLOR)
DEF_GATING(OnGateStadiums,  AP_UNLOCK_STADIUM)
DEF_GATING(OnGateBaseAbil,  AP_UNLOCK_BASE_ABILITY)

// The per-stat cap a City Trial run starts at and its ceiling, plus the item spawn
// rate floor. All three are read out of the slot options as a round loads.
static char *patch_cap_values[] = {"1", "10", "25", "50", "100", "127"};
static const int patch_cap_map[] = {1, 10, 25, 50, 100, PATCH_STAT_MAX};
static int patch_cap_min_state;
static int patch_cap_max_state;
static int patch_cap_synced_min = -1;
static int patch_cap_synced_max = -1;

static void OnPatchCapMinChange(int v)
{
    if (!ap_api || v == patch_cap_synced_min) return;
    patch_cap_synced_min = v;
    ap_api->DebugSetPatchCapMin(patch_cap_map[v]);
    OSReport("[ApDebug] CT patch cap min = %d\n", patch_cap_map[v]);
}

static void OnPatchCapMaxChange(int v)
{
    if (!ap_api || v == patch_cap_synced_max) return;
    patch_cap_synced_max = v;
    ap_api->DebugSetPatchCapMax(patch_cap_map[v]);
    OSReport("[ApDebug] CT patch cap max = %d\n", patch_cap_map[v]);
}

static char *spawn_rate_values[] = {"25%", "50%", "75%", "100%"};
static const int spawn_rate_map[] = {25, 50, 75, 100};
static int spawn_rate_state;
static int spawn_rate_synced = -1;

static void OnSpawnRateChange(int v)
{
    if (!ap_api || v == spawn_rate_synced) return;
    spawn_rate_synced = v;
    ap_api->DebugSetSpawnRateMin(spawn_rate_map[v]);
    OSReport("[ApDebug] Spawn rate floor = %d%%\n", spawn_rate_map[v]);
}

static void RefreshSlotOptions(void)
{
    for (int cat = 0; cat < AP_UNLOCK_NUM; cat++)
    {
        gating_state[cat] = ap_api ? ap_api->GetGating((APUnlockCategory)cat) : 1;
        gating_synced[cat] = gating_state[cat];
    }

    int cap_num = GetElementsIn(patch_cap_map);
    int min = 0, max = 0, rate = 0;
    if (ap_api)
    {
        ap_api->GetPatchCapRange(&min, &max);
        rate = ap_api->GetSpawnRateMin();
    }
    // A stored 0 is "options not received yet", which the mod reads as the ceiling.
    patch_cap_min_state = NearestRow(patch_cap_map, cap_num, min ? min : PATCH_STAT_MAX);
    patch_cap_max_state = NearestRow(patch_cap_map, cap_num, max ? max : PATCH_STAT_MAX);
    spawn_rate_state = NearestRow(spawn_rate_map, GetElementsIn(spawn_rate_map),
                                  rate ? rate : 100);

    patch_cap_synced_min = patch_cap_min_state;
    patch_cap_synced_max = patch_cap_max_state;
    spawn_rate_synced = spawn_rate_state;
}

static int SlotOptDbgReapply(OptionDesc *self)
{
    (void)self;
    if (!ap_api) return 1;
    ap_api->DebugReapplySlotOptions();
    RefreshStateFromMasks();
    OSReport("[ApDebug] Re-applied slot options over a cleared mask set\n");
    ap_api->Textbox("Slot options re-applied");
    return 1;
}

// Cross-session checklist progress, mirroring the live counters. Setting a row one
// short of its target lets the next real event in a round complete the check.
static char *allup_values[] = {"0", "1", "2", "3", "4", "5"};
static char *purple_values[] = {"0", "1", "2", "3"};
static char *race_color_values[] = {"None", "Partial", "All"};
static const int race_color_map[] = {0x00, 0x7F, ((1 << KIRBYCOLOR_NUM) - 1)};
static int allup_state;
static int purple_state;
static int race_color_state;
static int allup_synced = -1;
static int purple_synced = -1;
static int race_color_synced = -1;

static void OnAllUpProgressChange(int v)
{
    if (!ap_api || v == allup_synced) return;
    allup_synced = v;
    ap_api->DebugSetCheckProgress(AP_PROGRESS_ALLUP_TOTAL, v);
}

static void OnPurpleProgressChange(int v)
{
    if (!ap_api || v == purple_synced) return;
    purple_synced = v;
    ap_api->DebugSetCheckProgress(AP_PROGRESS_PURPLE_SR1, v);
}

static void OnRaceColorProgressChange(int v)
{
    if (!ap_api || v == race_color_synced) return;
    race_color_synced = v;
    ap_api->DebugSetCheckProgress(AP_PROGRESS_RACE_COLORS, race_color_map[v]);
}

static void RefreshCheckProgress(void)
{
    allup_state = ap_api ? ap_api->GetCheckProgress(AP_PROGRESS_ALLUP_TOTAL) : 0;
    if (allup_state > 5) allup_state = 5;
    purple_state = ap_api ? ap_api->GetCheckProgress(AP_PROGRESS_PURPLE_SR1) : 0;
    if (purple_state > 3) purple_state = 3;
    // A mask, so the middle row stands for every partial state rather than one value.
    int colors = ap_api ? ap_api->GetCheckProgress(AP_PROGRESS_RACE_COLORS) : 0;
    race_color_state = (colors == 0) ? 0 : ((colors == race_color_map[2]) ? 2 : 1);
    allup_synced = allup_state;
    purple_synced = purple_state;
    race_color_synced = race_color_state;
}

// EnergyLink balance. The cheapest purchase is 200 MJ and the dearest 50000, so the
// rows bracket both ends of the affordability check.
static char *energy_values[] = {"0", "199", "200", "2500", "49999", "50000", "100000"};
static const int energy_map[] = {0, 199, 200, 2500, 49999, 50000, 100000};
static int energy_state;
static int energy_synced = -1;

static void OnEnergyBalanceChange(int v)
{
    if (!ap_api || v == energy_synced) return;
    energy_synced = v;
    ap_api->DebugSetEnergyBalance((s64)energy_map[v]);
    OSReport("[ApDebug] Energy balance = %d MJ\n", energy_map[v]);
}

static void RefreshEnergy(void)
{
    s64 live = ap_api ? ap_api->GetEnergyBalance() : 0;
    int clamped = live < 0 ? 0 : (live > 100000 ? 100000 : (int)live);
    energy_state = NearestRow(energy_map, GetElementsIn(energy_map), clamped);
    energy_synced = energy_state;
}

// Which player slot the pad bindings that drop an item act on.
static char *target_player_values[] = {"1", "2", "3", "4"};
static int target_player_state;

int DebugMenu_TargetPlayer(void)
{
    return target_player_state;
}

static void OnTargetPlayerChange(int v)
{
    OSReport("[ApDebug] Debug drops target player %d\n", v + 1);
}

static int MsgDbgSend(int kind, const char *name)
{
    if (!ap_api) return 1;
    if (ap_api->DebugSendText(kind))
        OSReport("[ApDebug] Posted a canned %s message\n", name);
    else
        OSReport("[ApDebug] Text mailbox still full, dropped the %s message\n", name);
    return 1;
}

static int MsgDbgCheck(OptionDesc *self)  { (void)self; return MsgDbgSend(APTEXT_KIND_CHECK,  "check"); }
static int MsgDbgItem(OptionDesc *self)   { (void)self; return MsgDbgSend(APTEXT_KIND_ITEM,   "item"); }
static int MsgDbgHint(OptionDesc *self)   { (void)self; return MsgDbgSend(APTEXT_KIND_HINT,   "hint"); }
static int MsgDbgStatus(OptionDesc *self) { (void)self; return MsgDbgSend(APTEXT_KIND_STATUS, "status"); }
static int MsgDbgChat(OptionDesc *self)   { (void)self; return MsgDbgSend(APTEXT_KIND_CHAT,   "chat"); }
static int MsgDbgLink(OptionDesc *self)   { (void)self; return MsgDbgSend(APTEXT_KIND_LINK,   "link"); }

static int MsgDbgOverlong(OptionDesc *self)
{
    (void)self;
    if (!ap_api) return 1;
    if (ap_api->DebugSendOverlongText())
        OSReport("[ApDebug] Posted an 8-run message past the third line\n");
    else
        OSReport("[ApDebug] Text mailbox still full, dropped the overlong message\n");
    return 1;
}

static int LinkDbgDeathlink(OptionDesc *self)
{
    (void)self;
    if (!ap_api) return 1;
    ap_api->DebugTriggerDeathlinkReceive();
    OSReport("[ApDebug] Armed deathlink_receive\n");
    ap_api->Textbox("DeathLink armed");
    return 1;
}

static int LinkDbgTraplink(OptionDesc *self)
{
    (void)self;
    if (!ap_api) return 1;
    ap_api->DebugTriggerTraplinkReceive();
    OSReport("[ApDebug] Armed traplink_receive\n");
    ap_api->Textbox("TrapLink armed");
    return 1;
}

static int EnergyDbgDrain(OptionDesc *self)
{
    (void)self;
    if (!ap_api) return 1;
    ap_api->DebugSetEnergyBalance(0);
    RefreshEnergy();
    OSReport("[ApDebug] Energy balance drained to 0\n");
    ap_api->Textbox("Energy drained");
    return 1;
}

static int ApPatchDbgClearCollected(OptionDesc *self)
{
    (void)self;
    if (!ap_api) return 1;
    ap_api->DebugClearApPatchCollected();
    ap_api->Textbox("Cleared collected AP Patches");
    return 1;
}

static int StateDbgReport(OptionDesc *self)
{
    (void)self;
    if (!ap_api) return 1;
    ap_api->DebugReportState();
    ap_api->Textbox("AP state written to the console");
    return 1;
}

static int StateDbgResetProgression(OptionDesc *self)
{
    (void)self;
    if (!ap_api) return 1;
    ap_api->DebugResetProgression();
    RefreshCheckProgress();
    ap_api->Textbox("Progression reset");
    return 1;
}

void DebugMenu_RefreshState(void)
{
    RefreshStateFromMasks();
    RefreshGoals();
    RefreshSlotOptions();
    RefreshCheckProgress();
    RefreshEnergy();
}

#define S(label, desc, menu_ref) \
    &(OptionDesc){ \
        .name = label, \
        .description = desc, \
        .kind = OPTKIND_MENU, \
        .menu_ptr = &menu_ref, \
    }

// 23 player-rideable machines plus the three action rows. The 4 omitted VCKINDs
// (WINGKIRBY, WHEELNORMAL, WHEELKIRBY, WHEELVSDEDEDE) are transformation forms or
// stadium CPU-only machines with no player-facing unlock surface.
static MenuDesc machines_menu = {
    .option_num = 26,
    .options = {
        A("Unlock All", "Unlock all machines", MchUnlockAll),
        A("Lock All",   "Lock all machines",   MchLockAll),
        A("Give Random", "Queue one random machine unlock item", MchGiveRandom),
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
        G("Archipelago Star",  machine_state, AP_MACHINE_BIT_AP_STAR, SyncMachines),
    },
};

static MenuDesc abilities_menu = {
    .option_num = 14,
    .options = {
        A("Unlock All", "Unlock all copy abilities", AblUnlockAll),
        A("Lock All",   "Lock all copy abilities",   AblLockAll),
        A("Give Random", "Queue one random copy ability unlock item", AblGiveRandom),
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

static MenuDesc base_abilities_menu = {
    .option_num = 6,
    .options = {
        A("Unlock All", "Unlock all base abilities", BabUnlockAll),
        A("Lock All",   "Lock all base abilities",   BabLockAll),
        A("Give Random", "Queue one random base ability unlock item", BabGiveRandom),
        G("Inhale",     base_ability_state, BASEABILITY_INHALE,    SyncBaseAbil),
        G("Quick Spin", base_ability_state, BASEABILITY_QUICKSPIN, SyncBaseAbil),
        G("Charge",     base_ability_state, BASEABILITY_CHARGE,    SyncBaseAbil),
    },
};

static MenuDesc events_menu = {
    .option_num = 19,
    .options = {
        A("Unlock All", "Unlock all events", EvtUnlockAll),
        A("Lock All",   "Lock all events",   EvtLockAll),
        A("Give Random", "Queue one random event unlock item", EvtGiveRandom),
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
    .option_num = 12,
    .options = {
        A("Unlock All", "Unlock all patch types", PchUnlockAll),
        A("Lock All",   "Lock all patch types",   PchLockAll),
        A("Give Random", "Queue one random patch type unlock item", PchGiveRandom),
        G("Weight",    patch_state, PATCHKIND_WEIGHT,   SyncPatches),
        G("Boost",     patch_state, PATCHKIND_ACCEL,    SyncPatches),
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
        A("Give Random", "Queue one random CT item unlock item", ItmGiveRandom),
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
        G("Hydra Part X",   item_state, ITUNLOCK_HYDRA1,          SyncItems),
        G("Hydra Part Y",   item_state, ITUNLOCK_HYDRA2,          SyncItems),
        G("Hydra Part Z",   item_state, ITUNLOCK_HYDRA3,          SyncItems),
        G("Dragoon Part A", item_state, ITUNLOCK_DRAGOON1,      SyncItems),
        G("Dragoon Part B", item_state, ITUNLOCK_DRAGOON2,      SyncItems),
        G("Dragoon Part C", item_state, ITUNLOCK_DRAGOON3,      SyncItems),
    },
};

static MenuDesc boxes_menu = {
    .option_num = 6,
    .options = {
        A("Unlock All", "Unlock all box types", BoxUnlockAll),
        A("Lock All",   "Lock all box types",   BoxLockAll),
        A("Give Random", "Queue one random box type unlock item", BoxGiveRandom),
        G("Blue Box",  box_state, BOXKIND_BLUE,  SyncBoxes),
        G("Green Box", box_state, BOXKIND_GREEN, SyncBoxes),
        G("Red Box",   box_state, BOXKIND_RED,   SyncBoxes),
    },
};

// A sphere held locked here is kept out of the custom_items registry entirely, so
// it takes no delivery step and no carrier box can hold it.
static MenuDesc star_pieces_menu = {
    .option_num = 9,
    .options = {
        A("Unlock All", "Unlock all AP Star spheres", SphUnlockAll),
        A("Lock All",   "Lock all AP Star spheres",   SphLockAll),
        A("Give Random", "Queue one random AP Star sphere unlock item", SphGiveRandom),
        G("Rose Sphere",   star_piece_state, AP_STAR_PIECE_ROSE,   SyncStarPiece),
        G("Green Sphere",  star_piece_state, AP_STAR_PIECE_GREEN,  SyncStarPiece),
        G("Violet Sphere", star_piece_state, AP_STAR_PIECE_VIOLET, SyncStarPiece),
        G("Tan Sphere",    star_piece_state, AP_STAR_PIECE_TAN,    SyncStarPiece),
        G("Blue Sphere",   star_piece_state, AP_STAR_PIECE_BLUE,   SyncStarPiece),
        G("Yellow Sphere", star_piece_state, AP_STAR_PIECE_YELLOW, SyncStarPiece),
    },
};

static MenuDesc ar_stages_menu = {
    .option_num = 12,
    .options = {
        A("Unlock All", "Unlock all AR stages", ArsUnlockAll),
        A("Lock All",   "Lock all AR stages",   ArsLockAll),
        A("Give Random", "Queue one random AR stage unlock item", ArsGiveRandom),
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
    .option_num = 10,
    .options = {
        A("Unlock All", "Unlock all TR stages", TrsUnlockAll),
        A("Lock All",   "Lock all TR stages",   TrsLockAll),
        A("Give Random", "Queue one random TR stage unlock item", TrsGiveRandom),
        G("Grass", tr_stage_state, TOPRIDE_GRASS, SyncTRStages),
        G("Sand",  tr_stage_state, TOPRIDE_SAND,  SyncTRStages),
        G("Sky",   tr_stage_state, TOPRIDE_SKY,   SyncTRStages),
        G("Fire",  tr_stage_state, TOPRIDE_FIRE,  SyncTRStages),
        G("Light", tr_stage_state, TOPRIDE_LIGHT, SyncTRStages),
        G("Water", tr_stage_state, TOPRIDE_WATER, SyncTRStages),
        G("Metal", tr_stage_state, TOPRIDE_METAL, SyncTRStages),
    },
};

// One row per TRITEM kind, in enum order - Tri(Unlock|Lock)All drive the whole
// TRITEM_NUM mask, so a missing row would leave a gate the menu cannot show.
static MenuDesc tr_items_menu = {
    .option_num = 3 + TRITEM_NUM,
    .options = {
        A("Unlock All", "Unlock all TR items", TriUnlockAll),
        A("Lock All",   "Lock all TR items",   TriLockAll),
        A("Give Random", "Queue one random TR item unlock item", TriGiveRandom),
        G("Hammer",            tr_item_state, TRITEM_HAMMER,           SyncTRItems),
        G("Big Cake",          tr_item_state, TRITEM_BIG_CAKE,         SyncTRItems),
        G("Speed Up",          tr_item_state, TRITEM_SPEED_UP,         SyncTRItems),
        G("Speed Down",        tr_item_state, TRITEM_SPEED_DOWN,       SyncTRItems),
        G("Spinner",           tr_item_state, TRITEM_SPINNER,          SyncTRItems),
        G("Charge Tank",       tr_item_state, TRITEM_CHARGE_TANK,      SyncTRItems),
        G("Invincible Candy",  tr_item_state, TRITEM_INVINCIBLE_CANDY, SyncTRItems),
        G("Buzz Saw",          tr_item_state, TRITEM_BUZZ_SAW,         SyncTRItems),
        G("Drill",             tr_item_state, TRITEM_DRILL,            SyncTRItems),
        G("Freeze Fan",        tr_item_state, TRITEM_FREEZE_FAN,       SyncTRItems),
        G("Missile",           tr_item_state, TRITEM_MISSILE,          SyncTRItems),
        G("Fire",              tr_item_state, TRITEM_FIRE,             SyncTRItems),
        G("Party Ball (alt)",  tr_item_state, TRITEM_PARTY_BALL_ALT,   SyncTRItems),
        G("Bomb",              tr_item_state, TRITEM_BOMB,             SyncTRItems),
        G("Step-boom",         tr_item_state, TRITEM_STEP_BOOM,        SyncTRItems),
        G("Lantern",           tr_item_state, TRITEM_LANTERN,          SyncTRItems),
        G("Walky",             tr_item_state, TRITEM_WALKY,            SyncTRItems),
        G("Kracko",            tr_item_state, TRITEM_KRACKO,           SyncTRItems),
        G("Who? Paint",        tr_item_state, TRITEM_WHO_PAINT,        SyncTRItems),
        G("Smokescreen",       tr_item_state, TRITEM_SMOKESCREEN,      SyncTRItems),
        G("Chickie",           tr_item_state, TRITEM_CHICKIE,          SyncTRItems),
        G("Party Ball",        tr_item_state, TRITEM_PARTY_BALL,       SyncTRItems),
    },
};

static MenuDesc colors_menu = {
    .option_num = 11,
    .options = {
        A("Unlock All", "Unlock all colors", ClrUnlockAll),
        A("Lock All",   "Lock all colors",   ClrLockAll),
        A("Give Random", "Queue one random color unlock item", ClrGiveRandom),
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
    .option_num = 27,
    .options = {
        A("Unlock All", "Unlock all stadiums", StdUnlockAll),
        A("Lock All",   "Lock all stadiums",   StdLockAll),
        A("Give Random", "Queue one random stadium unlock item", StdGiveRandom),
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
        A("Boost Patch",     "Give Boost patch",     GiveBoost),
        A("Top Speed Patch", "Give Top Speed patch", GiveTopSpd),
        A("Turn Patch",      "Give Turn patch",      GiveTurn),
        A("Charge Patch",    "Give Charge patch",    GiveCharge),
        A("Glide Patch",     "Give Glide patch",     GiveGlide),
        A("Offense Patch",   "Give Offense patch",   GiveOffense),
        A("Defense Patch",   "Give Defense patch",   GiveDefense),
        A("Weight Patch",    "Give Weight patch",    GiveWeight),
        A("All Up",          "Spawn an All Up pickup", GiveAllUp),
    },
};

static MenuDesc give_perm_patches_menu = {
    .option_num = 10,
    .options = {
        A("Perm HP",        "Give permanent HP patch",        GivePermHP),
        A("Perm Boost",     "Give permanent Boost patch",     GivePermBoost),
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
        A("Freeze",  "Give Freeze ability",  GiveCopyFreeze),
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

static MenuDesc give_base_abilities_menu = {
    .option_num = 3,
    .options = {
        A("Unlock Inhale",     "Grant the Inhale unlock item",     GiveUnlockInhale),
        A("Unlock Quick Spin", "Grant the Quick Spin unlock item", GiveUnlockQuickSpin),
        A("Unlock Charge",     "Grant the Charge unlock item",     GiveUnlockCharge),
    },
};

static MenuDesc give_spheres_menu = {
    .option_num = 6,
    .options = {
        A("Rose Sphere",   "Grant the Rose sphere unlock item",   GiveSphereRose),
        A("Green Sphere",  "Grant the Green sphere unlock item",  GiveSphereGreen),
        A("Violet Sphere", "Grant the Violet sphere unlock item", GiveSphereViolet),
        A("Tan Sphere",    "Grant the Tan sphere unlock item",    GiveSphereTan),
        A("Blue Sphere",   "Grant the Blue sphere unlock item",   GiveSphereBlue),
        A("Yellow Sphere", "Grant the Yellow sphere unlock item", GiveSphereYellow),
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
    .option_num = 15,
    .options = {
        A("Dragoon Part A", "Give Dragoon Part A", GiveDragoonA),
        A("Dragoon Part B", "Give Dragoon Part B", GiveDragoonB),
        A("Dragoon Part C", "Give Dragoon Part C", GiveDragoonC),
        A("Hydra Part X",   "Give Hydra Part X",   GiveHydraX),
        A("Hydra Part Y",   "Give Hydra Part Y",   GiveHydraY),
        A("Hydra Part Z",   "Give Hydra Part Z",   GiveHydraZ),
        A("Rose Sphere",    "Collect the Rose sphere",   GiveSphereItemRose),
        A("Green Sphere",   "Collect the Green sphere",  GiveSphereItemGreen),
        A("Violet Sphere",  "Collect the Violet sphere", GiveSphereItemViolet),
        A("Tan Sphere",     "Collect the Tan sphere",    GiveSphereItemTan),
        A("Blue Sphere",    "Collect the Blue sphere",   GiveSphereItemBlue),
        A("Yellow Sphere",  "Collect the Yellow sphere", GiveSphereItemYellow),
        A("Give Dragoon",   "Assemble full Dragoon",  GiveDragoon),
        A("Give Hydra",     "Assemble full Hydra",    GiveHydra),
        A("Give AP Star",   "Assemble the full Archipelago Star", GiveApStar),
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
        A("Fog",               "Trigger Fog event",               GiveEvtFog),
        A("Fake Powerups",     "Trigger Fake Powerups event",     GiveEvtFakePowerups),
    },
};

static MenuDesc give_traps_menu = {
    .option_num = 3,
    .options = {
        A("1 HP Trap",         "Set HP to 1",                 Give1HPTrap),
        A("All Down",          "All stats down",              GiveAllDown),
        A("Drop Patches Trap", "Eject rider patches (CT)",    GiveDropPatchesTrap),
    },
};

static MenuDesc give_upgrades_menu = {
    .option_num = 9,
    .options = {
        A("All Up",              "Grant the AP All Up item to each rider", GiveApAllUp),
        A("Patch Cap Increase",  "Increase patch cap",       GivePatchCap),
        A("Spawn Rate Up",       "Increase item spawn rate", GiveSpawnRateUp),
        A("AR Checkbox Filler",  "Fill AR checklist square", GiveFillerAR),
        A("TR Checkbox Filler",  "Fill TR checklist square", GiveFillerTR),
        A("CT Checkbox Filler",  "Fill CT checklist square", GiveFillerCT),
        A("AP Checkbox Filler",  "Fill AP checklist square", GiveFillerAP),
        A("Big Kirby",           "Scale Kirby model up (x1.5)",   GiveBigKirby),
        A("Small Kirby",         "Scale Kirby model down (x0.5)", GiveSmallKirby),
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
    .option_num = 12,
    .options = {
        S("Stat Patches",      "Temporary stat patches",          give_stat_patches_menu),
        S("Permanent Patches", "Permanent stat boosts",           give_perm_patches_menu),
        S("Copy Abilities",    "Give Kirby a copy ability",       give_abilities_menu),
        S("Base Ability Unlocks", "Grant a base-ability unlock",  give_base_abilities_menu),
        S("AP Star Spheres",   "Grant a sphere unlock item",      give_spheres_menu),
        S("Food",              "Healing items",                   give_food_menu),
        S("Special Items",     "Powerful one-use items",          give_special_menu),
        S("Legendary Pieces",  "Dragoon, Hydra and AP Star parts", give_legendary_menu),
        S("Top Ride Items",    "Apply a Top Ride item to each human Kirby", give_topride_items_menu),
        S("CT Events",         "Trigger a City Trial event",      give_events_menu),
        S("Traps",             "Traps and stat drops",            give_traps_menu),
        S("Upgrades",          "Progression upgrades and fillers", give_upgrades_menu),
    },
};

static MenuDesc reveal_menu = {
    .option_num = 5,
    .options = {
        A("All Checklists", "Reveal every checkbox on every checklist", CheckDbgRevealAll),
        A("Air Ride",       "Reveal the Air Ride checklist",            CheckDbgRevealAirRide),
        A("Top Ride",       "Reveal the Top Ride checklist",            CheckDbgRevealTopRide),
        A("City Trial",     "Reveal the City Trial checklist",          CheckDbgRevealCityTrial),
        A("Archipelago",    "Reveal the Archipelago checklist",         CheckDbgRevealArchipelago),
    },
};

static MenuDesc checks_menu = {
    .option_num = 10,
    .options = {
        &(OptionDesc){
            .name = "Auto-Grant on Z Unlock",
            .description = "On: Z-unlock also grants the cell reward. Off: only send the check and let the AP client deliver.",
            .kind = OPTKIND_VALUE,
            .val = &auto_grant_on_debug_unlock,
            .value_num = 2,
            .value_names = toggle_values,
            .on_change = OnAutoGrantChange,
        },
        A("Clear All sent_checks",   "Wipe sent_checks bitmask and goal_complete", CheckDbgClearAll),
        A("Force-Mark All",          "Set every backed sent_checks bit, goal_complete and goal_announced", CheckDbgForceMarkAll),
        A("Trigger goal_complete",   "Set only goal_complete (sent_checks unchanged)", CheckDbgTriggerGoal),
        S("Reveal Checklists",       "Make checkboxes visible (visual only)",       reveal_menu),
        A("Simulate Location Data",  "Fill location arrays with a random shuffle",  CheckDbgSimulateLocationData),
        A("Clear All Checklist Data", "Wipe every checkbox flag, sent_checks, and location shuffle", CheckDbgClearAllChecklistData),
        &(OptionDesc){
            .name = "AP Patches",
            .description = "Override the seed's AP Patch location count; the drop-ins register at the next round load.",
            .kind = OPTKIND_VALUE,
            .no_save = 1,
            .val = &ap_patch_count_state,
            .value_num = 4,
            .value_names = ap_patch_count_values,
            .on_change = OnApPatchCountChange,
        },
        A("Collect AP Patch",        "Claim the lowest unclaimed AP Patch",         ApPatchDbgCollect),
        A("Clear Collected AP Patches", "Clear every collected bit, so they can be claimed again", ApPatchDbgClearCollected),
    },
};

static MenuDesc gates_menu = {
    .option_num = 13,
    .options = {
        S("Machines",        "Toggle machine unlock gates",     machines_menu),
        S("Copy Abilities",  "Toggle ability unlock gates",     abilities_menu),
        S("Base Abilities",  "Toggle base ability gates",       base_abilities_menu),
        S("Events",          "Toggle event unlock gates",       events_menu),
        S("Patch Types",     "Toggle patch type unlock gates",  patches_menu),
        S("CT Items",        "Toggle CT item unlock gates",     items_menu),
        S("Box Types",       "Toggle box type unlock gates",    boxes_menu),
        S("AP Star Spheres", "Toggle AP Star sphere gates",     star_pieces_menu),
        S("AR Stages",       "Toggle Air Ride stage gates",     ar_stages_menu),
        S("TR Stages",       "Toggle Top Ride stage gates",     tr_stages_menu),
        S("TR Items",        "Toggle Top Ride item gates",      tr_items_menu),
        S("Colors",          "Toggle Kirby color gates",        colors_menu),
        S("Stadiums",        "Toggle stadium unlock gates",     stadiums_menu),
    },
};

static MenuDesc goals_menu = {
    .option_num = 6,
    .options = {
        V("Air Ride",     "Goal for the Air Ride checklist row",    goal_state[GMMODE_AIRRIDE],   goal_values, 0),
        V("Top Ride",     "Goal for the Top Ride checklist row",    goal_state[GMMODE_TOPRIDE],   goal_values, 0),
        V("City Trial",   "Goal for the City Trial checklist row",  goal_state[GMMODE_CITYTRIAL], goal_values, 0),
        V("Archipelago",  "Goal for the Archipelago checklist row", goal_state[AP_CHECKLIST_ROW], goal_values, 0),
        V("Squares for N", "Square count the N Squares rows compare against", goal_amount_state, goal_amount_values, 0),
        A("Apply", "Commit the five rows above and re-evaluate", GoalDbgApply),
    },
};

static MenuDesc gating_menu = {
    .option_num = 12,
    .options = {
        T("Machines",       "On: AP ships machine unlock items",       gating_state[AP_UNLOCK_MACHINE],       OnGateMachines),
        T("Copy Abilities", "On: AP ships ability unlock items",       gating_state[AP_UNLOCK_ABILITY],       OnGateAbilities),
        T("Events",         "On: AP ships event unlock items",         gating_state[AP_UNLOCK_EVENT],         OnGateEvents),
        T("Patch Types",    "On: AP ships patch type unlock items",    gating_state[AP_UNLOCK_PATCH],         OnGatePatches),
        T("CT Items",       "On: AP ships CT item unlock items",       gating_state[AP_UNLOCK_ITEM],          OnGateItems),
        T("Box Types",      "On: AP ships box type unlock items",      gating_state[AP_UNLOCK_BOX],           OnGateBoxes),
        T("AR Stages",      "On: AP ships Air Ride stage unlocks",     gating_state[AP_UNLOCK_AIRRIDE_STAGE], OnGateARStages),
        T("TR Stages",      "On: AP ships Top Ride stage unlocks",     gating_state[AP_UNLOCK_TOPRIDE_STAGE], OnGateTRStages),
        T("TR Items",       "On: AP ships Top Ride item unlocks",      gating_state[AP_UNLOCK_TOPRIDE_ITEM],  OnGateTRItems),
        T("Colors",         "On: AP ships Kirby color unlock items",   gating_state[AP_UNLOCK_COLOR],         OnGateColors),
        T("Stadiums",       "On: AP ships stadium unlock items",       gating_state[AP_UNLOCK_STADIUM],       OnGateStadiums),
        T("Base Abilities", "On: AP ships base ability unlock items",  gating_state[AP_UNLOCK_BASE_ABILITY],  OnGateBaseAbil),
    },
};

static MenuDesc slot_options_menu = {
    .option_num = 5,
    .options = {
        S("Category Gating", "Which categories AP ships unlock items for", gating_menu),
        V("Patch Cap Min",   "Per-stat cap a City Trial run starts at",    patch_cap_min_state, patch_cap_values,  OnPatchCapMinChange),
        V("Patch Cap Max",   "Ceiling Patch Cap Increase items raise it to", patch_cap_max_state, patch_cap_values, OnPatchCapMaxChange),
        V("Spawn Rate Floor", "Item spawn rate before any Spawn Rate Up",  spawn_rate_state,    spawn_rate_values, OnSpawnRateChange),
        A("Re-apply", "Clear every unlock mask and re-run the connect-time pre-fill", SlotOptDbgReapply),
    },
};

static MenuDesc progress_menu = {
    .option_num = 3,
    .options = {
        V("All Ups Collected", "Lifetime CT All Ups, toward the 5 that check needs",  allup_state,      allup_values,       OnAllUpProgressChange),
        V("Purple SR1 Wins",   "SINGLE RACE 1 wins as Purple Kirby, toward 3",        purple_state,     purple_values,      OnPurpleProgressChange),
        V("Race Colors",       "Which Kirby colors have finished an Air Ride race",   race_color_state, race_color_values,  OnRaceColorProgressChange),
    },
};

static MenuDesc messages_menu = {
    .option_num = 7,
    .options = {
        A("Check",    "Post a canned check line",                  MsgDbgCheck),
        A("Item",     "Post a canned item line",                   MsgDbgItem),
        A("Hint",     "Post a canned hint line",                   MsgDbgHint),
        A("Status",   "Post a canned status line",                 MsgDbgStatus),
        A("Chat",     "Post a canned chat line",                   MsgDbgChat),
        A("Link",     "Post a canned DeathLink/TrapLink line",     MsgDbgLink),
        A("Overlong", "Post a line of 8 runs past the third line", MsgDbgOverlong),
    },
};

static MenuDesc links_menu = {
    .option_num = 2,
    .options = {
        A("Arm DeathLink", "Set deathlink_receive; it lands at the next round", LinkDbgDeathlink),
        A("Arm TrapLink",  "Set traplink_receive; the mode picks the trap",     LinkDbgTraplink),
    },
};

static MenuDesc energy_menu = {
    .option_num = 3,
    .options = {
        V("Balance",       "Set the EnergyLink balance in MJ", energy_state, energy_values, OnEnergyBalanceChange),
        A("Add 1000",      "Add 1000 MJ to the balance",       GiveEnergy1000),
        A("Drain to Zero", "Set the balance to 0 MJ",          EnergyDbgDrain),
    },
};

static MenuDesc debug_menu = {
    .option_num = 12,
    .options = {
        S("Unlock Gates",   "Toggle the unlock gate masks by category",     gates_menu),
        S("Give Items",     "Give items directly (free)",                   give_items_menu),
        S("Checks",         "Check detection and goal debug",               checks_menu),
        S("Goals",          "Override the goal of each checklist row",      goals_menu),
        S("Slot Options",   "Override the options the seed shipped",        slot_options_menu),
        S("Check Progress", "Cross-session AP checklist counters",          progress_menu),
        S("Messages",       "Render canned client text messages",           messages_menu),
        S("Links",          "Arm a DeathLink or TrapLink receive",          links_menu),
        S("EnergyLink",     "Set the energy balance the spend menu reads",  energy_menu),
        V("Target Player",  "Player slot the pad drops act on", target_player_state, target_player_values, OnTargetPlayerChange),
        A("Report State",   "Log every mask, counter and goal to the console",    StateDbgReport),
        A("Reset Progression", "Roll received-item progression back to pre-connect", StateDbgResetProgression),
    },
};

OptionDesc DebugMod_RootOption = {
    .name = "Archipelago Debug",
    .description = "Toggle gate unlocks, give items, and exercise check detection.",
    .kind = OPTKIND_MENU,
    .menu_ptr = &debug_menu,
};

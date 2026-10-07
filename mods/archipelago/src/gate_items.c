#include "game.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_items.h"
#include "gate_boxes.h"
#include "textbox_api.h"
#include "inline.h"
#include "ap_announce.h"

static const ItemKind itunlock_to_itkind[ITUNLOCK_NUM] = {
    [ITUNLOCK_ALLUP]            = ITKIND_ALLUP,
    [ITUNLOCK_SPEEDMAX]         = ITKIND_SPEEDMAX,
    [ITUNLOCK_SPEEDMIN]         = ITKIND_SPEEDMIN,
    [ITUNLOCK_OFFENSEMAX]       = ITKIND_OFFENSEMAX,
    [ITUNLOCK_DEFENSEMAX]       = ITKIND_DEFENSEMAX,
    [ITUNLOCK_CHARGEMAX]        = ITKIND_CHARGEMAX,
    [ITUNLOCK_CHARGENONE]       = ITKIND_CHARGENONE,
    [ITUNLOCK_CANDY]            = ITKIND_CANDY,
    [ITUNLOCK_FOODMAXIMTOMATO]  = ITKIND_FOODMAXIMTOMATO,
    [ITUNLOCK_FOODENERGYDRINK]  = ITKIND_FOODENERGYDRINK,
    [ITUNLOCK_FOODICECREAM]     = ITKIND_FOODICECREAM,
    [ITUNLOCK_FOODRICEBALL]     = ITKIND_FOODRICEBALL,
    [ITUNLOCK_FOODCHICKEN]      = ITKIND_FOODCHICKEN,
    [ITUNLOCK_FOODCURRY]        = ITKIND_FOODCURRY,
    [ITUNLOCK_FOODRAMEN]        = ITKIND_FOODRAMEN,
    [ITUNLOCK_FOODOMELET]       = ITKIND_FOODOMELET,
    [ITUNLOCK_FOODHAMBURGER]    = ITKIND_FOODHAMBURGER,
    [ITUNLOCK_FOODSUSHI]        = ITKIND_FOODSUSHI,
    [ITUNLOCK_FOODHOTDOG]       = ITKIND_FOODHOTDOG,
    [ITUNLOCK_FOODAPPLE]        = ITKIND_FOODAPPLE,
    [ITUNLOCK_FIREWORKS]        = ITKIND_FIREWORKS,
    [ITUNLOCK_PANICSPIN]        = ITKIND_PANICSPIN,
    [ITUNLOCK_SENSORBOMB]       = ITKIND_SENSORBOMB,
    [ITUNLOCK_GORDO]            = ITKIND_GORDO,
    [ITUNLOCK_HYDRA1]           = ITKIND_HYDRA1,
    [ITUNLOCK_HYDRA2]           = ITKIND_HYDRA2,
    [ITUNLOCK_HYDRA3]           = ITKIND_HYDRA3,
    [ITUNLOCK_DRAGOON1]         = ITKIND_DRAGOON1,
    [ITUNLOCK_DRAGOON2]         = ITKIND_DRAGOON2,
    [ITUNLOCK_DRAGOON3]         = ITKIND_DRAGOON3,
};

static const char *ItemUnlockName(ItemUnlockKind kind)
{
    return ItemKind_Names[itunlock_to_itkind[kind]];
}

// The unlock bit an ItemKind is gated on, or -1 for one this gate doesn't cover.
static int ItemKindToUnlockBit(u8 it_kind)
{
    for (int bit = 0; bit < ITUNLOCK_NUM; bit++)
    {
        if (itunlock_to_itkind[bit] == it_kind)
            return bit;
    }
    return -1;
}

int GateItems_IsItemLocked(u8 it_kind)
{
    int bit = ItemKindToUnlockBit(it_kind);
    return bit >= 0 && !(ap_save->item_unlocked_mask & (1 << bit));
}

// One bit per unlock index whose locked-spawn skip has been reported this round.
static u32 stc_locked_reported;

// Disables a legendary's piece spawns when none of its parts is unlocked, or all of them
// while their red carrier box is locked.
static void GateItems_FilterLegendaryPieces()
{
    stc_locked_reported = 0;

    LegendaryPieceData *lpd = *stc_legendary_piece_data;
    if (!lpd)
        return;

    // The carrier hardcodes red and never reaches the box color picker, so box gating
    // has to be applied here or a locked Red still delivers pieces.
    if (!GateBoxes_IsUnlocked(BOXKIND_RED))
    {
        lpd->machine[0].is_enabled = 0;
        lpd->machine[1].is_enabled = 0;
        OSReport("[GateItems] Legendary pieces disabled (Red Box locked)\n");
        return;
    }

    u32 mask = ap_save->item_unlocked_mask;

    u32 dragoon_bits = (1 << ITUNLOCK_DRAGOON1) | (1 << ITUNLOCK_DRAGOON2) | (1 << ITUNLOCK_DRAGOON3);
    if (!(mask & dragoon_bits))
    {
        lpd->machine[0].is_enabled = 0;
        OSReport("[GateItems] Legendary Dragoon disabled (no pieces unlocked)\n");
    }

    u32 hydra_bits = (1 << ITUNLOCK_HYDRA1) | (1 << ITUNLOCK_HYDRA2) | (1 << ITUNLOCK_HYDRA3);
    if (!(mask & hydra_bits))
    {
        lpd->machine[1].is_enabled = 0;
        OSReport("[GateItems] Legendary Hydra disabled (no pieces unlocked)\n");
    }
}

// Hook after LegendaryPieces_Init returns in CityItemSpawn_Init (0x800ebf70).
CODEPATCH_HOOKCREATE(0x800ec284,
    "",
    GateItems_FilterLegendaryPieces,
    "",
    0
)

// Replaces both bl LegendaryPiece_MarkAsSpawned in CityItemSpawn_SpawnLegendaryPiece
// (0x800ed384). Skipping it leaves the box on its default forced_item (-1, a random roll);
// the caller still advances next_piece_index.
static void GateItems_MarkAsSpawnedGated(GOBJ *box, int item_kind)
{
    int bit = ItemKindToUnlockBit(item_kind);
    if (bit >= 0 && !(ap_save->item_unlocked_mask & (1 << bit)))
    {
        if (!(stc_locked_reported & (1u << bit)))
        {
            stc_locked_reported |= (1u << bit);
            OSReport("[GateItems] Legendary piece %d (%s) locked, spawn skipped\n",
                     item_kind, ItemUnlockName(bit));
        }
        return;
    }
    LegendaryPiece_MarkAsSpawned(box, item_kind);
}

// Slot 0 of every UFO ring is a hardcoded All Up, past the event-drop table the spawn
// filter zeroes. A locked All Up rolls the UFO column like the other slots, and a -1
// there leaves the slot empty.
static ItemKind GateItems_UfoRingLeadItem(void)
{
    if (GateItems_IsItemLocked(ITKIND_ALLUP))
        return CityItem_GetEventItem(EVDROP_UFO);
    return ITKIND_ALLUP;
}

// The li r29, ITKIND_ALLUP in each UFO stop's ring loop. The b after it skips the pool
// roll and lands on the mr r29, r3 that roll returns through.
static const u32 ufo_ring_lead_sites[] = {
    0x8010b268, // CityUFO_State0Think (0x8010b024)
    0x8010b958, // CityUFO_State1Think (0x8010b714)
    0x8010c0cc, // CityUFO_State2Think (0x8010be88)
    0x8010c7a4, // CityUFO_State3Think (0x8010c560)
    0x8010ce44, // CityUFO_State4Think (0x8010cca4)
};

// Replaces the bl CityItem_Throw at 0x8021ddf4 in DynaBlade_ThrowItems (0x8021db44), her
// one hardcoded throw: the All Up she gives up once enough damage lands. A locked All Up
// gives way to one roll of her regular column.
static void GateItems_DynaBladeThrowReward(ItemKind kind, int spawn_group, Vec3 *pos, Vec3 *dir,
                                           int flags, f32 elev_angle, f32 speed)
{
    if (GateItems_IsItemLocked(kind))
    {
        kind = CityItem_GetEventItem(EVDROP_DYNA);
        if (kind < 0)
            return;
    }
    CityItem_Throw(kind, spawn_group, pos, dir, flags, elev_angle, speed);
}

void GateItems_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x800ec284);
    CODEPATCH_REPLACECALL(0x800ed41c, GateItems_MarkAsSpawnedGated); // Dragoon piece
    CODEPATCH_REPLACECALL(0x800ed49c, GateItems_MarkAsSpawnedGated); // Hydra piece

    for (int i = 0; i < (int)GetElementsIn(ufo_ring_lead_sites); i++)
    {
        u32 site = ufo_ring_lead_sites[i];
        CODEPATCH_REPLACECALL(site, GateItems_UfoRingLeadItem);
        CODEPATCH_REPLACEINSTRUCTION(site + 4, 0x4800000c); // b +0xc, onto mr r29, r3
    }
    CODEPATCH_REPLACECALL(0x8021ddf4, GateItems_DynaBladeThrowReward);
    OSReport("[GateItems] Hooks installed\n");
}

int GateItems_UnlockItem(ItemUnlockKind kind)
{
    if (kind >= ITUNLOCK_NUM)
        return 0;

    ap_save->item_unlocked_mask |= (1 << kind);
    OSReport("[GateItems] Item %d (%s) unlocked (mask = %s)\n",
             kind, ItemUnlockName(kind), MaskBits(ap_save->item_unlocked_mask, ITUNLOCK_NUM));
    APAnnounce_Grant("Unlocked Item: ", ItemUnlockName(kind), tb_api->ItemColor, NULL);
    return 1;
}

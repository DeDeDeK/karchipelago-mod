// Makes both character select screens able to lay out one more icon than the game
// was built for, and packs their icon lists.
//
// Three things cap the grids at 20. The name-plate art banks put King Dedede on
// the frame an appended character needs. The packed icon list in GameData is a
// 20-byte field with a live neighbour. And the icon positions come from a strip of
// 20 anchor joints posed by an animation whose frame is the icon count, with no
// key past 20 and no 21st anchor to pose. The art banks in assets/ carry the
// appended entry; the rest is here.
//
// Packing is here too, because both screens build their lists by walking the
// character grid this mod widened: vanilla's loops are a hard 10 columns into two
// 10-byte stack rows, so an 11th column has nowhere to land and an appended
// character is invisible however well it is registered. Who fills the list is a
// consumer's decision, taken through an availability filter - with none set the
// engine's own roster is reproduced, so a drop-in machine works standalone.

#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "menu.h"
#include "game.h"
#include "scene.h"
#include "code_patch/code_patch.h"

#include "custom_machines.h"

// The `addi rD, rS, 20` that forms King Dedede's name-plate frame, in the Air Ride
// and City Trial CSS plate setters and then the four results screens. Each screen
// has a set and an update path carrying its own copy.
static const u32 stc_plate_frame_sites[] = {
    0x80151b08, 0x80151bd4,
    0x8015c5c8, 0x8015c694,
    0x801672bc, 0x8016b064,
    0x8016e9bc, 0x80177b5c,
};

#define PLATE_DEDEDE_FRAME 21

// Every `lbz`/`stb` at select base +0x7a: the Air Ride row-layout flag, then City
// Trial's debug-grid flag. Both sit one byte past their screen's packed icon list,
// so the 21st entry lands on them. +0x7d is untouched by either screen and inside
// the block each screen's init memsets, so that is where they move.
#define RELOCATED_FLAG_OFF 0x7d

static const u32 stc_airride_flag_sites[] = {
    0x80020b04, 0x80020b4c, 0x80020b98, 0x800214a4,
    0x80027f60, 0x800285c8, 0x80028818, 0x80028970, 0x80029c7c,
};

static const u32 stc_city_flag_sites[] = {
    0x8002e444, 0x80038d00, 0x8003a150, 0x8003a15c, 0x8003ac3c, 0x8003ac48,
};

// Icons the widened list and the layout below can carry. One past the vanilla 20,
// which is what a single appended character needs and all the packed list has room
// for once its neighbour moves.
#define SELECT_ICON_MAX 21

// Anchor joints each screen's ipos model ships with, and so the icons the engine
// can pose by itself.
#define ANCHOR_NUM 20

// Both screens keep the icons they offer in the same shape at their own base in
// GameData: a count, then one CharacterKind per icon. City Trial's base arrives in
// a register; Air Ride's is a fixed offset.
#define AIRRIDE_SELECT_BASE 0x10a
#define SELECT_COUNT        0x65
#define SELECT_LIST         0x66
#define SELECT_DEBUG_GRID   0x7b

// Columns per row before this mod widens the grid, and so also the count at which
// a drawn row is full and the icons wrap to two.
#define VANILLA_GRID_COLS 10

// ipos GObj userdata: one Vec3 per icon at +0x60, then the scale every icon shares
// and the icon count - in the opposite order on the two screens, so the scale's
// offset comes from the layout below.
#define IPOS_POSITIONS 0x60

// Where an icon sits, in columns from the left edge of the block the two rows
// span. The top row is ceil(N/2) columns wide; the bottom row holds the rest and
// starts half a column in, so the rows interleave.
typedef struct IconLayout
{
    float half_width;  // half the block both rows span, in world units
    float top_y;
    float bottom_y;
    int scale_off;     // ipos userdata offset of the shared icon scale
    Vec3 extra;        // icons past the anchor strip, which the userdata has no room for
} IconLayout;

static IconLayout stc_airride = { 30.62f, 5.2122f, -0.8853f, 0x150 };
static IconLayout stc_city = { 25.1992f, 4.2999f, -0.7000f, 0x154 };

// Columns from the block's left edge to its right, which is what the spacing has
// to divide to keep a count inside half_width.
static float RowSpread(int count)
{
    int top = (count + 1) / 2;
    float a = (float)(top - 1);
    float b = (float)(count - top) - 0.5f;
    return a > b ? a : b;
}

// Redo the whole grid arithmetically. Only counts the anchor animation has no key
// for come through here; up to 20 the engine's own pass has already run and is
// left alone.
static void Relayout(IconLayout *lay, int count, GOBJ *ipos)
{
    Vec3 *pos;
    Vec3 *scale;
    float spread, step, shrink, z;
    int top;

    if (ipos == NULL || count <= ANCHOR_NUM)
        return;
    if (count > SELECT_ICON_MAX)
        count = SELECT_ICON_MAX;

    pos = (Vec3 *)((u8 *)ipos->userdata + IPOS_POSITIONS);
    scale = (Vec3 *)((u8 *)ipos->userdata + lay->scale_off);

    spread = RowSpread(count);
    step = 2.0f * lay->half_width / spread;
    shrink = RowSpread(ANCHOR_NUM) / spread;
    top = (count + 1) / 2;
    z = pos[0].Z;

    for (int i = 0; i < count; i++)
    {
        Vec3 *p = (i < ANCHOR_NUM) ? &pos[i] : &lay->extra;
        int col = (i < top) ? i : i - top;

        p->X = -lay->half_width + step * (float)col + ((i < top) ? 0.0f : step * 0.5f);
        p->Y = (i < top) ? lay->top_y : lay->bottom_y;
        p->Z = z;
    }

    // The icons keep filling their column: a tighter grid needs smaller tiles.
    scale->X *= shrink;
    scale->Y *= shrink;
}

static void GetIconPos(IconLayout *lay, GOBJ *ipos, s8 index, Vec3 *out)
{
    if (ipos == NULL)
        return;
    if (index >= ANCHOR_NUM)
        *out = lay->extra;
    else
        *out = ((Vec3 *)((u8 *)ipos->userdata + IPOS_POSITIONS))[index];
}

static void AirRideLayoutIcons(s8 count)
{
    _AirRideSelect_LayoutIcons(count);
    Relayout(&stc_airride, count, Gm_GetMenuData()->airride_select.ipos_gobj);
}

static void AirRideGetIconPos(s8 index, Vec3 *out)
{
    GetIconPos(&stc_airride, Gm_GetMenuData()->airride_select.ipos_gobj, index, out);
}

static void CityLayoutIcons(s8 count)
{
    _CitySelect_LayoutMachineIcons(count);
    Relayout(&stc_city, count, Gm_GetMenuData()->city_select.ipos_gobj);
}

static void CityGetIconPos(s8 index, Vec3 *out)
{
    GetIconPos(&stc_city, Gm_GetMenuData()->city_select.ipos_gobj, index, out);
}

// Each screen's array of icon GObjs holds 20 pointers and its writer indexes it
// unguarded, so a 21st icon overwrites the JOBJSet pointer that follows. Nothing
// reads the array, so the write is undone as soon as it lands.
static void AirRideCreateSIcon(s8 ckind, s8 index)
{
    ScMenuCommon *mc = Gm_GetMenuData();
    JOBJSet *saved = mc->airride_select.ScMenSelplySicon2_scene_models;

    _AirRideSelect_CreateSIcon(ckind, index);
    if (index >= ANCHOR_NUM)
        mc->airride_select.ScMenSelplySicon2_scene_models = saved;
}

static void CityCreateMachineIcon(s8 ckind, s8 index)
{
    ScMenuCommon *mc = Gm_GetMenuData();
    JOBJSet **saved = mc->city_select.ScMenSelplySicon2Ct_scene_models;

    _CitySelect_CreateScMenSelplySiconCt(ckind, index);
    if (index >= ANCHOR_NUM)
        mc->city_select.ScMenSelplySicon2Ct_scene_models = saved;
}

static CustomMachineAvailabilityFilter stc_filter;

void CustomMachineSelect_SetAvailabilityFilter(CustomMachineAvailabilityFilter filter)
{
    stc_filter = filter;
}

// City Trial's per-character check, which the engine only ever inlined into
// CitySelect_CreateMachineIcons and which differs by mode: Stadium offers the 15
// basic characters and nothing else, with no checklist involved, while Free Run
// gates the four specials on rewards and offers the rest unconditionally.
static int CityDefaultAvailable(int ckind)
{
    int reward;

    if (Gm_GetCityMode() != CITYMODE_FREERUN)
        return ckind < CKIND_DRAGOON;

    switch (ckind)
    {
    case CKIND_DRAGOON:    reward = 30; break;
    case CKIND_HYDRA:      reward = 34; break;
    case CKIND_DEDEDE:     reward = 35; break;
    case CKIND_METAKNIGHT: reward = 36; break;
    default:               return 1;
    }

    if (Checklist_IsCacheValid())
        return Checklist_CheckCachedUnlock_CityTrial((s8)reward);
    return ClearChecker_CheckUnlocked(GMMODE_CITYTRIAL, (u8)reward);
}

// Appended characters are unconditional - a drop-in machine has no checklist reward
// behind it. The ceiling rejects the sentinel the widened grid pads its last cell
// with, which has no icon frame to draw.
static int IsAvailable(int ckind, int is_city)
{
    int def;

    if (ckind < 0 || ckind >= CustomMachines_GetCharacterKindCeiling())
        return 0;

    if (ckind >= CKIND_NUM)
        def = 1;
    else
        def = is_city ? CityDefaultAvailable(ckind) : AirRide_CheckCharacterAvailable(ckind);

    return stc_filter ? stc_filter(ckind, def) : def;
}

static int CountAvailable(int is_city)
{
    int n = 0;

    for (int ckind = 0; ckind < CustomMachines_GetCharacterKindCeiling(); ckind++)
    {
        if (IsAvailable(ckind, is_city))
            n++;
    }
    return n;
}

// Fill a screen's packed list. Air Ride takes its order from the one-row strip when
// the icons fit on one drawn row and from the grid otherwise; City Trial always uses
// the grid. Returns the count.
static int PackSelectList(u8 *base, int is_city, int allow_single_row)
{
    int cols = CustomMachineCharacter_GetGridCols();
    int n = 0;

    for (int i = 0; i < SELECT_ICON_MAX; i++)
        base[SELECT_LIST + i] = 0;

    if (allow_single_row && CountAvailable(is_city) < VANILLA_GRID_COLS)
    {
        for (int i = 0; i < CustomMachines_GetCharacterKindCeiling() && n < SELECT_ICON_MAX; i++)
        {
            CharacterKind ckind = SelIcon_GetCKindLinear(i);
            if (IsAvailable(ckind, is_city))
                base[SELECT_LIST + n++] = (u8)ckind;
        }
    }
    else
    {
        for (int row = 0; row < 2 && n < SELECT_ICON_MAX; row++)
        {
            for (int col = 0; col < cols && n < SELECT_ICON_MAX; col++)
            {
                CharacterKind ckind = SelIcon_GetCKind(row, col);
                if (IsAvailable(ckind, is_city))
                    base[SELECT_LIST + n++] = (u8)ckind;
            }
        }
    }

    base[SELECT_COUNT] = (u8)n;
    return n;
}

// Replaces the count store that opens the tail of CitySelect_CreateMachineIcons,
// which flat-copies two 10-byte packing rows into the screen's list. The count goes
// back in r27 for the store this hook displaced.
int CustomMachineSelect_FillCityIcons(u8 *base)
{
    int n = PackSelectList(base, 1, 0);

    CitySelect_LayoutMachineIcons((s8)n);
    for (int i = 0; i < n; i++)
        CitySelect_CreateMachineIcon((s8)base[SELECT_LIST + i], (s8)i);
    return n;
}

int CustomMachineSelect_CountCityAvailable(void)
{
    return CountAvailable(1);
}

// Replaces AirRide_PopulateSelectIcons, whose grid pass packs into two 10-byte stack
// rows and then rebalances them assuming the vanilla grid's fixed positions for the
// legendary machines - neither of which survives an 11th column.
void CustomMachineSelect_PopulateAirRideIcons(void)
{
    u8 *base = (u8 *)Gm_GetGameData() + AIRRIDE_SELECT_BASE;
    int n;

    if (base[SELECT_DEBUG_GRID] && *stc_dblevel > DB_DEBUG_DEVELOP)
    {
        // The debug grid shows every character, gated or not - but still not the
        // sentinel, which has no icon frame.
        int cols = CustomMachineCharacter_GetGridCols();
        n = 0;
        for (int i = 0; i < SELECT_ICON_MAX; i++)
            base[SELECT_LIST + i] = 0;
        for (int row = 0; row < 2 && n < SELECT_ICON_MAX; row++)
        {
            for (int col = 0; col < cols && n < SELECT_ICON_MAX; col++)
            {
                CharacterKind ckind = SelIcon_GetCKind(row, col);
                if (ckind >= 0 && ckind < CustomMachines_GetCharacterKindCeiling())
                    base[SELECT_LIST + n++] = (u8)ckind;
            }
        }
        base[SELECT_COUNT] = (u8)n;
    }
    else
    {
        n = PackSelectList(base, 0, 1);
    }

    CustomMachineSelect_SetAirRideRowSplit(base, n >= VANILLA_GRID_COLS);

    AirRideSelect_LayoutIcons((s8)n);
    for (int i = 0; i < n; i++)
        AirRideSelect_CreateSIcon((s8)base[SELECT_LIST + i], (s8)i);
}

// Mode 1 (Stadium) and mode 2 (Free Run) counting passes of
// CitySelect_CreateMachineIcons. Result -> r27; exit past the loop where the mode is
// rechecked before the array-building pass. The clobbered `li r24, 0` at the mode 2
// site is harmless - r24 is unused after the loop this skips.
CODEPATCH_HOOKCREATE(0x8002e4d0,
    "",
    CustomMachineSelect_CountCityAvailable,
    "mr 27, 3\n\t",
    0x8002e670
)

CODEPATCH_HOOKCREATE(0x8002e5c0,
    "",
    CustomMachineSelect_CountCityAvailable,
    "mr 27, 3\n\t",
    0x8002e670
)

// Tail of CitySelect_CreateMachineIcons. r30 = the City Trial select base; exit past
// the flat copy and the icon loop, both of which this replaces.
CODEPATCH_HOOKCREATE(0x8002f0b8,
    "mr 3, 30\n\t",
    CustomMachineSelect_FillCityIcons,
    "mr 27, 3\n\t",
    0x8002f220
)

int CustomMachineSelect_GetIconMax(void)
{
    return SELECT_ICON_MAX;
}

void CustomMachineSelect_SetAirRideRowSplit(void *select_base, int two_rows)
{
    ((u8 *)select_base)[RELOCATED_FLAG_OFF] = (u8)(two_rows ? 1 : 0);
}

static void MoveFlag(const u32 *sites, int num)
{
    for (int i = 0; i < num; i++)
        CODEPATCH_REPLACEINSTRUCTION(sites[i], (*(u32 *)sites[i] & 0xFFFF0000) | RELOCATED_FLAG_OFF);
}

void CustomMachineSelect_OnBoot(void)
{
    for (int i = 0; i < (int)(sizeof(stc_plate_frame_sites) / sizeof(u32)); i++)
    {
        u32 addr = stc_plate_frame_sites[i];
        CODEPATCH_REPLACEINSTRUCTION(addr, (*(u32 *)addr & 0xFFFF0000) | PLATE_DEDEDE_FRAME);
    }

    MoveFlag(stc_airride_flag_sites, sizeof(stc_airride_flag_sites) / sizeof(u32));
    MoveFlag(stc_city_flag_sites, sizeof(stc_city_flag_sites) / sizeof(u32));

    // AirRide_CheckCharacterAvailable switches on a 20-entry jump table and reaches
    // the checklist query with an uninitialised reward index for anything past it.
    // Send appended characters to the `return 1` arm instead; a gating mod replaces
    // the whole function and never runs this.
    CODEPATCH_REPLACEINSTRUCTION(0x80020924, (*(u32 *)0x80020924 & 0xFFFF0000) | 0x24);

    CODEPATCH_REPLACEFUNC(AirRideSelect_LayoutIcons, AirRideLayoutIcons);
    CODEPATCH_REPLACEFUNC(AirRideSelect_GetIconPos, AirRideGetIconPos);
    CODEPATCH_REPLACEFUNC(AirRideSelect_CreateSIcon, AirRideCreateSIcon);
    CODEPATCH_REPLACEFUNC(CitySelect_LayoutMachineIcons, CityLayoutIcons);
    CODEPATCH_REPLACEFUNC(CitySelect_GetIconPos, CityGetIconPos);
    CODEPATCH_REPLACEFUNC(CitySelect_CreateMachineIcon, CityCreateMachineIcon);

    CODEPATCH_REPLACEFUNC(AirRide_PopulateSelectIcons, CustomMachineSelect_PopulateAirRideIcons);

    CODEPATCH_HOOKAPPLY(0x8002e4d0);  // CT Stadium (mode 1) counting pass
    CODEPATCH_HOOKAPPLY(0x8002e5c0);  // CT Free Run (mode 2) counting pass
    CODEPATCH_HOOKAPPLY(0x8002f0b8);  // CT select list, layout and icons

    // The two array-building passes now have nothing to build: skip each straight to
    // the tail above, which also skips the reorder between them. That reorder assumes
    // vanilla's grid iteration (special characters at fixed col 0/9) and produces
    // duplicate icons on a packed list.
    CODEPATCH_REPLACEINSTRUCTION(0x8002e67c, 0x48000a3c);  // b 0x8002f0b8
    CODEPATCH_REPLACEINSTRUCTION(0x8002e738, 0x48000980);  // b 0x8002f0b8

    // CitySelect_Cursor1InputThink splits cursor rows at num>=10 (`cmpwi r3, 9; ble`),
    // but the grid renderer keeps up to 10 icons on one drawn row and only wraps at
    // 11, so at num==10 the cursor splits 5+5 across a single row. Vanilla CT only
    // produces counts 15-20; a filtered roster can land on exactly 10.
    CODEPATCH_REPLACEINSTRUCTION(0x80031350, 0x2c03000a);  // cmpwi r3, 10

    OSReport("[CustomMachines] Select screens widened to %d icons, Dedede on plate frame %d\n",
             SELECT_ICON_MAX, PLATE_DEDEDE_FRAME);
}

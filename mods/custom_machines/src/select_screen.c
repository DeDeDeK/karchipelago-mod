// Lays out and packs more icons than either character select screen was built for.
// The packed icon list in GameData grows past its vanilla 20 entries, the icon
// positions past the 20th anchor joint are computed rather than posed, and both
// screens' packing is replaced because vanilla's walks a hard 10-column grid this mod
// widened.

#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "menu.h"
#include "game.h"
#include "scene.h"
#include "code_patch/code_patch.h"

#include "custom_machines.h"

// Each screen's select block in GameData, as AirRide_PopulateSelectIcons (0x80020a08) and
// CitySelect_CreateMachineIcons (0x8002e3c4) form it, and where the block keeps its packed
// list: a count byte, then one CharacterKind per icon.
#define AIRRIDE_SELECT_BASE 0x10a
#define CITY_SELECT_BASE    0x1d0
#define SELECT_COUNT        0x65
#define SELECT_LIST         0x66

// The flags vanilla keeps just past a screen's list - Air Ride's row-layout flag at +0x7a
// and debug-grid flag at +0x7b, City Trial's debug-grid flag at +0x7a - move into the
// bytes at +0x11, which no engine code reads, so the list can grow over the ones they
// vacate.
#define AIRRIDE_ROWSPLIT_OFF  0x11
#define AIRRIDE_DEBUGGRID_OFF 0x12
#define CITY_DEBUGGRID_OFF    0x11

// What a rebuild clears: the vanilla list and the flag bytes past it. An icon past this
// span lands on the per-slot controller-claim arrays that follow.
#define AIRRIDE_LIST_SPAN 22
#define CITY_LIST_SPAN    21

// Every `lbz`/`stb` of a flag relative to its screen's select block.
static const u32 stc_airride_rowsplit_sites[] = {
    0x80027f60, 0x800285c8, 0x80028818, 0x80028970, 0x80029c7c,
};
static const u32 stc_airride_debuggrid_sites[] = {
    0x8002881c, 0x8002895c, 0x80028968, 0x80029c64, 0x80029c70,
};
static const u32 stc_city_debuggrid_sites[] = {
    0x8002e444, 0x80038d00, 0x8003a150, 0x8003a15c, 0x8003ac3c, 0x8003ac48,
};

// The debug-grid flags reached from GameData itself: the colour changers' unlock bypass,
// CSS_airRide_colorChanger and CitySelect_ChangeColor, and CitySelect_LoadCityTrial's clear.
static const u32 stc_airride_debuggrid_gamedata_sites[] = { 0x800216d8 };
static const u32 stc_city_debuggrid_gamedata_sites[] = { 0x8002f2bc, 0x80038d98 };

// Positions of the icons past the anchor strip. The ipos userdata's position array ends
// flush against the shared scale, so these are held here instead of extending it.
static Vec3 stc_airride_extra[SELECT_ICON_MAX - SELECT_ICON_ANCHOR_NUM];
static Vec3 stc_city_extra[SELECT_ICON_MAX - SELECT_ICON_ANCHOR_NUM];

// Where an icon sits, in columns from the left edge of the block the two rows span. The
// top row is ceil(N/2) columns wide; the bottom row holds the rest and starts half a
// column in, so the rows interleave. This is what the spacing has to divide to keep a
// count inside the block.
static float RowSpread(int count)
{
    int top = (count + 1) / 2;
    float a = (float)(top - 1);
    float b = (float)(count - top) - 0.5f;
    return a > b ? a : b;
}

// Redo the whole grid arithmetically, for the counts the anchor animation has no key
// for; up to 20 the engine's own pass has already run. The block the rows span is
// measured off the strip the engine just posed, because Air Ride hangs its anchors
// under a joint the layout animation scales; past twenty that animation holds its
// twenty-icon pose, so the strip always arrives in that layout.
static void Relayout(Vec3 *extra, int count, Vec3 *pos, Vec3 *scale)
{
    float left, right, half, centre, top_y, bottom_y, z;
    float spread, step, shrink;
    int top;

    if (count <= SELECT_ICON_ANCHOR_NUM)
        return;
    if (count > SELECT_ICON_MAX)
        count = SELECT_ICON_MAX;

    // The bottom row starts half a column in and so ends half a column past the
    // top row, putting the block's right edge on the last icon of the strip.
    left = pos[0].X;
    right = pos[SELECT_ICON_ANCHOR_NUM - 1].X;
    half = (right - left) * 0.5f;
    centre = (right + left) * 0.5f;
    top_y = pos[0].Y;
    bottom_y = pos[SELECT_ICON_ANCHOR_NUM / 2].Y;
    z = pos[0].Z;

    spread = RowSpread(count);
    step = 2.0f * half / spread;
    shrink = RowSpread(SELECT_ICON_ANCHOR_NUM) / spread;
    top = (count + 1) / 2;

    for (int i = 0; i < count; i++)
    {
        Vec3 *p = (i < SELECT_ICON_ANCHOR_NUM) ? &pos[i] : &extra[i - SELECT_ICON_ANCHOR_NUM];
        int col = (i < top) ? i : i - top;

        p->X = centre - half + step * (float)col + ((i < top) ? 0.0f : step * 0.5f);
        p->Y = (i < top) ? top_y : bottom_y;
        p->Z = z;
    }

    // The icons keep filling their column: a tighter grid needs smaller tiles.
    scale->X *= shrink;
    scale->Y *= shrink;
}

static void GetIconPos(const Vec3 *extra, const Vec3 *pos, s8 index, Vec3 *out)
{
    if (index < 0 || index >= SELECT_ICON_MAX)
        return;
    *out = index >= SELECT_ICON_ANCHOR_NUM ? extra[index - SELECT_ICON_ANCHOR_NUM] : pos[index];
}

static void AirRideLayoutIcons(s8 count)
{
    GOBJ *ipos = Gm_GetMenuData()->airride_select.ipos_gobj;

    _AirRideSelect_LayoutIcons(count);
    if (ipos != NULL)
    {
        AirRideSelectIposData *data = ipos->userdata;
        Relayout(stc_airride_extra, count, data->pos, &data->scale);
    }
}

static void AirRideGetIconPos(s8 index, Vec3 *out)
{
    GOBJ *ipos = Gm_GetMenuData()->airride_select.ipos_gobj;

    if (ipos != NULL)
        GetIconPos(stc_airride_extra, ((AirRideSelectIposData *)ipos->userdata)->pos, index, out);
}

static void CityLayoutIcons(s8 count)
{
    GOBJ *ipos = Gm_GetMenuData()->city_select.ipos_gobj;

    _CitySelect_LayoutMachineIcons(count);
    if (ipos != NULL)
    {
        CitySelectIposData *data = ipos->userdata;
        Relayout(stc_city_extra, count, data->pos, &data->scale);
    }
}

static void CityGetIconPos(s8 index, Vec3 *out)
{
    GOBJ *ipos = Gm_GetMenuData()->city_select.ipos_gobj;

    if (ipos != NULL)
        GetIconPos(stc_city_extra, ((CitySelectIposData *)ipos->userdata)->pos, index, out);
}

// Each screen's array of icon GObjs holds 20 pointers and its writer indexes it
// unguarded, so an icon past the strip walks into the scene-model pointers that
// follow it. Both writers end in the same `extsb`/`slwi`/`add`/`stw` before the
// epilogue, so the store is taken over here and an appended index is dropped.
// Nothing reads either array.
static void StoreAirRideIcon(int index, GOBJ *gobj)
{
    if (index >= 0 && index < SELECT_ICON_ANCHOR_NUM)
        Gm_GetMenuData()->airride_select.sicon_gobj[index] = gobj;
}

static void StoreCityIcon(int index, GOBJ *gobj)
{
    if (index >= 0 && index < SELECT_ICON_ANCHOR_NUM)
        Gm_GetMenuData()->city_select.sicon_gobj[index] = gobj;
}

// r28 is the icon index and r30 the GObj. Exiting past the store leaves the engine's own
// epilogue to run.
CODEPATCH_HOOKCREATE(0x8015181c,
    "extsb 3, 28\n\t"
    "mr 4, 30\n\t",
    StoreAirRideIcon,
    "",
    0x8015182c
)

CODEPATCH_HOOKCREATE(0x8015c2dc,
    "extsb 3, 28\n\t"
    "mr 4, 30\n\t",
    StoreCityIcon,
    "",
    0x8015c2ec
)

static CustomMachineAvailabilityFilter stc_filter;

void CustomMachineSelectScreen_SetAvailabilityFilter(CustomMachineAvailabilityFilter filter)
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
    case CKIND_DRAGOON:    reward = CITYTRIAL_REWARD_DRAGOON; break;
    case CKIND_HYDRA:      reward = CITYTRIAL_REWARD_HYDRA; break;
    case CKIND_DEDEDE:     reward = CITYTRIAL_REWARD_DEDEDE; break;
    case CKIND_METAKNIGHT: reward = CITYTRIAL_REWARD_METAKNIGHT; break;
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
// the grid. `show_all` is Air Ride's debug grid, which offers every character, gated
// or not - but still not the sentinel, which has no icon frame. Returns the count.
static int PackSelectList(u8 *base, int span, int is_city, int allow_single_row, int show_all)
{
    int cols = CustomMachineCharacterRegistry_GetGridCols();
    int ceiling = CustomMachines_GetCharacterKindCeiling();
    int n = 0;

    for (int i = 0; i < span; i++)
        base[SELECT_LIST + i] = 0;

    if (!show_all && allow_single_row && CountAvailable(is_city) < SELICON_GRID_COLS)
    {
        for (int i = 0; i < ceiling && n < SELECT_ICON_MAX; i++)
        {
            CharacterKind ckind = SelIcon_GetCKindLinear(i);
            if (IsAvailable(ckind, is_city))
                base[SELECT_LIST + n++] = (u8)ckind;
        }
    }
    else
    {
        for (int row = 0; row < SELICON_GRID_ROWS && n < SELECT_ICON_MAX; row++)
        {
            for (int col = 0; col < cols && n < SELECT_ICON_MAX; col++)
            {
                CharacterKind ckind = SelIcon_GetCKind(row, col);
                int take = show_all ? (ckind >= 0 && ckind < ceiling)
                                    : IsAvailable(ckind, is_city);
                if (take)
                    base[SELECT_LIST + n++] = (u8)ckind;
            }
        }
    }

    base[SELECT_COUNT] = (u8)n;
    return n;
}

// Replaces the count store that opens the tail of CitySelect_CreateMachineIcons, with
// r30 the City Trial select block: packs the list, lays it out and creates the icons,
// then exits past the vanilla flat copy and icon loop. The count goes back in r27 for
// the store this hook displaced.
static int FillCityIcons(u8 *base)
{
    int n = PackSelectList(base, CITY_LIST_SPAN, 1, 0, 0);

    CitySelect_LayoutMachineIcons((s8)n);
    for (int i = 0; i < n; i++)
        CitySelect_CreateMachineIcon((s8)base[SELECT_LIST + i], (s8)i);
    return n;
}

CODEPATCH_HOOKCREATE(0x8002f0b8,
    "mr 3, 30\n\t",
    FillCityIcons,
    "mr 27, 3\n\t",
    0x8002f220
)

// Replaces AirRide_PopulateSelectIcons, whose grid pass packs into two 10-byte stack
// rows and then rebalances them assuming the vanilla grid's fixed positions for the
// legendary machines - neither of which survives an 11th column.
static void PopulateAirRideIcons(void)
{
    u8 *base = (u8 *)Gm_GetGameData() + AIRRIDE_SELECT_BASE;
    int debug_grid = base[AIRRIDE_DEBUGGRID_OFF] && *stc_dblevel > DB_DEBUG_DEVELOP;
    int n = PackSelectList(base, AIRRIDE_LIST_SPAN, 0, 1, debug_grid);

    base[AIRRIDE_ROWSPLIT_OFF] = n >= SELICON_GRID_COLS;

    AirRideSelect_LayoutIcons((s8)n);
    for (int i = 0; i < n; i++)
        AirRideSelect_CreateSIcon((s8)base[SELECT_LIST + i], (s8)i);
}

static void MoveFlags(const u32 *sites, int num, u32 offset)
{
    for (int i = 0; i < num; i++)
        CustomMachines_SetImmediate(sites[i], offset);
}

#define MOVE_FLAGS(sites, off) MoveFlags(sites, sizeof(sites) / sizeof(sites[0]), off)

void CustomMachineSelectScreen_OnBoot(void)
{
    MOVE_FLAGS(stc_airride_rowsplit_sites, AIRRIDE_ROWSPLIT_OFF);
    MOVE_FLAGS(stc_airride_debuggrid_sites, AIRRIDE_DEBUGGRID_OFF);
    MOVE_FLAGS(stc_city_debuggrid_sites, CITY_DEBUGGRID_OFF);
    MOVE_FLAGS(stc_airride_debuggrid_gamedata_sites, AIRRIDE_SELECT_BASE + AIRRIDE_DEBUGGRID_OFF);
    MOVE_FLAGS(stc_city_debuggrid_gamedata_sites, CITY_SELECT_BASE + CITY_DEBUGGRID_OFF);

    CODEPATCH_REPLACEFUNC(AirRideSelect_LayoutIcons, AirRideLayoutIcons);
    CODEPATCH_REPLACEFUNC(AirRideSelect_GetIconPos, AirRideGetIconPos);
    CODEPATCH_REPLACEFUNC(CitySelect_LayoutMachineIcons, CityLayoutIcons);
    CODEPATCH_REPLACEFUNC(CitySelect_GetIconPos, CityGetIconPos);

    CODEPATCH_REPLACEFUNC(AirRide_PopulateSelectIcons, PopulateAirRideIcons);

    CODEPATCH_HOOKAPPLY(0x8015181c);  // Air Ride icon-GObj store
    CODEPATCH_HOOKAPPLY(0x8015c2dc);  // City Trial icon-GObj store
    CODEPATCH_HOOKAPPLY(0x8002f0b8);  // CT select list, layout and icons

    // CitySelect_CreateMachineIcons' Stadium and Free Run passes count the roster and
    // pack the vanilla grid, then reorder it assuming the special characters sit at
    // columns 0 and 9, which duplicates icons on a packed list. Both branch straight to
    // the tail above instead.
    CODEPATCH_REPLACEINSTRUCTION(0x8002e4d0, 0x48000be8);  // b 0x8002f0b8
    CODEPATCH_REPLACEINSTRUCTION(0x8002e5c0, 0x48000af8);  // b 0x8002f0b8

    // CitySelect_Cursor1InputThink splits cursor rows at num>=10 (`cmpwi r3, 9; ble`),
    // but the grid renderer keeps up to 10 icons on one drawn row and only wraps at
    // 11, so at num==10 the cursor splits 5+5 across a single row. Vanilla CT only
    // produces counts 15-20; a filtered roster can land on exactly 10.
    CODEPATCH_REPLACEINSTRUCTION(0x80031350, 0x2c03000a);  // cmpwi r3, 10

    OSReport("[SelectScreen] Select screens widened to %d icons\n", SELECT_ICON_MAX);
}

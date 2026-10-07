#include <string.h>

#include "game.h"
#include "os.h"
#include "obj.h"
#include "hud.h"
#include "inline.h"

#include "ap_star.h"
#include "ap_star_piece_hud.h"

// The tracker's own row, one icon height below the vanilla one, whose six anchors
// Hydra and Dragoon already claim.
#define AP_PIECE_HUD_ROW_DY -3.4f

static JOBJSet **icon_sets;
static const u8 *collected; // each player's set, one bit per APStarPieceKind

static struct
{
    GOBJ *icon[APSTARPIECE_NUM];
    u8 piece[APSTARPIECE_NUM]; // sphere each occupied slot is showing
    u8 count;
    u8 shown_mask;
} piece_hud[PLY_NUM];

// The viewport a player's HUD draws in, or -1 for a player with none, which every CPU is.
static int ViewForPly(int ply)
{
    Game3dData *g3d = Gm_Get3dData();
    for (int v = 0; v < 4; v++)
    {
        if (g3d->plyview_lookup[v] == ply)
            return v;
    }
    return -1;
}

// Where vanilla would put an icon on that anchor of the viewport's own row, shifted down
// onto the AP row. The row's instance carries HUD_CreatePlyElement's per-viewport offset.
static int AnchorPos(int view, int slot, Vec3 *out)
{
    GOBJ *row = Gm_Get3dData()->legendary_hud_gobj[view];
    if (row == NULL)
        return 0;
    JOBJ *anchor = GObj_GetJObjIndex(row, slot + 1);
    if (anchor == NULL)
        return 0;

    JObj_GetWorldPosition(anchor, NULL, out);
    out->Y += AP_PIECE_HUD_ROW_DY;
    return 1;
}

static void PlaceIcon(GOBJ *g, const Vec3 *pos)
{
    JOBJ *j = g->hsd_object;
    j->trans = *pos;
    JObj_SetMtxDirtySub(j);
}

static void ShowPieceIcon(int ply, int view, int piece)
{
    int slot = piece_hud[ply].count;
    Vec3 pos;
    if (icon_sets[piece] == NULL || icon_sets[piece]->jobj == NULL || !AnchorPos(view, slot, &pos))
        return;

    GOBJ *g = HUD_CreatePlyElement(view, icon_sets[piece]->jobj);
    if (g == NULL)
        return;
    GObj_SetPLink(g, GAMEPLINK_PAUSEHUD, 0);
    HUD_AddElementData(g, HUDKIND_LEGENDARYPIECE, ply, view);
    PlaceIcon(g, &pos);

    piece_hud[ply].icon[slot] = g;
    piece_hud[ply].piece[slot] = (u8)piece;
    piece_hud[ply].count = (u8)(slot + 1);
}

// The icons behind a removed one slide left, so the row keeps collection order.
static void RemovePieceIcon(int ply, int view, int slot)
{
    GObj_Destroy(piece_hud[ply].icon[slot]);

    for (int i = slot; i + 1 < piece_hud[ply].count; i++)
    {
        piece_hud[ply].icon[i] = piece_hud[ply].icon[i + 1];
        piece_hud[ply].piece[i] = piece_hud[ply].piece[i + 1];

        Vec3 pos;
        if (AnchorPos(view, i, &pos))
            PlaceIcon(piece_hud[ply].icon[i], &pos);
    }
    piece_hud[ply].count--;
    piece_hud[ply].icon[piece_hud[ply].count] = NULL;
}

static void UpdateRow(int ply, u8 mask)
{
    u8 shown = piece_hud[ply].shown_mask;
    if (mask == shown)
        return;
    piece_hud[ply].shown_mask = mask;

    // Vanilla builds its row only for a player with a viewport, so this does the same.
    int view = ViewForPly(ply);
    if (view < 0)
        return;

    // A dropped sphere clears its bit, so the diff runs both ways: an icon left
    // standing would be shown twice if that color were collected again.
    for (int s = 0; s < piece_hud[ply].count;)
    {
        if (mask & (1 << piece_hud[ply].piece[s]))
            s++;
        else
            RemovePieceIcon(ply, view, s);
    }
    for (int p = 0; p < APSTARPIECE_NUM; p++)
    {
        if (mask & ~shown & (1 << p))
            ShowPieceIcon(ply, view, p);
    }
}

static void HudThink(GOBJ *g)
{
    for (int ply = 0; ply < PLY_NUM; ply++)
        UpdateRow(ply, collected[ply]);
}

void ApStarPieceHud_OnSceneChange(void)
{
    memset(piece_hud, 0, sizeof(piece_hud));
    icon_sets = NULL;
}

void ApStarPieceHud_Create(const u8 *masks)
{
    HSD_Archive *icons = NULL;
    Gm_LoadGameFile(&icons, "ApPieceIcons");
    if (icons != NULL)
        icon_sets = Archive_GetPublicAddress(icons, "apPieceIcons_scene_models");

    // A tracker that cannot build fails the same way every round, so it says so once.
    static int unavailable_reported;
    if (icon_sets == NULL)
    {
        if (!unavailable_reported)
        {
            unavailable_reported = 1;
            OSReport("[ApStarPieceHud] ApPieceIcons.dat has no apPieceIcons_scene_models, "
                     "sphere tracker is off\n");
        }
        return;
    }

    collected = masks;
    // p_link 0 runs through the match pause and the hitstops, so a given sphere shows at once.
    GOBJ_EZCreator(0, GAMEPLINK_SYS, 0, 0, 0, HSD_OBJKIND_NONE, 0, HudThink, 0, 0, 0, 0);
}

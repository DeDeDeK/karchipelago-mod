#include <string.h>

#include "game.h"
#include "os.h"
#include "obj.h"
#include "hud.h"

#include "ap_star.h"
#include "ap_star_piece_hud.h"

// The tracker's own row, one icon height below the vanilla one, whose six anchors
// Hydra and Dragoon already claim.
#define AP_PIECE_HUD_ROW_DY -3.4f

static JOBJSet **icon_sets;
static Vec3 anchor_pos[APSTARPIECE_NUM];
static int anchors_valid;

static struct
{
    GOBJ *icon[APSTARPIECE_NUM];
    u8 piece[APSTARPIECE_NUM]; // sphere each occupied slot is showing
    u8 count;
    u8 shown_mask;
} piece_hud[PLY_NUM];

// The six anchors are children of the root with no rotation and unit scale, so a
// world position is the sum of two translations. Reading the descriptor rather than
// an instance keeps the AP row from needing a position-model element of its own.
static void ReadAnchors(void)
{
    Game3dData *g3d = Gm_Get3dData();
    if (g3d == NULL || g3d->legendary_hud_pos == NULL || g3d->legendary_hud_pos[0] == NULL)
        return;

    JOBJDesc *root = g3d->legendary_hud_pos[0]->jobj;
    if (root == NULL)
        return;

    JOBJDesc *anchor = root->child;
    for (int i = 0; i < APSTARPIECE_NUM; i++)
    {
        if (anchor == NULL)
            return;
        anchor_pos[i].X = root->position.X + anchor->position.X;
        anchor_pos[i].Y = root->position.Y + anchor->position.Y + AP_PIECE_HUD_ROW_DY;
        anchor_pos[i].Z = root->position.Z + anchor->position.Z;
        anchor = anchor->next;
    }
    anchors_valid = 1;
}

static int ViewForPly(int ply)
{
    Game3dData *g3d = Gm_Get3dData();
    if (g3d == NULL)
        return 0;
    for (int v = 0; v < 4; v++)
    {
        if (g3d->plyview_lookup[v] == (s8)ply)
            return v;
    }
    return 0;
}

static void ShowPieceIcon(int ply, int piece)
{
    int slot = piece_hud[ply].count;
    if (slot >= APSTARPIECE_NUM || piece_hud[ply].icon[slot] != NULL)
        return;
    if (icon_sets[piece] == NULL || icon_sets[piece]->jobj == NULL)
        return;

    GOBJ *g = HUD_CreateElement(ply, icon_sets[piece]->jobj);
    if (g == NULL)
        return;
    GObj_SetPLink(g, GAMEPLINK_PAUSEHUD, 0);
    HUD_AddElementData(g, HUDKIND_LEGENDARYPIECE, ply, ViewForPly(ply));

    JOBJ *j = g->hsd_object;
    j->trans = anchor_pos[slot];
    JObj_SetMtxDirtySub(j);

    piece_hud[ply].icon[slot] = g;
    piece_hud[ply].piece[slot] = (u8)piece;
    piece_hud[ply].count = (u8)(slot + 1);
}

// The icons behind a removed one slide left, so the row keeps collection order.
static void RemovePieceIcon(int ply, int slot)
{
    if (piece_hud[ply].icon[slot] != NULL)
        GObj_Destroy(piece_hud[ply].icon[slot]);

    for (int i = slot; i + 1 < piece_hud[ply].count; i++)
    {
        GOBJ *g = piece_hud[ply].icon[i + 1];
        piece_hud[ply].icon[i] = g;
        piece_hud[ply].piece[i] = piece_hud[ply].piece[i + 1];
        if (g == NULL)
            continue;

        JOBJ *j = g->hsd_object;
        j->trans = anchor_pos[i];
        JObj_SetMtxDirtySub(j);
    }
    piece_hud[ply].count--;
    piece_hud[ply].icon[piece_hud[ply].count] = NULL;
}

static void ClearPieceIcons(int ply)
{
    for (int i = 0; i < APSTARPIECE_NUM; i++)
    {
        if (piece_hud[ply].icon[i] != NULL)
            GObj_Destroy(piece_hud[ply].icon[i]);
        piece_hud[ply].icon[i] = NULL;
    }
    piece_hud[ply].count = 0;
    piece_hud[ply].shown_mask = 0;
}

void ApStarPieceHud_Update(int ply, u8 mask)
{
    if (mask == piece_hud[ply].shown_mask)
        return;

    if (mask == 0)
    {
        ClearPieceIcons(ply);
        return;
    }
    if (!anchors_valid || icon_sets == NULL)
    {
        piece_hud[ply].shown_mask = mask;
        return;
    }
    // A dropped sphere clears its bit, so the diff runs both ways: an icon left
    // standing would be shown twice if that color were collected again.
    for (int s = 0; s < piece_hud[ply].count;)
    {
        if (mask & (1 << piece_hud[ply].piece[s]))
            s++;
        else
            RemovePieceIcon(ply, s);
    }
    for (int p = 0; p < APSTARPIECE_NUM; p++)
    {
        u8 bit = (u8)(1 << p);
        if ((mask & bit) && !(piece_hud[ply].shown_mask & bit))
            ShowPieceIcon(ply, p);
    }
    piece_hud[ply].shown_mask = mask;
}

void ApStarPieceHud_OnSceneChange(void)
{
    memset(piece_hud, 0, sizeof(piece_hud));
    icon_sets = NULL;
    anchors_valid = 0;
}

void ApStarPieceHud_Load(void)
{
    HSD_Archive *icons = NULL;
    Gm_LoadGameFile(&icons, "ApPieceIcons");
    if (icons != NULL)
        icon_sets = Archive_GetPublicAddress(icons, "apPieceIcons_scene_models");
    ReadAnchors();

    // A tracker that cannot build fails the same way every round, so it says so once.
    static int unavailable_reported;
    if ((icon_sets == NULL || !anchors_valid) && !unavailable_reported)
    {
        unavailable_reported = 1;
        OSReport("[ApStarPieceHud] Sphere tracker unavailable\n");
    }
}

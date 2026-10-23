#include "os.h"
#include "game.h"
#include "weapon.h"
#include "hoshi/mod.h"

#include "main.h"
#include "gate_ap_star.h"
#include "ap_item_handler.h"
#include "ap_check_detect.h"
#include "gate_boxes.h"

#include "ap_star_api.h"
#include "ap_announce.h"

_Static_assert((int)AP_STAR_PIECE_NUM == (int)APSTARPIECE_NUM,
               "APStarPiece and APStarPieceKind must number the same spheres");

static const ApStarAPI *ap_star_api;
static int ap_star_kind = -1;

// ap_star fires this for CPU riders too; only a human assembly counts.
static void OnAssemble(int ply)
{
    if (Ply_GetPKind(ply) != PKIND_HMN)
        return;
    APCheckDetect_Observe(APCK_ASSEMBLE_AP_STAR);
}

// ap_star binds the star at its own OnSaveLoaded, which runs before this one.
void GateApStar_Resolve(void)
{
    ap_star_api = (const ApStarAPI *)Hoshi_ImportMod(
        (char *)AP_STAR_MOD_NAME, AP_STAR_API_MAJOR, AP_STAR_API_MINOR);
    if (ap_star_api != NULL)
    {
        ap_star_api->AddAssembleHandler(OnAssemble);
        ap_star_kind = ap_star_api->GetMachineKind();
    }
    GateApStar_PushMask();
}

void GateApStar_PushMask(void)
{
    if (ap_star_api == NULL)
        return;

    // Spheres ride the red carrier box, so a locked Red arms none of them.
    u8 mask = GateBoxes_IsUnlocked(BOXKIND_RED) ? ap_save->ap_star_piece_unlocked_mask : 0;
    ap_star_api->SetPieceMask(mask);
}

int GateApStar_UnlockPiece(int piece)
{
    if (piece < 0 || piece >= AP_STAR_PIECE_NUM)
        return 0;

    ap_save->ap_star_piece_unlocked_mask |= (u8)(1 << piece);

    // ap_star owns the sphere names and reports the gate change. Without it the bit is
    // kept and nothing is announced.
    if (ap_star_api == NULL)
    {
        OSReport("[GateApStar] Sphere %d unlocked with ap_star not built\n", piece);
        return 1;
    }

    APAnnounce_Grant("Unlocked Item: ", ap_star_api->GetPieceName(piece), tb_api->ItemColor, NULL);
    GateApStar_PushMask();
    return 1;
}

int GateApStar_MachineKind(void)
{
    return ap_star_kind;
}

int GateApStar_SpawnPiece(int piece, int ply)
{
    return ap_star_api ? ap_star_api->SpawnPiece(piece, ply) : 0;
}

int GateApStar_GivePiece(int piece)
{
    if (piece < 0 || piece >= AP_STAR_PIECE_NUM)
        return AP_ITEM_DROP;

    // No later round changes a build without ap_star.
    if (ap_star_api == NULL)
    {
        OSReport("[GateApStar] Sphere %d give dropped with ap_star not built\n", piece);
        return AP_ITEM_DROP;
    }

    int collected = 0;
    for (int i = 0; i < PLY_NUM; i++)
    {
        if (Ply_GetPKind(i) == PKIND_HMN)
            collected |= ap_star_api->CollectPiece(piece, i);
    }
    if (!collected)
        return AP_ITEM_RETRY;

    APAnnounce_Grant("Received: ", ap_star_api->GetPieceName(piece), tb_api->MachineColor, NULL);
    return AP_ITEM_APPLIED;
}

int GateApStar_GiveStar(void)
{
    if (ap_star_api == NULL)
    {
        OSReport("[GateApStar] Star give dropped with ap_star not built\n");
        return AP_ITEM_DROP;
    }

    for (int i = 0; i < PLY_NUM; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;
        if (ap_star_api->Assemble(i))
        {
            APAnnounce_Grant("Received: ", AP_STAR_MACHINE_NAME, tb_api->MachineColor, NULL);
            return AP_ITEM_APPLIED;
        }
    }
    return AP_ITEM_RETRY;
}

int GateApStar_AssembledThisRound(int ply)
{
    return ap_star_api ? ap_star_api->AssembledThisRound(ply) : 0;
}

int GateApStar_IsShotAttack(int credited_attack)
{
    return (credited_attack & WP_ATTACK_CAUSE_MASK) == AP_STAR_SHOT_ATTACK_CAUSE;
}

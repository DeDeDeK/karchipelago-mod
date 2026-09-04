#include "os.h"
#include "game.h"
#include "hoshi/mod.h"

#include "main.h"
#include "gate_ap_star.h"
#include "ap_item_handler.h"
#include "ap_check_detect.h"

// APStarPiece and ap_star's own APStarPieceKind number the six spheres the same
// way, which is what lets an index cross the boundary unchanged.
#include "ap_star_api.h"
#include "ap_announce.h"

static const ApStarAPI *ap_star_api;

static void OnAssemble(int ply)
{
    (void)ply;
    APCheckDetect_Observe(APCK_ASSEMBLE_AP_STAR);
}

void GateApStar_Resolve(void)
{
    if (ap_star_api == NULL)
    {
        ap_star_api = (const ApStarAPI *)Hoshi_ImportMod(
            (char *)AP_STAR_MOD_NAME, AP_STAR_API_MAJOR, AP_STAR_API_MINOR);
        if (ap_star_api == NULL)
            return;

        ap_star_api->AddAssembleHandler(OnAssemble);
    }

    GateApStar_PushMask();
}

void GateApStar_PushMask(void)
{
    // ap_star sorts before us, so the import already answers during our own OnBoot -
    // earlier than hoshi hands us a save. OnSaveLoaded pushes the real mask.
    if (ap_star_api == NULL || ap_save == NULL)
        return;
    ap_star_api->SetPieceMask(ap_save->ap_star_piece_unlocked_mask);
}

int GateApStar_UnlockPiece(int piece)
{
    if (piece < 0 || piece >= AP_STAR_PIECE_NUM)
        return 0;

    ap_save->ap_star_piece_unlocked_mask |= (u8)(1 << piece);

    // The gate change is reported by ap_star, which owns the sphere names too -
    // they are the CustomItemDesc.name of the archives it binds by. With the mod
    // absent there is no star to assemble and nothing to name, so the bit is kept
    // for a later build and nothing is announced.
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
    GateApStar_Resolve();
    return ap_star_api ? ap_star_api->GetMachineKind() : -1;
}

int GateApStar_SpawnPiece(int piece, int ply)
{
    GateApStar_Resolve();
    return ap_star_api ? ap_star_api->SpawnPiece(piece, ply) : 0;
}

int GateApStar_GivePiece(int piece)
{
    if (piece < 0 || piece >= AP_STAR_PIECE_NUM)
        return AP_ITEM_DROP;

    // Nothing a later round could change with the mod absent, so the item is
    // dropped rather than left retrying for the rest of the seed.
    GateApStar_Resolve();
    if (ap_star_api == NULL)
    {
        OSReport("[GateApStar] Sphere %d give dropped with ap_star not built\n", piece);
        return AP_ITEM_DROP;
    }

    int collected = 0;
    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) == PKIND_HMN)
            collected |= ap_star_api->CollectPiece(piece, i);
    }
    if (!collected)
        return AP_ITEM_RETRY;

    // ap_star owns the sphere names, so the announcement takes one from it rather
    // than a copy that can drift out of step with the archives.
    APAnnounce_Grant("Received: ", ap_star_api->GetPieceName(piece), tb_api->MachineColor, NULL);
    return AP_ITEM_APPLIED;
}

int GateApStar_GiveStar(void)
{
    GateApStar_Resolve();
    if (ap_star_api == NULL)
    {
        OSReport("[GateApStar] Star give dropped with ap_star not built\n");
        return AP_ITEM_DROP;
    }

    for (int i = 0; i < 5; i++)
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

int GateApStar_WasAssembled(void)
{
    return ap_star_api ? ap_star_api->WasAssembled() : 0;
}

int GateApStar_AssembledThisRound(int ply)
{
    return ap_star_api ? ap_star_api->AssembledThisRound(ply) : 0;
}

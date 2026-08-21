#include "os.h"
#include "hoshi/mod.h"

#include "main.h"
#include "gate_ap_star.h"
#include "ap_check_detect.h"

// APStarPiece and ap_star's own APStarPieceKind number the six spheres the same
// way, which is what lets an index cross the boundary unchanged.
#include "ap_star_api.h"

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

    tb_api->EnqueueColoredNoun("Unlocked Item: ", ap_star_api->GetPieceName(piece),
                               tb_api->ItemColor, NULL);
    GateApStar_PushMask();
    return 1;
}

int GateApStar_MachineKind(void)
{
    GateApStar_Resolve();
    return ap_star_api ? ap_star_api->GetMachineKind() : -1;
}

int GateApStar_DebugSpawnPiece(int piece, int ply)
{
    GateApStar_Resolve();
    return ap_star_api ? ap_star_api->DebugSpawnPiece(piece, ply) : 0;
}

int GateApStar_WasAssembled(void)
{
    return ap_star_api ? ap_star_api->WasAssembled() : 0;
}

int GateApStar_AssembledThisRound(int ply)
{
    return ap_star_api ? ap_star_api->AssembledThisRound(ply) : 0;
}

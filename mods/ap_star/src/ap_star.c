#include "os.h"
#include "hoshi/mod.h"

#include "ap_star.h"
#include "ap_star_palette.h"
#include "ap_star_pieces.h"
#include "ap_star_ring.h"

const CustomMachinesAPI *cm_api;

const GXColor ap_star_piece_colors[APSTARPIECE_NUM] = {
    [APSTARPIECE_ROSE]   = { 0xC9, 0x76, 0x82, 0xFF },
    [APSTARPIECE_GREEN]  = { 0x75, 0xC2, 0x75, 0xFF },
    [APSTARPIECE_VIOLET] = { 0xCA, 0x94, 0xC2, 0xFF },
    [APSTARPIECE_TAN]    = { 0xD9, 0xA0, 0x7D, 0xFF },
    [APSTARPIECE_BLUE]   = { 0x76, 0x7E, 0xBD, 0xFF },
    [APSTARPIECE_YELLOW] = { 0xEE, 0xE3, 0x91, 0xFF },
};

static int stc_kind = -1;

int ApStar_MachineKind(void)
{
    return stc_kind;
}

// custom_machines boots after this mod, and an import only finds a mod that has already
// booted, so the import waits for OnSaveLoaded, past every mod's OnBoot.
void ApStar_OnSaveLoaded(void)
{
    cm_api = (const CustomMachinesAPI *)Hoshi_ImportMod(
        (char *)CUSTOM_MACHINES_MOD_NAME, CUSTOM_MACHINES_API_MAJOR, CUSTOM_MACHINES_API_MINOR);
    stc_kind = cm_api ? cm_api->FindKindByName(AP_STAR_MACHINE_NAME) : -1;
    if (stc_kind < 0)
    {
        OSReport("[ApStar] %s not registered, shot and platform cycle are off\n",
                 AP_STAR_MACHINE_NAME);
        return;
    }
    ApStarRing_Bind(stc_kind);
    ApStarPalette_Bind(stc_kind);
}

static const ApStarAPI api = {
    .GetMachineKind     = ApStar_MachineKind,
    .GetPieceName       = ApStarPieces_GetName,
    .SetPieceMask       = ApStarPieces_SetGate,
    .AddAssembleHandler = ApStarPieces_AddAssembleHandler,
    .AssembledThisRound = ApStarPieces_AssembledThisRound,
    .SpawnPiece         = ApStarPieces_SpawnPiece,
    .CollectPiece       = ApStarPieces_CollectPiece,
    .Assemble           = ApStarPieces_Assemble,
};

void ApStar_ExportApi(void)
{
    Hoshi_ExportMod((void *)&api);
}

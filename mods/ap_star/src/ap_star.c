#include "os.h"
#include "inline.h"
#include "hoshi/mod.h"

#include "ap_star.h"
#include "ap_star_pieces.h"

const CustomMachinesAPI *cm_api;

u32 ap_star_piece_gate = AP_STAR_PIECE_ALL;

#define AP_STAR_HANDLER_MAX 4

static ApStarAssembleFn assemble_handlers[AP_STAR_HANDLER_MAX];

void ApStar_ResolveCustomMachines(void)
{
    if (cm_api)
        return;

    cm_api = (const CustomMachinesAPI *)Hoshi_ImportMod(
        (char *)CUSTOM_MACHINES_MOD_NAME, CUSTOM_MACHINES_API_MAJOR, CUSTOM_MACHINES_API_MINOR);
}

int ApStar_MachineKind(void)
{
    ApStar_ResolveCustomMachines();
    return cm_api ? cm_api->FindKindByName(AP_STAR_MACHINE_NAME) : -1;
}

int ApStar_ClassIndex(int *is_bike)
{
    int kind = ApStar_MachineKind();
    if (kind < 0)
        return -1;
    return CustomMachines_ClassIndexOf(cm_api, (MachineKind)kind, is_bike);
}

int ApStar_StartAssembly(int ply)
{
    int kind = ApStar_MachineKind();
    if (kind < 0 || cm_api == NULL)
        return 0;
    return cm_api->StartAssembly(kind, ply);
}

void ApStar_FireAssemble(int ply)
{
    for (int i = 0; i < AP_STAR_HANDLER_MAX; i++)
    {
        if (assemble_handlers[i] != NULL)
            assemble_handlers[i](ply);
    }
}

static void ApiSetPieceEnabled(int piece, int enabled)
{
    if (piece < 0 || piece >= APSTARPIECE_NUM)
        return;

    u32 bit = 1u << piece;
    if (((ap_star_piece_gate & bit) != 0) == (enabled != 0))
        return;

    if (enabled)
    {
        ap_star_piece_gate |= bit;
        OSReport("[ApStar] %s enabled (mask = %s)\n", ApStarPieces_GetName(piece),
                 MaskBits(ap_star_piece_gate, APSTARPIECE_NUM));
    }
    else
    {
        ap_star_piece_gate &= ~bit;
    }
}

static int ApiIsPieceEnabled(int piece)
{
    if (piece < 0 || piece >= APSTARPIECE_NUM)
        return 0;
    return (ap_star_piece_gate >> piece) & 1;
}

// Per-bit so a mask write reports exactly like a single gate change.
static void ApiSetPieceMask(u32 mask)
{
    for (int i = 0; i < APSTARPIECE_NUM; i++)
        ApiSetPieceEnabled(i, (mask >> i) & 1);
}

static u32 ApiGetPieceMask(void)
{
    return ap_star_piece_gate;
}

static void ApiAddAssembleHandler(ApStarAssembleFn fn)
{
    if (fn == NULL)
        return;
    for (int i = 0; i < AP_STAR_HANDLER_MAX; i++)
    {
        if (assemble_handlers[i] == fn)
            return;
    }
    for (int i = 0; i < AP_STAR_HANDLER_MAX; i++)
    {
        if (assemble_handlers[i] == NULL)
        {
            assemble_handlers[i] = fn;
            return;
        }
    }
    OSReport("[ApStar] Assemble handler list full\n");
}

static void ApiRemoveAssembleHandler(ApStarAssembleFn fn)
{
    for (int i = 0; i < AP_STAR_HANDLER_MAX; i++)
    {
        if (assemble_handlers[i] == fn)
            assemble_handlers[i] = NULL;
    }
}

static const ApStarAPI api = {
    .GetMachineKind        = ApStar_MachineKind,
    .GetPieceName          = ApStarPieces_GetName,
    .SetPieceEnabled       = ApiSetPieceEnabled,
    .IsPieceEnabled        = ApiIsPieceEnabled,
    .SetPieceMask          = ApiSetPieceMask,
    .GetPieceMask          = ApiGetPieceMask,
    .AddAssembleHandler    = ApiAddAssembleHandler,
    .RemoveAssembleHandler = ApiRemoveAssembleHandler,
    .WasAssembled          = ApStarPieces_WasAssembled,
    .AssembledThisRound    = ApStarPieces_AssembledThisRound,
    .DebugSpawnPiece       = ApStarPieces_DebugSpawn,
};

void ApStar_ExportApi(void)
{
    Hoshi_ExportMod((void *)&api);
}

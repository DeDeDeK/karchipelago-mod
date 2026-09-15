#include "os.h"
#include "inline.h"
#include "hoshi/mod.h"

#include "ap_star.h"
#include "ap_star_pieces.h"

const CustomMachinesAPI *cm_api;

u32 ap_star_piece_gate = AP_STAR_PIECE_ALL;

const u32 ap_star_piece_colors[APSTARPIECE_NUM] = {
    [APSTARPIECE_ROSE]   = 0xC97682,
    [APSTARPIECE_GREEN]  = 0x75C275,
    [APSTARPIECE_VIOLET] = 0xCA94C2,
    [APSTARPIECE_TAN]    = 0xD9A07D,
    [APSTARPIECE_BLUE]   = 0x767EBD,
    [APSTARPIECE_YELLOW] = 0xEEE391,
};

#define AP_STAR_HANDLER_MAX 4

static ApStarAssembleFn assemble_handlers[AP_STAR_HANDLER_MAX];

// Retried until it resolves: mods boot in FST order and an import only finds a mod that
// has already booted, so a lookup from any mod's OnBoot - ours or a consumer's - runs
// before custom_machines exports.
static void ResolveCustomMachines(void)
{
    if (cm_api)
        return;

    cm_api = (const CustomMachinesAPI *)Hoshi_ImportMod(
        (char *)CUSTOM_MACHINES_MOD_NAME, CUSTOM_MACHINES_API_MAJOR, CUSTOM_MACHINES_API_MINOR);
}

int ApStar_MachineKind(void)
{
    ResolveCustomMachines();
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
    if (kind < 0)
        return 0;
    return cm_api->StartAssembly(kind, ply);
}

int ApStar_Mount(int ply)
{
    int kind = ApStar_MachineKind();
    if (kind < 0)
        return 0;
    return cm_api->MountMachine(kind, ply);
}

void ApStar_FireAssemble(int ply)
{
    for (int i = 0; i < AP_STAR_HANDLER_MAX; i++)
    {
        if (assemble_handlers[i] != NULL)
            assemble_handlers[i](ply);
    }
}

static void SetPieceEnabled(int piece, int enabled)
{
    u32 bit = 1u << piece;
    if (((ap_star_piece_gate & bit) != 0) == (enabled != 0))
        return;

    if (enabled)
        ap_star_piece_gate |= bit;
    else
        ap_star_piece_gate &= ~bit;

    OSReport("[ApStar] %s %s (mask = %s)\n", ApStarPieces_GetName(piece),
             enabled ? "enabled" : "disabled",
             MaskBits(ap_star_piece_gate, APSTARPIECE_NUM));
}

// Per-bit so a mask write reports exactly like a single gate change.
static void ApiSetPieceMask(u32 mask)
{
    for (int i = 0; i < APSTARPIECE_NUM; i++)
        SetPieceEnabled(i, (mask >> i) & 1);
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

static const ApStarAPI api = {
    .GetMachineKind     = ApStar_MachineKind,
    .GetPieceName       = ApStarPieces_GetName,
    .SetPieceMask       = ApiSetPieceMask,
    .AddAssembleHandler = ApiAddAssembleHandler,
    .AssembledThisRound = ApStarPieces_AssembledThisRound,
    .SpawnPiece         = ApStarPieces_SpawnPiece,
    .CollectPiece       = ApStarPieces_CollectPiece,
    .Assemble           = ApStarPieces_Assemble,
};

void ApStar_ExportApi(void)
{
    Hoshi_ExportMod((void *)&api);
}

#ifndef AP_STAR_API_H
#define AP_STAR_API_H

#include "datatypes.h"

// The Archipelago Star: a drop-in custom machine plus the City Trial content that
// awards it. The machine archive is machines/VcStarAp.dat, discovered and registered
// by custom_machines like any other drop-in; this mod owns the behavior hung off it -
// the charge-release shot and the six-sphere collection - and starts the registry's
// assembly cutscene.
//
// Built without a consumer every sphere is in play and the star assembles the way
// Hydra and Dragoon do. A consumer narrows that through SetPieceEnabled; how a sphere
// is earned is its idea, not this mod's.

#define AP_STAR_MOD_NAME  "ap_star"
#define AP_STAR_API_MAJOR 2
#define AP_STAR_API_MINOR 0

// CustomMachineDesc.name of the machine archive. The registry is generic, so
// this string is the only thing tying machines/VcStarAp.dat to this code.
#define AP_STAR_MACHINE_NAME "Archipelago Star"

// The six spheres, in the logo's ring order - rose at twelve o'clock, then
// clockwise. Also the index every piece call below addresses a sphere by.
typedef enum APStarPieceKind
{
    APSTARPIECE_ROSE,
    APSTARPIECE_GREEN,
    APSTARPIECE_VIOLET,
    APSTARPIECE_TAN,
    APSTARPIECE_BLUE,
    APSTARPIECE_YELLOW,
    APSTARPIECE_NUM,
} APStarPieceKind;

// All six in play, which is what the gate holds until a consumer narrows it.
#define AP_STAR_PIECE_ALL ((1u << APSTARPIECE_NUM) - 1)

// Fired once the frame a player completes a set. `ply` is the 0..4 slot.
typedef void (*ApStarAssembleFn)(int ply);

typedef struct ApStarAPI
{
    // MachineKind custom_machines registered the star as, or -1 while nothing
    // has. Resolves lazily, so it is safe from any scene but answers -1 during
    // a mod's own OnBoot.
    int (*GetMachineKind)(void);

    // Display name of one sphere. This is the CustomItemDesc.name its archive
    // carries, which is also what binds that archive to its slot, so a consumer
    // naming a sphere to the player takes it from here rather than keeping a
    // copy that can drift out of step with the archives.
    const char *(*GetPieceName)(int piece);

    // The gate on each sphere, which decides whether it is in the item registry
    // for the round about to load. Gates default open. A sphere held closed has
    // no ItemKind at all, so no path can spawn it, and the set cannot be
    // completed until all six are open. Reads back through IsPieceEnabled.
    void (*SetPieceEnabled)(int piece, int enabled);
    int (*IsPieceEnabled)(int piece);

    // Whole-gate form, one bit per APStarPieceKind. AP_STAR_PIECE_ALL is the
    // default.
    void (*SetPieceMask)(u32 mask);
    u32 (*GetPieceMask)(void);

    // Handlers run on a completed set. Every registered handler runs; adding one
    // already present is a no-op.
    void (*AddAssembleHandler)(ApStarAssembleFn fn);
    void (*RemoveAssembleHandler)(ApStarAssembleFn fn);

    // 1 once any player has assembled the star this boot. Sticky, for predicates
    // polled long after the round it happened in.
    int (*WasAssembled)(void);
    // 1 if this player assembled the star in the round currently loaded. Cleared
    // on every 3D scene load.
    int (*AssembledThisRound)(int ply);

    // Drop one sphere on the ground in front of a player's machine, bypassing
    // the delivery schedule. Returns 0 if the sphere's gate was closed when this
    // scene loaded, since it has no ItemKind then.
    int (*DebugSpawnPiece)(int piece, int ply);
} ApStarAPI;

#endif // AP_STAR_API_H

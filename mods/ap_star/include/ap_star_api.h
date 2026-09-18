#ifndef AP_STAR_API_H
#define AP_STAR_API_H

#include "datatypes.h"

// The Archipelago Star: a drop-in custom machine plus the City Trial content that
// awards it. custom_machines registers the machine and owns the assembly cutscene;
// this mod owns the charge-release shot and the six-sphere collection.

#define AP_STAR_MOD_NAME  "ap_star"
#define AP_STAR_API_MAJOR 4
#define AP_STAR_API_MINOR 0

// CustomMachineDesc.name of the machine archive. The registry is generic, so
// this string is the only thing tying machines/VcStarAp.dat to this code.
#define AP_STAR_MACHINE_NAME "Archipelago Star"

// The six spheres, in the logo's ring order - rose at twelve o'clock, then clockwise.
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

// Fired once the frame a player completes a set. `ply` is the 0..4 slot.
typedef void (*ApStarAssembleFn)(int ply);

typedef struct ApStarAPI
{
    // MachineKind the star registered as, or -1. Resolves lazily, so it answers
    // -1 during a mod's own OnBoot.
    int (*GetMachineKind)(void);

    // Display name of one sphere, which is its archive's CustomItemDesc.name.
    const char *(*GetPieceName)(int piece);

    // The gate on the six spheres, one bit per APStarPieceKind, read at 3D load
    // start. A closed sphere gets no ItemKind, so no path can spawn it and the set
    // cannot complete. All six are open until a consumer narrows it.
    void (*SetPieceMask)(u32 mask);

    // Run as a player completes a set. Adding a handler already present is a no-op.
    void (*AddAssembleHandler)(ApStarAssembleFn fn);

    // 1 if this player assembled the star in the round currently loaded.
    int (*AssembledThisRound)(int ply);

    // Drop one sphere in front of a player's machine, bypassing the delivery
    // schedule. 0 if the sphere's gate was closed when this scene loaded.
    int (*SpawnPiece)(int piece, int ply);

    // Add one sphere to a player's set with no pickup, completing the set if it is
    // the sixth. Independent of the gate, which is how a consumer awards one
    // directly. City Trial only, and 0 with the player not riding.
    int (*CollectPiece)(int piece, int ply);

    // Put a player straight through the assembly without the set: the cutscene (or
    // the mount and completion sounds when it cannot run), the assembled flags and
    // the handlers. Their collected set is cleared. City Trial only.
    int (*Assemble)(int ply);
} ApStarAPI;

#endif // AP_STAR_API_H

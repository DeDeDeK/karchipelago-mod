#ifndef AP_STAR_H
#define AP_STAR_H

#include "structs.h"

// NULL when custom_machines is not built, in which case the star was never
// registered and every consumer of the kind falls back to doing nothing.
#include "custom_machines_api.h"
extern const CustomMachinesAPI *cm_api;

#include "ap_star_api.h"

// Import custom_machines if it has not resolved yet. Idempotent, and safe from
// any scene - mods boot alphabetically, so it returns NULL during our own OnBoot.
void ApStar_ResolveCustomMachines(void);

// MachineKind of the Archipelago Star, or -1 while nothing has registered it.
int ApStar_MachineKind(void);

// Class slot the star occupies, or -1. `is_bike` is written when it resolves.
int ApStar_ClassIndex(int *is_bike);

// Put a player through the assembly cutscene on the star. custom_machines owns
// the cutscene, driven by the two archives the machine's descriptor names.
// Returns 0 if it could not run - no machine, one already up, or a rider the
// vanilla assembly state does not cover - in which case the caller still owes
// the mount and the completion sounds.
int ApStar_StartAssembly(int ply);

// The sphere gate, one bit per APStarPieceKind. AP_STAR_PIECE_ALL until a
// consumer narrows it.
extern u32 ap_star_piece_gate;

// Notify the assemble handlers. The caller owns the assembly state; this only
// dispatches.
void ApStar_FireAssemble(int ply);

// Export the API table. Runs at OnBoot, after the subsystems have initialized.
void ApStar_ExportApi(void);

// Menu toggle state, bound to this mod's settings page via OptionDesc.
typedef struct ApStarSettings
{
    int shot_enabled; // the star fires a sphere on every full-charge release
} ApStarSettings;

extern ApStarSettings ap_star_settings;

#endif // AP_STAR_H

#ifndef AP_STAR_H
#define AP_STAR_H

#include "structs.h"

// NULL when custom_machines is not built.
#include "custom_machines_api.h"
extern const CustomMachinesAPI *cm_api;

#include "ap_star_api.h"

// The six sphere colors as 0x00RRGGBB. The pods wear them in the same order around the
// ring, from the one on +Z clockwise seen from above.
extern const u32 ap_star_piece_colors[APSTARPIECE_NUM];

// MachineKind of the Archipelago Star, or -1 while nothing has registered it.
int ApStar_MachineKind(void);

// Class slot the star occupies, or -1. `is_bike` is written when it resolves.
int ApStar_ClassIndex(int *is_bike);

// Put a player through the assembly cutscene on the star, which custom_machines owns.
// Returns 0 if it could not run.
int ApStar_StartAssembly(int ply);

// Put a player on the star with no cutscene, at the start of the next frame. Returns 0
// with the star unregistered or the player not riding.
int ApStar_Mount(int ply);

// All six in play, which is what the gate holds until a consumer narrows it.
#define AP_STAR_PIECE_ALL ((1u << APSTARPIECE_NUM) - 1)

// The sphere gate, one bit per APStarPieceKind.
extern u32 ap_star_piece_gate;

static inline int ApStar_IsPieceEnabled(int piece)
{
    return (ap_star_piece_gate & (1u << piece)) != 0;
}

void ApStar_FireAssemble(int ply);

// Runs at OnBoot, after the subsystems have initialized.
void ApStar_ExportApi(void);

typedef struct ApStarSettings
{
    int shot_enabled; // the star fires a sphere on every full-charge release
} ApStarSettings;

extern ApStarSettings ap_star_settings;

#endif // AP_STAR_H

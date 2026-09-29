#ifndef AP_STAR_H
#define AP_STAR_H

#include "structs.h"
#include "gx.h"

// NULL when custom_machines is not built.
#include "custom_machines_api.h"
extern const CustomMachinesAPI *cm_api;

#include "ap_star_api.h"

// The six sphere colors. The pods wear them in the same order around the ring, from
// the one on +Z toward +X.
extern const GXColor ap_star_piece_colors[APSTARPIECE_NUM];

// MachineKind of the Archipelago Star, or -1 while nothing has registered it.
int ApStar_MachineKind(void);

// Settles the machine binding on the first call, past every mod's OnBoot, and claims
// the star's handler slots.
void ApStar_OnSceneChange(void);

// Put a player through the assembly cutscene on the star, which custom_machines owns.
// Returns 0 if it could not run.
int ApStar_StartAssembly(int ply);

// Put a player on the star with no cutscene, at the start of the next frame. Returns 0
// with the star unregistered or the player slot empty.
int ApStar_Mount(int ply);

void ApStar_ExportApi(void);

typedef struct ApStarSettings
{
    int shot_enabled; // the star fires a sphere on every full-charge release
} ApStarSettings;

extern ApStarSettings ap_star_settings;

#endif // AP_STAR_H

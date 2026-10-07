#ifndef AP_STAR_H
#define AP_STAR_H

#include "structs.h"
#include "gx.h"

// NULL when custom_machines is not built.
#include "custom_machines_api.h"
extern const CustomMachinesAPI *cm_api;

#include "ap_star_api.h"

#define AP_STAR_PIECE_ALL ((1u << APSTARPIECE_NUM) - 1)

// The six sphere colors. The pods wear them in the same order around the ring, from
// the one on +Z toward +X.
extern const GXColor ap_star_piece_colors[APSTARPIECE_NUM];

// MachineKind of the Archipelago Star, or -1 when nothing registered it or before
// OnSaveLoaded.
int ApStar_MachineKind(void);

// Imports custom_machines, resolves the star's kind and claims its handler slots.
void ApStar_OnSaveLoaded(void);

void ApStar_ExportApi(void);

typedef struct ApStarSettings
{
    int shot_enabled; // the star fires a sphere on a full-charge release
} ApStarSettings;

extern ApStarSettings ap_star_settings;

#endif // AP_STAR_H

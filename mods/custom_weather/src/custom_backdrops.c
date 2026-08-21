// Random City Trial skybox selection: swaps the stage's backdrop JObj for one
// carved out of another stage's archive.

#include "os.h"
#include "game.h"
#include "stage.h"
#include "obj.h"
#include "hsd.h"
#include "code_patch/code_patch.h"
#include "hoshi/settings.h"

#include "custom_weather.h"

// Index 0 ("Vanilla") is the no-override path: the stock loader keeps CT's own
// backdrop, so no donor archive is loaded.
typedef struct BackdropDef
{
    const char *display_name;
    const char *filename;   // NULL for vanilla
    const char *symbol;     // NULL for vanilla
} BackdropDef;

#define BACKDROP_VANILLA_INDEX 0

static const BackdropDef backdrop_defs[] = {
    { "Vanilla",     NULL,                       NULL                 },
    { "Check 2",     "BackdropCheck2.dat",       "backdropCheck2"     },
    { "Colosseum 1", "BackdropColosseum1.dat",   "backdropColosseum1" },
    { "Colosseum 3", "BackdropColosseum3.dat",   "backdropColosseum3" },
    { "Colosseum 5", "BackdropColosseum5.dat",   "backdropColosseum5" },
    { "Dedede 1",    "BackdropDedede1.dat",      "backdropDedede1"    },
    { "Desert 1",    "BackdropDesert1.dat",      "backdropDesert1"    },
    { "Heat 2",      "BackdropHeat2.dat",        "backdropHeat2"      },
    { "Ice 1",       "BackdropIce1.dat",         "backdropIce1"       },
    { "Jump 1",      "BackdropJump1.dat",        "backdropJump1"      },
    { "Jump 2",      "BackdropJump2.dat",        "backdropJump2"      },
    { "Jump 3",      "BackdropJump3.dat",        "backdropJump3"      },
    { "Machine 2",   "BackdropMachine2.dat",     "backdropMachine2"   },
    { "Pasture 1",   "BackdropPasture1.dat",     "backdropPasture1"   },
    { "Plants 1",    "BackdropPlants1.dat",      "backdropPlants1"    },
    { "Sky 2",       "BackdropSky2.dat",         "backdropSky2"       },
    { "Space 2",     "BackdropSpace2.dat",       "backdropSpace2"     },
    { "Valley 2",    "BackdropValley2.dat",      "backdropValley2"    },
    { "Zeroyon 1",   "BackdropZeroyon1.dat",     "backdropZeroyon1"   },
    { "Zeroyon 3",   "BackdropZeroyon3.dat",     "backdropZeroyon3"   },
    { "Zeroyon 4",   "BackdropZeroyon4.dat",     "backdropZeroyon4"   },
    { "Zeroyon 5",   "BackdropZeroyon5.dat",     "backdropZeroyon5"   },
};
#define BACKDROP_NUM (sizeof(backdrop_defs) / sizeof(backdrop_defs[0]))

// Per-entry enable toggle, persisted by hoshi menu save (keyed by option name hash).
static int backdrop_enabled[BACKDROP_NUM] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1,
};

static char *toggle_names[] = {"Disabled", "Enabled"};

// Multiplies the root-joint scale the loader stamps into every backdrop, pushing
// the whole sky dome out or pulling it in.
static const float backdrop_distance_factors[] = {1.0f, 1.25f, 1.5f, 1.75f, 2.0f};
static char *backdrop_distance_names[] = {"100%", "125%", "150%", "175%", "200%"};
#define BACKDROP_DISTANCE_NUM \
    ((int)(sizeof(backdrop_distance_factors) / sizeof(backdrop_distance_factors[0])))
static int backdrop_distance_index = 1; // default 125%

// The donor archive is never cached across rounds: Archive_LoadFile allocates into
// the per-scene heap, which is zeroed on 3D scene exit. Teardown reclaims it, so
// there is no Archive_Free.

static int PickEnabled(void)
{
    int enabled_count = 0;
    for (int i = 0; i < (int)BACKDROP_NUM; i++)
    {
        if (backdrop_enabled[i])
            enabled_count++;
    }
    if (enabled_count == 0)
        return BACKDROP_VANILLA_INDEX;

    int pick = HSD_Randi(enabled_count);
    for (int i = 0; i < (int)BACKDROP_NUM; i++)
    {
        if (backdrop_enabled[i])
        {
            if (pick == 0)
                return i;
            pick--;
        }
    }
    return BACKDROP_VANILLA_INDEX;
}

static void CustomBackdrop_Override(GrObj *grobj)
{
    if (grobj == NULL || grobj->gr_kind != GR_CITY1)
        return;

    int picked = PickEnabled();

    if (picked == BACKDROP_VANILLA_INDEX)
    {
        OSReport("[CustomBackdrop] Selected Vanilla\n");
        return;
    }

    const BackdropDef *def = &backdrop_defs[picked];

    HSD_Archive *donor = Archive_LoadFile((char *)def->filename);
    if (donor == NULL)
    {
        OSReport("[CustomBackdrop] Archive_LoadFile(%s) failed\n", def->filename);
        return;
    }

    void **donor_ms = Archive_GetPublicAddress(donor, (char *)def->symbol);
    if (donor_ms == NULL || donor_ms[1] == NULL)
    {
        OSReport("[CustomBackdrop] %s lookup failed in %s\n", def->symbol, def->filename);
        return;
    }

    ModelSection *ct_ms = grobj->gr_data->model_section;
    if (ct_ms == NULL)
        return;

    ct_ms->backdrop = (JOBJDesc **)donor_ms[1];
    OSReport("[CustomBackdrop] Selected %s\n", def->display_name);
}

// Inside 3D_CreateStageModel, after r30 = grobj and just before it reads
// grdata->model_section, so the ms.backdrop override lands on the next
// instruction. The macro replays the clobbered `lwz r3, 8(r30)`.
CODEPATCH_HOOKCREATE(0x800dcc18,
    "mr 3, 30\n\t",
    CustomBackdrop_Override,
    "",
    0x800dcc1c);

// 3D_CreateStageModel stamps grGetStageScale() (City's 0.70) into the backdrop
// root joint's JOBJ+0x2C/30/34; scaling that uniformly moves the whole sky dome
// in or out without re-carving geometry.
static void CustomBackdrop_ScaleDistance(GrObj *grobj, JOBJ *backdrop)
{
    if (grobj == NULL || backdrop == NULL || grobj->gr_kind != GR_CITY1)
        return;

    float f = backdrop_distance_factors[backdrop_distance_index];
    if (f == 1.0f)
        return;

    backdrop->scale.X *= f;
    backdrop->scale.Y *= f;
    backdrop->scale.Z *= f;
}

// Inside 3D_CreateStageModel, immediately after the backdrop branch stamps the
// scale into the root joint; r30 = grobj, r29 = backdrop JObj (both non-volatile).
// The macro replays the clobbered `lwz r0, 20(r29)`, so the classical-scaling flag
// check that follows sees the rescaled joint, still before the matrix build.
CODEPATCH_HOOKCREATE(0x800dce84,
    "mr 3, 30\n\t"
    "mr 4, 29\n\t",
    CustomBackdrop_ScaleDistance,
    "",
    0x800dce88);

void CustomBackdrop_OnBoot(void)
{
    CODEPATCH_HOOKAPPLY(0x800dcc18);
    CODEPATCH_HOOKAPPLY(0x800dce84);
    OSReport("[CustomBackdrop] Hooks installed (%d backdrops in pool)\n",
             (int)BACKDROP_NUM);
}

static int EnableAllBackdrops(OptionDesc *self)
{
    (void)self;
    for (int i = 0; i < (int)BACKDROP_NUM; i++)
        backdrop_enabled[i] = 1;
    return 1;
}

static int DisableAllBackdrops(OptionDesc *self)
{
    (void)self;
    for (int i = 0; i < (int)BACKDROP_NUM; i++)
        backdrop_enabled[i] = 0;
    return 1;
}

#define BACKDROP_TOGGLE(idx, label) \
    &(OptionDesc){ \
        .name = label, \
        .kind = OPTKIND_VALUE, \
        .val = &backdrop_enabled[idx], \
        .value_num = 2, \
        .value_names = toggle_names, \
    }

MenuDesc backdrop_menu = {
    .option_num = BACKDROP_NUM + 3,
    .options = {
        &(OptionDesc){
            .name = "Backdrop Distance",
            .description = "How far the City Trial sky backdrop renders (scales all backdrops, including Vanilla)",
            .kind = OPTKIND_VALUE,
            .val = &backdrop_distance_index,
            .value_num = BACKDROP_DISTANCE_NUM,
            .value_names = backdrop_distance_names,
        },
        &(OptionDesc){
            .name = "Enable All",
            .description = "Enable all backdrops",
            .kind = OPTKIND_ACTION,
            .on_action = EnableAllBackdrops,
        },
        &(OptionDesc){
            .name = "Disable All",
            .description = "Disable all backdrops",
            .kind = OPTKIND_ACTION,
            .on_action = DisableAllBackdrops,
        },
        BACKDROP_TOGGLE(0,  "Vanilla"),
        BACKDROP_TOGGLE(1,  "Check 2"),
        BACKDROP_TOGGLE(2,  "Colosseum 1"),
        BACKDROP_TOGGLE(3,  "Colosseum 3"),
        BACKDROP_TOGGLE(4,  "Colosseum 5"),
        BACKDROP_TOGGLE(5,  "Dedede 1"),
        BACKDROP_TOGGLE(6,  "Desert 1"),
        BACKDROP_TOGGLE(7,  "Heat 2"),
        BACKDROP_TOGGLE(8,  "Ice 1"),
        BACKDROP_TOGGLE(9,  "Jump 1"),
        BACKDROP_TOGGLE(10, "Jump 2"),
        BACKDROP_TOGGLE(11, "Jump 3"),
        BACKDROP_TOGGLE(12, "Machine 2"),
        BACKDROP_TOGGLE(13, "Pasture 1"),
        BACKDROP_TOGGLE(14, "Plants 1"),
        BACKDROP_TOGGLE(15, "Sky 2"),
        BACKDROP_TOGGLE(16, "Space 2"),
        BACKDROP_TOGGLE(17, "Valley 2"),
        BACKDROP_TOGGLE(18, "Zeroyon 1"),
        BACKDROP_TOGGLE(19, "Zeroyon 3"),
        BACKDROP_TOGGLE(20, "Zeroyon 4"),
        BACKDROP_TOGGLE(21, "Zeroyon 5"),
    },
};

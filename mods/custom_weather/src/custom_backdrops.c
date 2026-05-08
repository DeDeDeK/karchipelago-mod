// Custom backdrops — random skybox selection in City Trial.
//
// Replaces City Trial's vanilla backdrop JObj (the city horizon /
// distant skyline subtree at GrObj+0xF4) with a backdrop carved out
// of a different stage's archive. Pool of 21 carved donor backdrops
// plus a "Vanilla" no-op option lives in `mods/custom_weather/assets/`,
// extracted by `scripts/carve_all_backdrops.py`.
//
// Mechanism:
//   stage init pipeline is grLoadStage -> grLoadStageArchive ->
//   3D_CreateStageModel (0x800dcbf0). The last reads
//   grdata->model_section (a ModelSection * at grdata + 0x0C) and
//   instantiates ms.terrain and ms.backdrop as JObj trees. By
//   overriding ms.backdrop *before* 3D_CreateStageModel reads it,
//   the stock loader does the work for us — instantiation,
//   JObj+0x2C scale set-up, attach to GrObj+0xF4 — using the
//   donor's backdrop description.
//
// Selection / lifetime:
//   On every CT stage init we pick uniformly from the user-enabled
//   subset of `backdrop_defs[]`. If "Vanilla" wins we fall through
//   and the stock loader uses CT's own ms.backdrop. Otherwise we
//   load the donor archive fresh and repoint ms.backdrop at the
//   donor's pp slot.
//
//   We deliberately do NOT cache the donor across rounds. Despite
//   passing heap_id 0, Archive_LoadFile ends up allocating into a
//   per-scene heap that is wiped on 3D scene exit (verified: a
//   stashed archive pointer reads back as all-zeros after returning
//   from player select). Reloading each round is cheap relative to
//   the full stage load and avoids a dangling-pointer footgun.
//
// See docs/sky-backdrop-system.md for the verified ModelSection
// layout and the gr_kind table that confirms which stage archives
// carry usable backdrops.

#include "os.h"
#include "game.h"
#include "stage.h"
#include "obj.h"
#include "hsd.h"
#include "code_patch/code_patch.h"
#include "hoshi/settings.h"

#include "custom_weather.h"

// Backdrop pool. Index 0 ("Vanilla") is the no-override path: the
// stock loader uses CT's own backdrop, which avoids spending heap
// when the user just wants the base skybox in the random pool.
//
// "City 1" is intentionally absent — it would be a duplicate of
// "Vanilla" since CT's own archive *is* GrCity1Model.dat. "Simple"
// (from the 14 MB GrSimpleModel system archive) is also skipped —
// its backdrop subtree is a 4 KB placeholder, almost certainly a
// dummy that won't render anything useful in CT.
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

// Per-entry enable toggle, default all on. Persisted by hoshi menu
// save (keyed by option name hash) once the user changes them.
static int backdrop_enabled[BACKDROP_NUM] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1,
};

static char *toggle_names[] = {"Disabled", "Enabled"};

// We don't cache the loaded archive across rounds. The heap that
// `Archive_LoadFile` allocates into is per-scene — its memory is
// zeroed on 3D scene exit (verified empirically: a cached pointer
// reads back as `file_size=0, data=0, flags=0` after returning from
// player select). So we simply reload on each CT entry and let the
// scene-exit teardown reclaim the storage. No `Archive_Free` needed.

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
    if (grobj == NULL || grobj->gr_kind != GRKIND_CITY1)
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

// Hook 3D_CreateStageModel at 0x800dcc18, immediately after r30 has
// been loaded with `grobj = gobj->user_data` (instruction at
// 0x800dcc14: `lwz r30, 44(r3)`). At this point the original code
// is about to read grdata->model_section into r29, so overriding
// ms.backdrop here takes effect on the very next instruction.
//
//   0x800dcc14  lwz r30, 44(r3)        ; r30 = grobj
//   0x800dcc18  lwz r3,  8(r30)        ; r3  = grobj->gr_data         <- HOOK
//   0x800dcc1c  lwz r29, 12(r3)        ; r29 = ModelSection *
//   ...
//   0x800dcc28  lwz r31, 4(r29)        ; r31 = ms->backdrop (overridden)
//
// The macro preserves and replays the clobbered instruction at
// 0x800dcc18 after the C callback returns, so r3 is correctly
// reloaded with grdata before execution resumes at 0x800dcc1c.
CODEPATCH_HOOKCREATE(0x800dcc18,
    "mr 3, 30\n\t",
    CustomBackdrop_Override,
    "",
    0x800dcc1c);

void CustomBackdrop_OnBoot(void)
{
    CODEPATCH_HOOKAPPLY(0x800dcc18);
    OSReport("[CustomBackdrop] Hook installed (%d backdrops in pool)\n",
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
    .option_num = BACKDROP_NUM + 2,
    .options = {
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

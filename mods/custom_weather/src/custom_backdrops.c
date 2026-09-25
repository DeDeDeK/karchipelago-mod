#include <string.h>

#include "os.h"
#include "game.h"
#include "stage.h"
#include "obj.h"
#include "hsd.h"
#include "code_patch/code_patch.h"
#include "hoshi/settings.h"

#include "custom_weather.h"
#include "weather_fx.h"

// Index 0 ("Vanilla") is the no-override path: the stock loader keeps CT's own
// backdrop, so nothing is rebuilt.
typedef struct BackdropDef
{
    const char *display_name;
    const char *key;        // manifest entry, NULL for vanilla
} BackdropDef;

#define BACKDROP_VANILLA_INDEX 0

static const BackdropDef backdrop_defs[] = {
    { "Vanilla",     NULL         },
    { "Check 2",     "Check2"     },
    { "Colosseum 1", "Colosseum1" },
    { "Colosseum 3", "Colosseum3" },
    { "Colosseum 5", "Colosseum5" },
    { "Dedede 1",    "Dedede1"    },
    { "Desert 1",    "Desert1"    },
    { "Heat 2",      "Heat2"      },
    { "Ice 1",       "Ice1"       },
    { "Jump 1",      "Jump1"      },
    { "Jump 2",      "Jump2"      },
    { "Jump 3",      "Jump3"      },
    { "Machine 2",   "Machine2"   },
    { "Pasture 1",   "Pasture1"   },
    { "Plants 1",    "Plants1"    },
    { "Sky 2",       "Sky2"       },
    { "Space 2",     "Space2"     },
    { "Valley 2",    "Valley2"    },
    { "Zeroyon 1",   "Zeroyon1"   },
    { "Zeroyon 3",   "Zeroyon3"   },
    { "Zeroyon 4",   "Zeroyon4"   },
    { "Zeroyon 5",   "Zeroyon5"   },
};
#define BACKDROP_NUM (sizeof(backdrop_defs) / sizeof(backdrop_defs[0]))

// Per-entry enable toggle, persisted by hoshi menu save (keyed by option name hash).
static int backdrop_enabled[BACKDROP_NUM] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1,
};
_Static_assert(sizeof(backdrop_enabled) / sizeof(backdrop_enabled[0]) == BACKDROP_NUM,
               "backdrop_enabled init must match BACKDROP_NUM");

// Multiplies the root-joint scale the loader stamps into every backdrop, pushing
// the whole sky dome out or pulling it in.
static const float backdrop_distance_factors[] = {1.0f, 1.25f, 1.5f, 1.75f, 2.0f};
static char *backdrop_distance_names[] = {"100%", "125%", "150%", "175%", "200%"};
#define BACKDROP_DISTANCE_NUM \
    ((int)(sizeof(backdrop_distance_factors) / sizeof(backdrop_distance_factors[0])))
static int backdrop_distance_index = 1; // default 125%

// Normalizes the picked backdrop's dome to City Trial's radius. Donors are modelled
// anywhere from ~1300 to ~10000 units and the loader stamps City's StageScale over
// whatever the root joint carried. Set per round by the override hook, consumed by
// the distance hook.
static float backdrop_geom_scale = 1.0f;

// Manifest and payload live in the per-scene heap, zeroed on 3D scene exit, so
// nothing here is freed or cached across rounds.

// A backdrop failure is a permanent condition (file missing, donor absent) and the
// override runs once per City Trial entry, so the reports latch.
static int stc_warned = 0;

// Returns 1 the first time it is called, so a failing branch reports once.
static int BackdropShouldWarn(void)
{
    if (stc_warned)
        return 0;
    stc_warned = 1;
    return 1;
}

static const BackdropManifestEntry *FindEntry(const BackdropManifest *m, const char *key)
{
    for (u32 i = 0; i < m->entry_num; i++)
    {
        if (strcmp(m->entries[i].key, key) == 0)
            return &m->entries[i];
    }
    return NULL;
}

// Returns the payload base, which leads with a pp slot shaped like a vanilla stage's
// grModel<X>[1] so it drops straight into ModelSection.backdrop. NULL if the donor is
// not on the disc.
static void *RebuildBackdrop(const BackdropManifestEntry *e)
{
    int entrynum = DVDConvertPathToEntrynum((char *)e->donor);
    if (entrynum < 0)
    {
        if (BackdropShouldWarn())
            OSReport("[CustomBackdrop] %s not on disc\n", e->donor);
        return NULL;
    }

    // File_Read DMAs straight into the buffer, so the base has to be 32-byte
    // aligned; the ranges are already sized and placed in multiples of 32.
    u8 *alloc = Heap_Alloc(0, e->payload_size + 31);
    if (alloc == NULL)
    {
        if (BackdropShouldWarn())
            OSReport("[CustomBackdrop] %s alloc failed (%d bytes)\n",
                     e->donor, e->payload_size + 31);
        return NULL;
    }
    u8 *base = (u8 *)(((u32)alloc + 31) & ~31);

    // No range covers the leading pp slot, and the heap hands back dirty memory, so
    // clear it here: everything past word 0 must read as "no model motion".
    for (u32 i = 0; i < BACKDROP_PP_SLOT / 4; i++)
        ((u32 *)base)[i] = 0;

    for (u32 i = 0; i < e->range_num; i++)
    {
        const BackdropRange *r = &e->ranges[i];
        *stc_file_read_done = 0;
        File_Read(entrynum, r->donor_off, base + r->dest_off, r->length,
                  0x21, 1, File_ReadDone, NULL);
        while (File_Wait() == 0)
            ;
    }

    for (u32 i = 0; i < e->reloc_num; i++)
    {
        const BackdropReloc *rl = &e->relocs[i];
        *(u32 *)(base + rl->dest_off) = (u32)base + rl->dest_val;
    }

    // Word 0 of the pp slot is the backdrop root; the rest stays zero (no motion).
    *(u32 *)base = (u32)base + e->root_off;
    return base;
}

static void CustomBackdrop_Override(GrObj *grobj)
{
    if (grobj == NULL || grobj->gr_kind != GR_CITY1)
        return;

    backdrop_geom_scale = 1.0f;

    int picked = Weather_PickEnabled(backdrop_enabled, (int)BACKDROP_NUM);
    if (picked < 0)
        picked = BACKDROP_VANILLA_INDEX;

    if (picked == BACKDROP_VANILLA_INDEX)
    {
        OSReport("[CustomBackdrop] Selected Vanilla\n");
        return;
    }

    const BackdropDef *def = &backdrop_defs[picked];

    ModelSection *ct_ms = grobj->gr_data->model_section;
    if (ct_ms == NULL)
        return;

    HSD_Archive *arc = Archive_LoadFile(BACKDROP_MANIFEST_FILE);
    if (arc == NULL)
    {
        if (BackdropShouldWarn())
            OSReport("[CustomBackdrop] %s missing\n", BACKDROP_MANIFEST_FILE);
        return;
    }

    const BackdropManifest *m = Archive_GetPublicAddress(arc, BACKDROP_MANIFEST_SYMBOL);
    if (m == NULL || m->magic != BACKDROP_MANIFEST_MAGIC
        || m->version != BACKDROP_MANIFEST_VERSION)
    {
        if (BackdropShouldWarn())
            OSReport("[CustomBackdrop] %s unusable (magic/version)\n", BACKDROP_MANIFEST_FILE);
        return;
    }

    const BackdropManifestEntry *e = FindEntry(m, def->key);
    if (e == NULL)
    {
        if (BackdropShouldWarn())
            OSReport("[CustomBackdrop] no manifest entry for %s\n", def->key);
        return;
    }

    void *payload = RebuildBackdrop(e);
    if (payload == NULL)
        return;

    ct_ms->backdrop = (JOBJDesc **)payload;
    backdrop_geom_scale = e->scale;
    OSReport("[CustomBackdrop] Selected %s (%d KB from %s)\n",
             def->display_name, e->payload_size / 1024, e->donor);
}

// Inside CreateStageModel_3D (0x800dcbf0), after r30 = grobj and just before it
// reads grdata->model_section, so the ms.backdrop override lands on the next
// instruction. The macro replays the clobbered `lwz r3, 8(r30)`.
CODEPATCH_HOOKCREATE(0x800dcc18,
    "mr 3, 30\n\t",
    CustomBackdrop_Override,
    "",
    0);

// Both corrections ride on the one scale CreateStageModel_3D stamps into the root
// joint: the geometry factor equalizes donors modelled at different raw sizes, and
// the user's distance factor then moves the whole dome.
static void CustomBackdrop_ScaleDistance(GrObj *grobj, JOBJ *backdrop)
{
    if (grobj == NULL || backdrop == NULL || grobj->gr_kind != GR_CITY1)
        return;

    float f = backdrop_distance_factors[backdrop_distance_index] * backdrop_geom_scale;
    if (f == 1.0f)
        return;

    backdrop->scale.X *= f;
    backdrop->scale.Y *= f;
    backdrop->scale.Z *= f;
}

// Inside CreateStageModel_3D (0x800dcbf0), immediately after the backdrop branch
// stamps the scale into the root joint; r30 = grobj, r29 = backdrop JObj (both
// non-volatile). The macro replays the clobbered `lwz r0, 20(r29)`, so the
// classical-scaling flag check that follows sees the rescaled joint.
CODEPATCH_HOOKCREATE(0x800dce84,
    "mr 3, 30\n\t"
    "mr 4, 29\n\t",
    CustomBackdrop_ScaleDistance,
    "",
    0);

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
    Weather_SetAllEnabled(backdrop_enabled, (int)BACKDROP_NUM, 1);
    return 1;
}

static int DisableAllBackdrops(OptionDesc *self)
{
    (void)self;
    Weather_SetAllEnabled(backdrop_enabled, (int)BACKDROP_NUM, 0);
    return 1;
}

static void OnBackdropDistanceChange(int val)
{
    OSReport("[CustomBackdrop] Distance %s\n", backdrop_distance_names[val]);
}

#define BACKDROP_TOGGLE(idx, label) \
    &(OptionDesc){ \
        .name = label, \
        .description = "Let this backdrop appear in the City Trial skybox roll", \
        .kind = OPTKIND_VALUE, \
        .val = &backdrop_enabled[idx], \
        .value_num = 2, \
        .value_names = weather_enable_names, \
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
            .on_change = OnBackdropDistanceChange,
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

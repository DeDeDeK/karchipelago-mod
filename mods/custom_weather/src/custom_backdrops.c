#include <string.h>

#include "os.h"
#include "game.h"
#include "stage.h"
#include "obj.h"
#include "hsd.h"
#include "preload.h"
#include "code_patch/code_patch.h"
#include "inline.h"
#include "hoshi/settings.h"

#include "custom_weather.h"
#include "weather_fx.h"

// BackdropManifest.dat holds no game data: each entry is a recipe for rebuilding one
// stage's backdrop subtree out of the retail disc, so no vanilla geometry or texture
// ships with the mod.
#define BACKDROP_MANIFEST_FILE    "BackdropManifest.dat"
#define BACKDROP_MANIFEST_SYMBOL  "backdropManifest"
#define BACKDROP_MANIFEST_MAGIC   0x42444D46u  // 'BDMF'
#define BACKDROP_MANIFEST_VERSION 1

// Bytes the payload reserves before the first range, holding a pp slot shaped like a
// vanilla stage's grModel<X>[1]: word 0 is the backdrop root, the rest reads as zero.
#define BACKDROP_PP_SLOT 0x20

// All three fields are multiples of 32, which is what File_Read requires of an
// offset, a length and a destination alike.
typedef struct BackdropRange
{
    u32 donor_off;   // 0x00 absolute offset in the donor file
    u32 dest_off;    // 0x04 offset in the payload
    u32 length;      // 0x08
} BackdropRange;

// The bytes arrive raw off the disc, so every pointer still holds a donor offset;
// dest_val is that offset already in payload coordinates, leaving the runtime to add
// the payload base.
typedef struct BackdropReloc
{
    u32 dest_off;    // 0x00 where the pointer sits in the payload
    u32 dest_val;    // 0x04 payload offset it should point at
} BackdropReloc;

typedef struct BackdropManifestEntry
{
    const char *key;              // 0x00 grModel<X> suffix, e.g. "Check2"
    const char *donor;            // 0x04 e.g. "GrCheck2Model.dat"
    u32 payload_size;             // 0x08
    float scale;                  // 0x0C normalizes the dome to City's radius
    u32 root_off;                 // 0x10 backdrop JOBJDesc, payload-relative
    const BackdropRange *ranges;  // 0x14
    u32 range_num;                // 0x18
    const BackdropReloc *relocs;  // 0x1C
    u32 reloc_num;                // 0x20
} BackdropManifestEntry;          // 0x24

typedef struct BackdropManifest
{
    u32 magic;                            // 0x00 BACKDROP_MANIFEST_MAGIC
    u32 version;                          // 0x04
    u32 entry_num;                        // 0x08
    const BackdropManifestEntry *entries; // 0x0C
} BackdropManifest;

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
    { "Ice 1",       "Ice1"       },
    { "Jump 1",      "Jump1"      },
    { "Jump 2",      "Jump2"      },
    { "Jump 3",      "Jump3"      },
    { "Machine 2",   "Machine2"   },
    { "Pasture 1",   "Pasture1"   },
    { "Plants 1",    "Plants1"    },
    { "Zeroyon 1",   "Zeroyon1"   },
};
#define BACKDROP_NUM ((int)GetElementsIn(backdrop_defs))

// Per-entry enable toggle, persisted by hoshi menu save (keyed by option name hash).
static int backdrop_enabled[BACKDROP_NUM] = {[0 ... BACKDROP_NUM - 1] = 1};

static WeatherPool backdrop_pool = {backdrop_enabled, BACKDROP_NUM, NULL};

// Multiplies the root-joint scale the loader stamps into every backdrop, pushing the
// whole sky dome out.
static const float backdrop_distance_factors[] = {1.0f, 1.25f, 1.5f, 1.75f, 2.0f};
static char *backdrop_distance_names[] = {"100%", "125%", "150%", "175%", "200%"};
WEATHER_KNOB_CHECK(backdrop_distance_factors, backdrop_distance_names);
static int backdrop_distance_index = 1; // default 125%

float CustomBackdrop_GetDistance(void)
{
    return backdrop_distance_factors[backdrop_distance_index];
}

// Normalizes the picked backdrop's dome to City Trial's radius. Donors are modelled
// anywhere from ~1300 to ~10000 units and the loader stamps City's StageScale over
// whatever the root joint carried. Set per round by the override hook, consumed by
// the distance hook.
static float backdrop_geom_scale = 1.0f;

// GrCity1Model.dat is preloaded into a heap that can outlive the round, and the loader
// reuses a resident preload as-is, so the overridden ModelSection slot is put back at
// the 3D exit, before the scene heap holding the payload is reset.
static ModelSection *stc_override_ms = NULL;
static JOBJDesc **stc_vanilla_backdrop = NULL;

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

    // The heap hands out 32-byte-aligned blocks, which File_Read's DMA requires of the
    // destination; the ranges are already sized and placed in multiples of 32.
    u8 *base = Heap_Alloc(PRELOADHEAPKIND_HSD, e->payload_size);
    if (base == NULL)
    {
        if (BackdropShouldWarn())
            OSReport("[CustomBackdrop] %s alloc failed (%d bytes)\n",
                     e->donor, e->payload_size);
        return NULL;
    }

    // No range covers the leading pp slot, and the heap hands back dirty memory, so
    // clear it here: everything past word 0 must read as "no model motion".
    memset(base, 0, BACKDROP_PP_SLOT);

    for (u32 i = 0; i < e->range_num; i++)
    {
        const BackdropRange *r = &e->ranges[i];
        *stc_file_read_done = 0;
        File_Read(entrynum, r->donor_off, base + r->dest_off, r->length,
                  DEVCOMSRC_DVD | DEVCOMDEST_DRAM, 1, File_ReadDone, NULL);
        while (File_PollReadDone() == 0)
            ;
    }

    for (u32 i = 0; i < e->reloc_num; i++)
    {
        const BackdropReloc *rl = &e->relocs[i];
        *(u32 *)(base + rl->dest_off) = (u32)base + rl->dest_val;
    }

    *(u32 *)base = (u32)base + e->root_off;
    return base;
}

static void CustomBackdrop_Override(GrObj *grobj)
{
    if (grobj->gr_kind != GR_CITY1)
        return;

    backdrop_geom_scale = 1.0f;

    int picked = Gm_Roll(backdrop_enabled, BACKDROP_NUM);
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

    // Archive_LoadFile asserts on a missing file rather than returning NULL.
    if (DVDConvertPathToEntrynum(BACKDROP_MANIFEST_FILE) < 0)
    {
        if (BackdropShouldWarn())
            OSReport("[CustomBackdrop] %s missing\n", BACKDROP_MANIFEST_FILE);
        return;
    }
    HSD_Archive *arc = Archive_LoadFile(BACKDROP_MANIFEST_FILE);

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

    stc_override_ms = ct_ms;
    stc_vanilla_backdrop = ct_ms->backdrop;
    ct_ms->backdrop = (JOBJDesc **)payload;
    backdrop_geom_scale = e->scale;
    OSReport("[CustomBackdrop] Selected %s (%d KB from %s)\n",
             def->display_name, e->payload_size / 1024, e->donor);
}

void CustomBackdrop_On3DExit(void)
{
    if (!stc_override_ms)
        return;
    stc_override_ms->backdrop = stc_vanilla_backdrop;
    stc_override_ms = NULL;
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
    if (grobj->gr_kind != GR_CITY1)
        return;

    float f = CustomBackdrop_GetDistance() * backdrop_geom_scale;
    backdrop->scale.X *= f;
    backdrop->scale.Y *= f;
    backdrop->scale.Z *= f;
}

// Inside CreateStageModel_3D (0x800dcbf0), immediately after the backdrop branch
// stamps the scale into the root joint; r30 = grobj, r29 = backdrop JObj (both
// non-volatile). The macro replays the clobbered `lwz r0, 20(r29)`, so the rescale
// lands before the loader's JOBJ_MTS_INDEPEND_SRT test and matrix-dirty mark.
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
    OSReport("[CustomBackdrop] Hooks installed (%d backdrops in pool)\n", BACKDROP_NUM);
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
        .value_num = WEATHER_ONOFF_NUM, \
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
            .value_num = GetElementsIn(backdrop_distance_factors),
            .value_names = backdrop_distance_names,
            .on_change = OnBackdropDistanceChange,
        },
        &(OptionDesc){
            .name = "Enable All",
            .description = "Enable all backdrops",
            .kind = OPTKIND_ACTION,
            .on_action = WeatherPool_EnableAll,
            .user_data = &backdrop_pool,
        },
        &(OptionDesc){
            .name = "Disable All",
            .description = "Disable all backdrops",
            .kind = OPTKIND_ACTION,
            .on_action = WeatherPool_DisableAll,
            .user_data = &backdrop_pool,
        },
        BACKDROP_TOGGLE(0,  "Vanilla"),
        BACKDROP_TOGGLE(1,  "Check 2"),
        BACKDROP_TOGGLE(2,  "Colosseum 1"),
        BACKDROP_TOGGLE(3,  "Colosseum 3"),
        BACKDROP_TOGGLE(4,  "Colosseum 5"),
        BACKDROP_TOGGLE(5,  "Dedede 1"),
        BACKDROP_TOGGLE(6,  "Desert 1"),
        BACKDROP_TOGGLE(7,  "Ice 1"),
        BACKDROP_TOGGLE(8,  "Jump 1"),
        BACKDROP_TOGGLE(9,  "Jump 2"),
        BACKDROP_TOGGLE(10, "Jump 3"),
        BACKDROP_TOGGLE(11, "Machine 2"),
        BACKDROP_TOGGLE(12, "Pasture 1"),
        BACKDROP_TOGGLE(13, "Plants 1"),
        BACKDROP_TOGGLE(14, "Zeroyon 1"),
    },
};

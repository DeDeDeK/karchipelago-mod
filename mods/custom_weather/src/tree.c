// Wind bends the City Trial forest trees: each frame a small tilt following the
// global wind vector is written into each tree's skeleton joint rotation. Visual
// only - collision is never touched.

#include "os.h"
#include "game.h"
#include "obj.h"
#include "stage.h"
#include "yakumono.h"
#include "hoshi/settings.h"

#include "custom_weather.h"
#include "weather_fx.h"

#define TREE_DESC_ID     34  // forest trees (weak break family, hitWeakObject)
#define TREE_MAX         96  // CT ships 53; headroom for the enumeration cache
#define TREE_MAX_PARENTS 16  // tree-family yakumono GObjs owning the instances

// The lean angle scales with wind speed up to a cap; a per-tree sinusoidal gust
// breaks the grove out of lockstep.
#define TREE_BEND_PER_SPEED 0.018f  // radians of lean per world-unit of wind speed
#define TREE_BEND_MAX       0.25f   // hard cap on lean (~14 deg)
#define TREE_RUSTLE         0.22f   // per-tree gust modulates the lean +/- this fraction
#define TREE_RUSTLE_FREQ    0.09f   // gust phase advance per frame
#define TREE_PHASE_STEP     0.7f    // phase offset between adjacent trees (radians)

typedef struct TreeEntry
{
    JOBJ *jobj;            // the skeleton joint we tilt
    GrCollRecord *record;  // placed instance, for the intact/broken gate
    Vec4  base_rot;  // authored rotation, restored under it each frame
} TreeEntry;

static TreeEntry stc_trees[TREE_MAX];
static int   stc_tree_count = 0;
static int   stc_enumerated = 0;
static float stc_phase = 0.0f;

// Preset = trees bend in wind.
static char *tree_toggle_names[] = {"Preset", "Off", "On"};
static int   tree_enabled = 0;

static const float tree_strength_factors[] = {1.0f, 0.6f, 1.0f, 1.6f};
static char *tree_strength_names[] = {"Preset", "Subtle", "Normal", "Strong"};
#define TREE_STRENGTH_NUM ((int)(sizeof(tree_strength_factors) / sizeof(tree_strength_factors[0])))
static int tree_strength_index = 0;

// Collect the forest-tree yakumono GObjs (desc_id 34) into `out`, returning the
// count. Every node of the yakumono GObj list is a live GObj, so its
// userdata->desc_id is real; a scene record's owner slot is only ever compared
// against these by pointer, never dereferenced.
static int Tree_CollectParents(GOBJ **out, int max)
{
    int n = 0;
    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_YAKUMONO];
         g != NULL && n < max; g = g->next)
    {
        if (g->entity_class != YAKUMONO_GOBJ_KIND)
            continue;
        YakumonoData *yd = (YakumonoData *)g->userdata;
        if (yd != NULL && yd->desc_id == TREE_DESC_ID)
            out[n++] = g;
    }
    return n;
}

// Scan the ground scene-instance pool once per stage, caching every forest-tree
// joint and its authored rotation. The pool holds every placed prop; trees are the
// records whose owner is one of the tree-family yakumono GObjs.
static void Tree_Enumerate(void)
{
    stc_tree_count = 0;

    int count = 0;
    GrCollRecord *pool = Gr_GetCollRecords(&count);
    if (pool == NULL)
        return; // scene not built yet; retry next frame

    // Pool exists: enumerate once even if the stage carries no trees.
    stc_enumerated = 1;

    GOBJ *parents[TREE_MAX_PARENTS];
    int nparents = Tree_CollectParents(parents, TREE_MAX_PARENTS);
    if (nparents == 0)
    {
        OSReport("[Trees] No forest trees on this stage, sway is off\n");
        return;
    }

    for (int i = 0; i < count && stc_tree_count < TREE_MAX; i++)
    {
        GrCollRecord *record = &pool[i];
        GOBJ *owner = record->yaku_gobj;
        if (owner == NULL)
            continue;

        // Pointer match only - never deref a record owner.
        int is_tree = 0;
        for (int p = 0; p < nparents; p++)
        {
            if (parents[p] == owner)
            {
                is_tree = 1;
                break;
            }
        }
        if (!is_tree)
            continue;

        JOBJ *j = record->jobj;
        if (j == NULL)
            continue;

        stc_trees[stc_tree_count].jobj = j;
        stc_trees[stc_tree_count].record = record;
        stc_trees[stc_tree_count].base_rot = j->rot;
        stc_tree_count++;
    }

    OSReport("[Trees] Enumerated %d forest trees\n", stc_tree_count);
}

void Tree_Tick(void)
{
    if (!WeatherToggle(tree_enabled, 1))
        return;

    if (!stc_enumerated)
        Tree_Enumerate();
    if (stc_tree_count == 0)
        return;

    Vec3 w;
    Wind_GetVector(&w);
    float mag = sqrtf(w.X * w.X + w.Z * w.Z);

    float bend = mag * TREE_BEND_PER_SPEED * tree_strength_factors[tree_strength_index];
    if (bend > TREE_BEND_MAX)
        bend = TREE_BEND_MAX;

    // Downwind unit direction; 0 when calm so trees hold at their base rot.
    float dirx = 0.0f, dirz = 0.0f;
    if (mag > 1e-4f)
    {
        dirx = w.X / mag;
        dirz = w.Z / mag;
    }

    stc_phase += TREE_RUSTLE_FREQ;

    for (int i = 0; i < stc_tree_count; i++)
    {
        TreeEntry *t = &stc_trees[i];

        // A knocked-down tree has its collision retired and the break tail owns
        // its joint from then on.
        if (!grScene_IsInstanceCollAll(t->record, 1))
            continue;

        float gust = 1.0f + TREE_RUSTLE * sinf(stc_phase + (float)i * TREE_PHASE_STEP);
        float theta = bend * gust;

        // Tip the trunk (+Y) toward the wind heading: rotating about X leans the
        // top toward +Z, about Z toward -X.
        t->jobj->rot.X = t->base_rot.X + theta * dirz;
        t->jobj->rot.Z = t->base_rot.Z - theta * dirx;
    }
}

void Tree_Reset(void)
{
    stc_enumerated = 0;
    stc_tree_count = 0;
    stc_phase = 0.0f;
}

MenuDesc tree_menu = {
    .option_num = 2,
    .options = {
        &(OptionDesc){
            .name = "Bend in Wind",
            .description = "Let wind lean the City Trial forest trees (visual only; Preset = on, calm = rigid)",
            .kind = OPTKIND_VALUE,
            .val = &tree_enabled,
            .value_num = 3,
            .value_names = tree_toggle_names,
        },
        &(OptionDesc){
            .name = "Sway Strength",
            .description = "How far the trees lean at a given wind speed",
            .kind = OPTKIND_VALUE,
            .val = &tree_strength_index,
            .value_num = TREE_STRENGTH_NUM,
            .value_names = tree_strength_names,
        },
    },
};

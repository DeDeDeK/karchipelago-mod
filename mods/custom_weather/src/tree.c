// Wind bends the City Trial forest trees: each frame a small tilt following the
// global wind vector is written into each tree's skeleton joint rotation. Visual
// only - collision is never touched.

#include "os.h"
#include "game.h"
#include "obj.h"
#include "stage.h"
#include "yakumono.h"
#include "inline.h"
#include "hoshi/settings.h"

#include "custom_weather.h"
#include "weather_fx.h"

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
    Vec4  base_rot;        // authored rotation the lean is added to
} TreeEntry;

static TreeEntry stc_trees[TREE_MAX];
static int   stc_tree_count = 0;
static int   stc_enumerated = 0;
static int   stc_bent = 0;     // some tree is off its base rotation
static float stc_phase = 0.0f;

static int   tree_enabled = 1;

// Trees have no per-preset config, so this knob has no Preset value.
static const float tree_strength_factors[] = {0.6f, 1.0f, 1.6f};
static char *tree_strength_names[] = {"Subtle", "Normal", "Strong"};
WEATHER_KNOB_CHECK(tree_strength_factors, tree_strength_names);
static int tree_strength_index = 1;

static int Tree_IsTreeKind(YakuKind kind)
{
    return kind == YAKUKIND_TREE;
}

// Trees are the scene-instance records whose owner is one of the tree-family
// yakumono GObjs. Scanned once per round.
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
    int nparents = Yaku_CollectGObjs(Tree_IsTreeKind, parents, TREE_MAX_PARENTS);
    if (nparents == 0)
    {
        OSReport("[Trees] No forest trees on this stage, sway is off\n");
        return;
    }

    for (int i = 0; i < count && stc_tree_count < TREE_MAX; i++)
    {
        GrCollRecord *record = &pool[i];
        if (!Gr_IsRecordOwnedBy(record, parents, nparents))
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
    if (!tree_enabled)
        return;

    if (!stc_enumerated)
        Tree_Enumerate();
    if (stc_tree_count == 0)
        return;

    float dirx, dirz;
    float speed = Wind_GetHeading(&dirx, &dirz);

    // A calm wind holds every tree at its base, so once one pass has put them back
    // there is nothing to write.
    if (speed <= 0.0f && !stc_bent)
        return;
    stc_bent = speed > 0.0f;

    float bend = speed * TREE_BEND_PER_SPEED * tree_strength_factors[tree_strength_index];

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
        if (theta > TREE_BEND_MAX)
            theta = TREE_BEND_MAX;

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
    stc_bent = 0;
    stc_phase = 0.0f;
}

MenuDesc tree_menu = {
    .option_num = 2,
    .options = {
        &(OptionDesc){
            .name = "Bend in Wind",
            .description = "Let wind lean the City Trial forest trees (visual only; calm wind = rigid)",
            .kind = OPTKIND_VALUE,
            .val = &tree_enabled,
            .value_num = WEATHER_ONOFF_NUM,
            .value_names = weather_onoff_names,
        },
        &(OptionDesc){
            .name = "Sway Strength",
            .description = "How far the trees lean at a given wind speed",
            .kind = OPTKIND_VALUE,
            .val = &tree_strength_index,
            .value_num = GetElementsIn(tree_strength_factors),
            .value_names = tree_strength_names,
        },
    },
};

#include "os.h"
#include "hoshi/mod.h"
#include "hoshi/settings.h"

#include "environment_destruction.h"

static char *stc_toggle_names[] = {
    "Disabled",
    "Enabled",
};

// Order must match EnvFillMode.
static char *stc_fill_names[] = {
    "Stretch texture", // ENV_FILL_TEXTURE
    "Flat colour",     // ENV_FILL_COLOR
};

// Order must match env_size_table.
static char *stc_size_names[] = {
    "Tiny",
    "Small",
    "Medium",
    "Large",
    "Huge",
};

// Order must match env_depth_table.
static char *stc_depth_names[] = {
    "Shallow",
    "Deep",
    "Through",
};

// Order must match env_shape_table.
static char *stc_shape_names[] = {
    "Square bore",    // ENV_SHAPE_BORE
    "Square pyramid", // ENV_SHAPE_PYRAMID4
    "Tri pyramid",    // ENV_SHAPE_PYRAMID3
};

static void OnBoot(void)
{
    EnvDestruct_OnBoot();
}

static void On3DLoadEnd(void)
{
    EnvDestruct_On3DLoadEnd();
}

static void On3DExit(void)
{
    EnvDestruct_On3DExit();
}

static void OnFrameEnd(void)
{
    EnvDestruct_OnFrameEnd();
}

static void OnChangeEnabled(int val)
{
    OSReport("[EnvDestruct] %s\n", val ? "enabled" : "disabled");
}

static MenuDesc sources_menu = {
    .option_num = 4,
    .options = {
        &(OptionDesc){
            .name = "Spin",
            .description = "Quick / tornado spins carve nearby walls",
            .kind = OPTKIND_VALUE,
            .val = &env_src_spin,
            .value_num = 2,
            .value_names = stc_toggle_names,
        },
        &(OptionDesc){
            .name = "Projectiles",
            .description = "Projectiles and items carve on impact",
            .kind = OPTKIND_VALUE,
            .val = &env_src_projectile,
            .value_num = 2,
            .value_names = stc_toggle_names,
        },
        &(OptionDesc){
            .name = "Machine Ram",
            .description = "Ramming a wall at speed carves it",
            .kind = OPTKIND_VALUE,
            .val = &env_src_machine,
            .value_num = 2,
            .value_names = stc_toggle_names,
        },
        &(OptionDesc){
            .name = "Charge / Dash",
            .description = "A charged machine carves on contact",
            .kind = OPTKIND_VALUE,
            .val = &env_src_charge,
            .value_num = 2,
            .value_names = stc_toggle_names,
        },
    },
};

static MenuDesc top_menu = {
    .option_num = 8,
    .options = {
        &(OptionDesc){
            .name = "Enabled",
            .description = "Enable environment destruction",
            .kind = OPTKIND_VALUE,
            .val = &env_destruct_enabled,
            .value_num = 2,
            .value_names = stc_toggle_names,
            .on_change = OnChangeEnabled,
        },
        &(OptionDesc){
            .name = "Chunk Size",
            .description = "How wide a bite each impact takes",
            .kind = OPTKIND_VALUE,
            .val = &env_destruct_size_sel,
            .value_num = ENV_SIZE_NUM,
            .value_names = stc_size_names,
        },
        &(OptionDesc){
            .name = "Chunk Depth",
            .description = "How far into the wall the chunk reaches",
            .kind = OPTKIND_VALUE,
            .val = &env_destruct_depth_sel,
            .value_num = ENV_DEPTH_NUM,
            .value_names = stc_depth_names,
        },
        &(OptionDesc){
            .name = "Chunk Shape",
            .description = "Bores tunnel straight; pyramids wander",
            .kind = OPTKIND_VALUE,
            .val = &env_destruct_shape_sel,
            .value_num = ENV_SHAPE_NUM,
            .value_names = stc_shape_names,
        },
        &(OptionDesc){
            .name = "Crater Fill",
            .description = "How the faces inside a chunk are shaded",
            .kind = OPTKIND_VALUE,
            .val = &env_destruct_fill,
            .value_num = ENV_FILL_NUM,
            .value_names = stc_fill_names,
        },
        &(OptionDesc){
            .name = "Damage Sources",
            .description = "What causes destruction",
            .kind = OPTKIND_MENU,
            .menu_ptr = &sources_menu,
        },
        &(OptionDesc){
            .name = "D Pad self test",
            .description = "Tap D-Pad Up to carve at player 1",
            .kind = OPTKIND_VALUE,
            .val = &env_destruct_selftest,
            .value_num = 2,
            .value_names = stc_toggle_names,
        },
        &(OptionDesc){
            .name = "Debug Log",
            .description = "Report each impact over OSReport",
            .kind = OPTKIND_VALUE,
            .val = &env_destruct_debug_log,
            .value_num = 2,
            .value_names = stc_toggle_names,
        },
    },
};

OptionDesc ModSettings = {
    .name = "Environment Destruction",
    .description = "Knock chunks out of the City Trial environment",
    .kind = OPTKIND_MENU,
    .menu_ptr = &top_menu,
};

ModDesc mod_desc = {
    .name = "environment_destruction",
    .author = "DeDeDK",
    .version.major = 1,
    .version.minor = 0,
    .affects_gameplay = 1,
    .option_desc = &ModSettings,
    .OnBoot = OnBoot,
    .On3DLoadEnd = On3DLoadEnd,
    .On3DExit = On3DExit,
    .OnFrameEnd = OnFrameEnd,
};

#include "game.h"
#include "topride.h"
#include "os.h"
#include "inline.h"

#include "topride_ai.h"

#define TOPRIDE_AGGRESSION_MAX 100.0f

int topride_ai_menu = TOPRIDE_AI_DEFAULT;

char *topride_ai_names[TOPRIDE_AI_MENU_NUM] = {
    [TOPRIDE_AI_DEFAULT] = "Default",
    [TOPRIDE_AI_CAUTIOUS] = "Cautious",
    [TOPRIDE_AI_AGGRESSIVE] = "Aggressive",
    [TOPRIDE_AI_HOARDER] = "Hoarder",
    [TOPRIDE_AI_EXPERT] = "Expert",
    [TOPRIDE_AI_RANDOM] = "Random",
};

// Multipliers on the reader's level's aggression weights (0-100); Expert plays at the top
// level instead.
typedef struct TopRidePresetDef
{
    float item;
    float rival;
    float ram;
} TopRidePresetDef;

static const TopRidePresetDef stc_presets[TOPRIDE_AI_RANDOM] = {
    [TOPRIDE_AI_CAUTIOUS] = {1.0f, 0.0f, 0.0f},
    [TOPRIDE_AI_AGGRESSIVE] = {0.5f, 3.0f, 3.0f},
    [TOPRIDE_AI_HOARDER] = {3.0f, 0.5f, 0.5f},
};

// Rolled for every slot at each Top Ride load.
static u8 stc_slot_preset[TOPRIDE_SLOT_NUM];

static float TopRideAI_Scale(float value, float mult)
{
    value *= mult;
    return value > TOPRIDE_AGGRESSION_MAX ? TOPRIDE_AGGRESSION_MAX : value;
}

// The reader's vt[3] in place of TopRide_CpuInputThink: runs the vanilla brain with this
// reader's level row of the aggression tables scaled for its preset, then restores the row.
static char TopRideAI_Think(TopRideCpuInputReader *reader, float *steer_out)
{
    int slot = TopRide_KirbyGetSlot(reader->kirby);
    int preset = slot < TOPRIDE_SLOT_NUM ? stc_slot_preset[slot] : TOPRIDE_AI_DEFAULT;
    const TopRidePresetDef *def = &stc_presets[preset];
    int level = reader->difficulty;
    TopRideCpuLevelParams *params = stc_topride_cpu_params;
    float item, rival, ram;
    char press;

    if (preset == TOPRIDE_AI_EXPERT)
        reader->difficulty = TOPRIDE_CPU_LEVEL_MAX;
    if (preset == TOPRIDE_AI_DEFAULT || preset == TOPRIDE_AI_EXPERT)
        return TopRide_CpuInputThink(reader, steer_out);

    item = params->item_aggression[level];
    rival = params->rival_aggression[level];
    ram = params->ram_aggression[level];
    params->item_aggression[level] = TopRideAI_Scale(item, def->item);
    params->rival_aggression[level] = TopRideAI_Scale(rival, def->rival);
    params->ram_aggression[level] = TopRideAI_Scale(ram, def->ram);

    press = TopRide_CpuInputThink(reader, steer_out);

    params->item_aggression[level] = item;
    params->rival_aggression[level] = rival;
    params->ram_aggression[level] = ram;
    return press;
}

// The title demo stays vanilla.
void TopRideAI_OnLoadEnd(void)
{
    int i;

    for (i = 0; i < TOPRIDE_SLOT_NUM; i++)
    {
        if (Gm_IsAutoDemo())
            stc_slot_preset[i] = TOPRIDE_AI_DEFAULT;
        else if (topride_ai_menu == TOPRIDE_AI_RANDOM)
            stc_slot_preset[i] = RandomInRange(TOPRIDE_AI_DEFAULT + 1, TOPRIDE_AI_RANDOM - 1);
        else
            stc_slot_preset[i] = topride_ai_menu;
    }
}

void TopRideAI_InstallHooks(void)
{
    stc_topride_cpu_reader_vtable[3] = TopRideAI_Think;
}

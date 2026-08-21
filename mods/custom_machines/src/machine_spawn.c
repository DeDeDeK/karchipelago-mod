// The City Trial field spawn roll, replaced because the engine rolls from a chance
// row in VcCommon.dat with exactly VCKIND_NUM columns and a selection loop the same
// width. The row seeds the vanilla kinds, each registered machine brings its
// descriptor's spawn_weight, and a consumer's filter gets the last word. Vanilla's
// four-deep history exclusion and weighted roll are kept.

#include "os.h"
#include "hsd.h"
#include "machine.h"
#include "code_patch/code_patch.h"

#include "custom_machines.h"

static CustomMachineSpawnWeightFilter stc_filter;

void CustomMachineSpawn_SetWeightFilter(CustomMachineSpawnWeightFilter filter)
{
    stc_filter = filter;
}

static float Weigh(int kind, float default_weight)
{
    if (stc_filter == NULL)
        return default_weight;
    return stc_filter(kind, default_weight);
}

// Every kind the filter permits at all, weighed alike. Reached only when the
// filter has zeroed the whole table, where a weighted roll has nothing to roll.
static int FirstPermitted(int kind_num)
{
    for (int i = 0; i < kind_num; i++)
    {
        if (Weigh(i, 1.0f) > 0)
            return i;
    }
    return VCKIND_COMPACT;
}

static int Select(MachineSpawnData *msd, float match_progress)
{
    vcDataCommon *common = *stc_vcDataCommon;
    int kind_num = CustomMachines_GetKindCeiling();
    float weight[CUSTOM_VCKIND_NUM];
    int table_idx = 0;
    int spawnable = 0;
    int history;
    int kind = VCKIND_COMPACT;
    float total = 0;
    float roll;

    while (match_progress > common->spawn_data->spawn_desc[table_idx].match_progress)
        table_idx++;

    for (int i = 0; i < kind_num; i++)
    {
        CustomMachineEntry *e;
        float base;

        if (i < VCKIND_NUM)
            base = common->spawn_data->spawn_desc[table_idx].chance[i];
        else
        {
            e = CustomMachines_FindByKind(i);
            base = e != NULL ? e->spawn_weight : 0.0f;
        }

        weight[i] = Weigh(i, base);
        if (weight[i] > 0)
            spawnable++;
    }

    if (spawnable == 0)
        return FirstPermitted(kind_num);

    history = (spawnable <= 4) ? (spawnable - 1) : 4;
    for (int i = 0; i < kind_num; i++)
    {
        for (int j = 0; j < history; j++)
        {
            if (i == msd->prev_machine_kind[j])
                weight[i] = 0;
        }
    }

    for (int i = 0; i < kind_num; i++)
        total += weight[i];

    roll = HSD_Randf() * total;
    total = 0;
    for (int i = 0; i < kind_num; i++)
    {
        total += weight[i];
        if (roll < total)
        {
            kind = i;
            break;
        }
    }
    return kind;
}

// Replace the selection in CityMachineSpawn_DecideAndSpawn (0x801defac). At
// 0x801df00c r30 = MachineSpawnData* and f1 = match_progress; the result goes to
// r31, which the vanilla code past the skip target writes to the spawn history and
// hands to CityMachineSpawn_Create.
CODEPATCH_HOOKCREATE(0x801df00c,
    "mr 3, 30\n\t",
    Select,
    "mr 31, 3\n\t",
    0x801df220
)

// Machine Formation event spawns, cityTrialSpawnFormationStar (0x801df408). Same
// register layout at its own hook point.
CODEPATCH_HOOKCREATE(0x801df44c,
    "mr 3, 30\n\t",
    Select,
    "mr 31, 3\n\t",
    0x801df630
)

void CustomMachineSpawn_OnBoot(void)
{
    CODEPATCH_HOOKAPPLY(0x801df00c);
    CODEPATCH_HOOKAPPLY(0x801df44c);
    OSReport("[MachineSpawn] City Trial spawn selection replaced\n");
}

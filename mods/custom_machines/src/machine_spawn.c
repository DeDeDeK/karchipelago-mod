// The City Trial field spawn roll, replaced because the engine rolls from a chance
// row in VcCommon.dat with exactly VCKIND_NUM columns and a selection loop no wider.
// The row seeds the vanilla kinds, each registered machine brings its descriptor's
// spawn_weight, and a consumer's filter gets the last word. Vanilla's four-deep
// history exclusion and weighted roll are kept.

#include "os.h"
#include "hsd.h"
#include "game.h"
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

// Machine Formation rolls stars only: its vanilla loop is VCSTAR_NUM wide.
static int IsRollable(int kind, int stars_only)
{
    if (!stars_only)
        return 1;
    if (kind < VCKIND_NUM)
        return !MachineKind_IsBike(kind);
    return !CustomMachines_FindByKind(kind)->is_bike;
}

// The lowest kind the filter permits at a weight of 1, for when it zeroed every seeded
// weight. A filter that permits nothing still gets the Compact Star.
static int FirstPermitted(int kind_num, int stars_only)
{
    for (int i = 0; i < kind_num; i++)
    {
        if (IsRollable(i, stars_only) && Weigh(i, 1.0f) > 0)
            return i;
    }
    return VCKIND_COMPACT;
}

static int Select(MachineSpawnData *msd, float match_progress, int stars_only)
{
    vcDataCommon *common = *stc_vcDataCommon;
    int kind_num = CustomMachines_GetKindCeiling();
    int history_max = sizeof(msd->prev_machine_kind);
    float weight[CUSTOM_VCKIND_NUM];
    int table_idx = 0;
    int spawnable = 0;
    int history;
    int kind = -1;
    float total = 0;
    float roll;

    // CityMachineSpawn_Think pins the progress at exactly 1.0 once the clock runs out,
    // and the engine rolls the first window for it.
    if (match_progress != 1.0f)
    {
        while (match_progress > common->spawn_data->spawn_desc[table_idx].match_progress)
            table_idx++;
    }

    for (int i = 0; i < kind_num; i++)
    {
        weight[i] = 0.0f;
        if (!IsRollable(i, stars_only))
            continue;

        float base = i < VCKIND_NUM ? common->spawn_data->spawn_desc[table_idx].chance[i]
                                    : CustomMachines_FindByKind(i)->spawn_weight;
        float w = Weigh(i, base);
        if (w > 0)
        {
            weight[i] = w;
            spawnable++;
        }
    }

    if (spawnable == 0)
        return FirstPermitted(kind_num, stars_only);

    history = spawnable <= history_max ? spawnable - 1 : history_max;
    for (int j = 0; j < history; j++)
    {
        int prev = msd->prev_machine_kind[j];
        if (prev < kind_num)
            weight[prev] = 0;
    }

    for (int i = 0; i < kind_num; i++)
        total += weight[i];

    // The last weighted kind stands in if rounding leaves the roll at the total.
    roll = HSD_Randf() * total;
    total = 0;
    for (int i = 0; i < kind_num; i++)
    {
        if (weight[i] <= 0)
            continue;
        kind = i;
        total += weight[i];
        if (roll < total)
            break;
    }
    return kind;
}

static int SelectField(MachineSpawnData *msd, float match_progress)
{
    return Select(msd, match_progress, 0);
}

static int SelectFormation(MachineSpawnData *msd, float match_progress)
{
    return Select(msd, match_progress, 1);
}

// Replace the selection in CityMachineSpawn_DecideAndSpawn (0x801defac). At
// 0x801df00c r30 = MachineSpawnData* and f1 = match_progress; the result goes to
// r31, which the vanilla code past the skip target writes to the spawn history and
// hands to CityMachineSpawn_Create.
CODEPATCH_HOOKCREATE(0x801df00c,
    "mr 3, 30\n\t",
    SelectField,
    "mr 31, 3\n\t",
    0x801df220
)

// Machine Formation event spawns, CityMachineSpawn_SpawnFormationStar (0x801df408), with
// the same registers at its own hook point.
CODEPATCH_HOOKCREATE(0x801df44c,
    "mr 3, 30\n\t",
    SelectFormation,
    "mr 31, 3\n\t",
    0x801df630
)

// Free Run keeps one machine of each kind on the field, counting them per absolute kind
// in the VCKIND_NUM-wide MachineSpawnData.freerun_placed through Machine_EncodeVehicleKind,
// which answers a custom machine's own MachineKind. A custom kind has no Free Run spot to
// be re-placed at, so it stays out of the counts. This is CityMachineSpawn_Init's
// per-player count, r28 the player and r31 the spawn data.
static void CountStartingMachine(int ply, MachineSpawnData *msd)
{
    int kind = CustomMachines_KindFromClassIndex(Ply_GetMachineIsBike(ply), Ply_GetMachineKind(ply));
    if (kind < VCKIND_NUM)
        msd->freerun_placed[kind]++;
}

CODEPATCH_HOOKCREATE(0x801de2ec,
    "mr 3, 28\n\tmr 4, 31\n\t",
    CountStartingMachine,
    "",
    0x801de304
)

static int IsCustomKind(int kind)
{
    return kind >= VCKIND_NUM;
}

// The take and release helpers' Free Run branch, r28 the kind. A custom kind skips to
// past the count.
CODEPATCH_HOOKCONDITIONALCREATE(0x801ded04, "mr 3, 28\n\t", IsCustomKind, "", 0, 0x801ded6c)
CODEPATCH_HOOKCONDITIONALCREATE(0x801dedd4, "mr 3, 28\n\t", IsCustomKind, "", 0, 0x801dee38)

void CustomMachineSpawn_OnBoot(void)
{
    CODEPATCH_HOOKAPPLY(0x801df00c);
    CODEPATCH_HOOKAPPLY(0x801df44c);
    CODEPATCH_HOOKAPPLY(0x801de2ec);
    CODEPATCH_HOOKAPPLY(0x801ded04);
    CODEPATCH_HOOKAPPLY(0x801dedd4);

    OSReport("[MachineSpawn] City Trial spawn selection and Free Run counts replaced\n");
}

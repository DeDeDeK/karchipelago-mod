// Some of the patch-stat scale pairs are kept one row per kind rather than one per class:
// blocks in the class table at MachineData.stat_scale - 19 rows for the star class, 7 for the
// bike class - indexed by MachineData.kind with no bound, so an appended slot would read
// the first rows of the block after its own. Each of those reads is rewritten in place to
// ask ScaleRow for the pair instead: six in Machine_ApplyStarStatScaling (0x801e81e4) and
// ten in Machine_ApplyBikeStatScaling (0x801f3d44).

#include "os.h"
#include "machine.h"
#include "code_patch/code_patch.h"

#include "custom_machines.h"

#define PPC_MR_R3_R30    0x7fc3f378 // mr r3, r30 (MachineData)
#define PPC_LI_R4        0x38800000 // li r4, 0
#define PPC_FMR_F1_F31   0xfc20f890 // fmr f1, f31 (the stat ratio)
#define PPC_FMR_F31_F1   0xffe00890 // fmr f31, f1
#define PPC_LWZ_R31_ATTR 0x83fe0650 // lwz r31, 0x650(r30)
#define PPC_LWZ_R29_ATTR 0x83be0650 // lwz r29, 0x650(r30)
#define PPC_NOP          0x60000000

#define BIKE_TURN_ROW_NUM 8

// Where each row's block starts in the class table, in the class's stat row order.
static const u16 stc_star_block[CUSTOM_MACHINE_STAT_ROW_NUM] = {
    0x068, 0x100, 0x328, 0x3c0, 0x538, 0x5d0,
};
static const u16 stc_bike_block[CUSTOM_MACHINE_BIKE_STAT_ROW_NUM] = {
    0x060, 0x098, 0x128, 0x160, 0x198, 0x1d0, 0x208, 0x240, 0x278, 0x2b0,
};

// A vanilla slot gets exactly the pair the engine would have read, a custom slot its
// descriptor's own.
static const float *ScaleRow(MachineData *md, int row)
{
    CustomMachineEntry *e = CustomMachines_FindByClassSlot(md->is_bike, md->kind);
    if (e != NULL)
        return e->stat_rows[row];

    const u16 *block = md->is_bike ? stc_bike_block : stc_star_block;
    return (const float *)(md->stat_scale + block[row] + md->kind * 8);
}

// The bike's ground Top Speed read carries the ratio save inside its four instructions,
// leaving no room to reload the ratio after a call, so the pair and its scale come back
// from one call standing in for the engine's own.
static float ScaleFromRow(MachineData *md, int row, float ratio)
{
    return Machine_ScaleFromRatio((float *)ScaleRow(md, row), ratio);
}

// Four instructions in place of a pair address: ask ScaleRow, then put the ratio back
// in f1 for the Machine_ScaleFromRatio call that follows. The ratio is held in f31 and
// the table bases in r29 and r31, which the call preserves.
static void EmitRowCall(u32 at, int row)
{
    CODEPATCH_REPLACEINSTRUCTION(at, PPC_MR_R3_R30);
    CODEPATCH_REPLACEINSTRUCTION(at + 4, PPC_LI_R4 | row);
    CODEPATCH_REPLACECALL(at + 8, ScaleRow);
    CODEPATCH_REPLACEINSTRUCTION(at + 12, PPC_FMR_F1_F31);
}

static void PatchStarRows(void)
{
    // Top Speed: lbz kind / slwi / addi block / add, with the ratio already in f1.
    EmitRowCall(0x801e83c0, CUSTOM_MACHINE_STAT_ROW_TOP_SPEED_GROUND);
    EmitRowCall(0x801e83e4, CUSTOM_MACHINE_STAT_ROW_TOP_SPEED_AIR);

    // Weight and Glide interleave the ratio save and the md->attr reload with those
    // four, so both move ahead of the call.
    CODEPATCH_REPLACEINSTRUCTION(0x801e8f38, PPC_FMR_F31_F1);
    CODEPATCH_REPLACEINSTRUCTION(0x801e8f3c, PPC_LWZ_R31_ATTR);
    EmitRowCall(0x801e8f40, CUSTOM_MACHINE_STAT_ROW_WEIGHT_AIR_IMPULSE);

    CODEPATCH_REPLACEINSTRUCTION(0x801e8f64, PPC_LWZ_R31_ATTR);
    EmitRowCall(0x801e8f68, CUSTOM_MACHINE_STAT_ROW_WEIGHT_AIR_RECOVER);

    CODEPATCH_REPLACEINSTRUCTION(0x801e8fc8, PPC_LWZ_R31_ATTR);
    EmitRowCall(0x801e8fcc, CUSTOM_MACHINE_STAT_ROW_GLIDE_AIR_IMPULSE);
    CODEPATCH_REPLACEINSTRUCTION(0x801e8fdc, PPC_NOP);

    CODEPATCH_REPLACEINSTRUCTION(0x801e9008, PPC_LWZ_R31_ATTR);
    EmitRowCall(0x801e900c, CUSTOM_MACHINE_STAT_ROW_GLIDE_AIR_RECOVER);
    CODEPATCH_REPLACEINSTRUCTION(0x801e901c, PPC_NOP);
}

static void PatchBikeRows(void)
{
    // Ground Top Speed: lbz kind / fmr f31, f1 / slwi / add, then the bl the pair feeds,
    // with the ratio in f1 throughout.
    CODEPATCH_REPLACEINSTRUCTION(0x801f3f90, PPC_FMR_F31_F1);
    CODEPATCH_REPLACEINSTRUCTION(0x801f3f94, PPC_MR_R3_R30);
    CODEPATCH_REPLACEINSTRUCTION(0x801f3f98, PPC_LI_R4 | CUSTOM_MACHINE_BIKE_STAT_ROW_TOP_SPEED_GROUND);
    CODEPATCH_REPLACECALL(0x801f3f9c, ScaleFromRow);
    CODEPATCH_REPLACEINSTRUCTION(0x801f3fa0, PPC_NOP);

    // Air Top Speed: lbz kind / slwi / addi block / add, with the ratio held in f31.
    EmitRowCall(0x801f3fb4, CUSTOM_MACHINE_BIKE_STAT_ROW_TOP_SPEED_AIR);

    // Turn: the same four with the md->attr reload into r29 after the first, which moves
    // ahead of the call. The eight reads are back to back.
    for (int i = 0; i < BIKE_TURN_ROW_NUM; i++)
    {
        u32 at = 0x801f41d8 + i * 0x28;

        CODEPATCH_REPLACEINSTRUCTION(at, PPC_LWZ_R29_ATTR);
        EmitRowCall(at + 4, CUSTOM_MACHINE_BIKE_STAT_ROW_TURN_074 + i);
    }
}

void CustomMachineStatScaling_OnBoot(void)
{
    // Each class's reads are rewritten only when it has a machine, so a class without
    // one runs the engine's own.
    if (CustomMachines_GetClassCount(0) > 0)
        PatchStarRows();
    if (CustomMachines_GetClassCount(1) > 0)
        PatchBikeRows();

    OSReport("[MachineStatScaling] Per-kind scale rows redirected for %d star(s) and %d bike(s)\n",
             CustomMachines_GetClassCount(0), CustomMachines_GetClassCount(1));
}

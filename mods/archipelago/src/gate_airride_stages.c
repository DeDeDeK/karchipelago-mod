#include "game.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_airride_stages.h"
#include "textbox.h"

static const char *airride_stage_names[AIRRIDE_NUM] = {
    [AIRRIDE_FANTASY_MEADOWS]  = "Fantasy Meadows",
    [AIRRIDE_CELESTIAL_VALLEY] = "Celestial Valley",
    [AIRRIDE_FROZEN_HILLSIDE]  = "Frozen Hillside",
    [AIRRIDE_MAGMA_FLOWS]      = "Magma Flows",
    [AIRRIDE_BEANSTALK_PARK]   = "Beanstalk Park",
    [AIRRIDE_MACHINE_PASSAGE]  = "Machine Passage",
    [AIRRIDE_SKY_SANDS]        = "Sky Sands",
    [AIRRIDE_CHECKER_KNIGHTS]  = "Checker Knights",
    [AIRRIDE_NEBULA_BELT]      = "Nebula Belt",
};

// Replacement for AirRide_CheckCourseUnlocked (0x8000c0e0).
// The vanilla function only checks stage_kind 8 (Nebula Belt) against the
// checklist. Our replacement checks ALL stages against the AP unlock mask.
// Returns 1 if the stage is unlocked, 0 if locked.
// Stage kinds outside 0-8 (e.g., 9 = random button) return 1 (always available).
int GateAirRideStages_CheckCourseUnlocked(s8 stage_kind)
{
    if (stage_kind < 0 || stage_kind >= AIRRIDE_NUM)
        return 1;
    return (save_data->airride_stage_unlocked_mask & (1 << stage_kind)) ? 1 : 0;
}

void GateAirRideStages_OnBoot()
{
    // Replace AirRide_CheckCourseUnlocked to check AP mask instead of vanilla checklist
    CODEPATCH_REPLACEFUNC(AirRide_CheckCourseUnlocked, GateAirRideStages_CheckCourseUnlocked);

    // Patch call site 1: AirRideSelect_Init (0x8003c114)
    // The vanilla code only checks stage_kind == 8 before calling CheckCourseUnlocked.
    // We patch out the guard so ALL stages go through the unlock check.
    // Original:  cmpwi r0, 8 / bne skip / li r3, 8
    // Patched:   mr r3, r0   / nop       / nop
    CODEPATCH_REPLACEINSTRUCTION(0x8003c210, 0x7c030378); // mr r3, r0
    CODEPATCH_REPLACEINSTRUCTION(0x8003c214, 0x60000000); // nop
    CODEPATCH_REPLACEINSTRUCTION(0x8003c218, 0x60000000); // nop

    // Patch call site 2: course init with random select (0x8003b4e8)
    // Same pattern as site 1.
    CODEPATCH_REPLACEINSTRUCTION(0x8003b520, 0x7c030378); // mr r3, r0
    CODEPATCH_REPLACEINSTRUCTION(0x8003b524, 0x60000000); // nop
    CODEPATCH_REPLACEINSTRUCTION(0x8003b528, 0x60000000); // nop

    // Patch call site 3: AirRide_RandomStageSelect (0x8000dd4c)
    // Loop var is r27. Original: cmpwi r27, 8 / bne skip / li r3, 8
    // Patched:   mr r3, r27  / nop       / nop
    CODEPATCH_REPLACEINSTRUCTION(0x8000ddc4, 0x7f63db78); // mr r3, r27
    CODEPATCH_REPLACEINSTRUCTION(0x8000ddc8, 0x60000000); // nop
    CODEPATCH_REPLACEINSTRUCTION(0x8000ddcc, 0x60000000); // nop

    // Patch call site 4: gmLanMenu_RenderMainMenuUI (0x80052070)
    // Slightly different layout: cmpwi r28, 8 / beq check / li r0, 1 / b past / li r3, 8
    // We NOP the guard and unconditional branch so all stages call the check.
    // Patched: mr r3, r28 / nop / nop / nop / nop
    CODEPATCH_REPLACEINSTRUCTION(0x80052070, 0x7f83e378); // mr r3, r28
    CODEPATCH_REPLACEINSTRUCTION(0x80052074, 0x60000000); // nop
    CODEPATCH_REPLACEINSTRUCTION(0x80052078, 0x60000000); // nop
    CODEPATCH_REPLACEINSTRUCTION(0x8005207c, 0x60000000); // nop
    CODEPATCH_REPLACEINSTRUCTION(0x80052080, 0x60000000); // nop

    OSReport("Air Ride stage gating installed (%d patches)\n", 14);
}

int GateAirRideStages_UnlockStage(int stage_kind)
{
    if (stage_kind < 0 || stage_kind >= AIRRIDE_NUM)
        return 0;

    save_data->airride_stage_unlocked_mask |= (1 << stage_kind);
    OSReport("Air Ride stage %d (%s) unlocked (mask = 0x%04x)\n",
             stage_kind, airride_stage_names[stage_kind], save_data->airride_stage_unlocked_mask);
    TextBox_Enqueue(airride_stage_names[stage_kind]);
    return 1;
}

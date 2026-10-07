#include "game.h"
#include "os.h"
#include "audio.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_topride_stages.h"
#include "textbox_api.h"
#include "inline.h"
#include "ap_announce.h"

// The course-select grid: 0-6 the courses, 7 the random button (the grid-to-course table
// at 0x805d51a8 maps 7 to 8), which needs at least one unlocked course.
#define TR_GRID_NUM 8

// A, Start, L and R rising edges, the launch press TopRide_CourseSelectThink tests.
#define TR_LAUNCH_BUTTONS (PAD_BUTTON_A | PAD_BUTTON_START | PAD_TRIGGER_L | PAD_TRIGGER_R)
static int GateTopRideStages_IsGridPosSelectable(int pos)
{
    if (pos >= TOPRIDE_NUM)
        return ap_save->topride_stage_unlocked_mask != 0;
    return (ap_save->topride_stage_unlocked_mask & (1 << pos)) ? 1 : 0;
}

static void GateTopRideStages_AdjustCursorToUnlocked(void)
{
    u8 *cursor_ptr = &Gm_GetGameData()->topride_course_select.cursor;
    int pos = *cursor_ptr;

    if (GateTopRideStages_IsGridPosSelectable(pos))
        return;

    for (int i = 1; i < TR_GRID_NUM; i++)
    {
        int next = (pos + i) % TR_GRID_NUM;
        if (GateTopRideStages_IsGridPosSelectable(next))
        {
            *cursor_ptr = (u8)next;
            return;
        }
    }
}

// It runs every frame, so feedback waits for a launch press. Returns 1 to block the launch.
static int GateTopRideStages_CourseSelectCanLaunch(u32 launch_buttons)
{
    if (!(launch_buttons & TR_LAUNCH_BUTTONS))
        return 0;

    int cursor = Gm_GetGameData()->topride_course_select.cursor;
    if (GateTopRideStages_IsGridPosSelectable(cursor))
        return 0;

    playSoundFX_errorNoise();
    if (cursor < TOPRIDE_NUM)
        tb_api->EnqueueColoredNoun("Unlock the ", TopRideCourse_Names[cursor], tb_api->StageColor, " course to play!");
    else
        tb_api->EnqueueColoredNoun("Unlock a ", "Top Ride course", tb_api->StageColor, " to play!");
    return 1;
}

// Hook at 0x8003ca78 (andi. r0, r7, 0x1160) in TopRide_CourseSelectThink (0x8003c8bc).
// Blocking goes to the D-pad handler at 0x8003cc18, which reads r5 (direction bits);
// allowing re-runs the andi on r7. Both are caller-saved, so both are stashed.
CODEPATCH_HOOKCONDITIONALCREATE(
    0x8003ca78,
    "stwu 1, -16(1)\n\t"
    "stw 7, 0x8(1)\n\t"
    "stw 5, 0xc(1)\n\t"
    "mr 3, 7\n\t",
    GateTopRideStages_CourseSelectCanLaunch,
    "lwz 7, 0x8(1)\n\t"
    "lwz 5, 0xc(1)\n\t"
    "addi 1, 1, 16\n\t",
    0,
    0x8003cc18
);

// Hook at 0x8003cd18 (lbz r0, 0x2(r31)) in TopRide_CourseSelectThink, where every D-pad
// path meets after writing the cursor, so the re-run lbz highlights the corrected one.
CODEPATCH_HOOKCREATE(
    0x8003cd18,
    "",
    GateTopRideStages_AdjustCursorToUnlocked,
    "",
    0
);

// Replaces the vanilla HSD_Randi(7) course picks, which consult only the used history. The
// pick is always unused, so the vanilla re-check after the call never re-rolls.
static int GateTopRideStages_RandomPick(int unused)
{
    (void)unused;
    u16 *used_ptr = &Gm_GetGameData()->topride_course_select.used_history_mask;
    u16 used = *used_ptr;
    u16 unlock = ap_save->topride_stage_unlocked_mask & ((1 << TOPRIDE_NUM) - 1);

    int pick = RandomBitInField(unlock & ~used);

    // Every unlocked course is used - restart the cycle.
    if (pick < 0)
    {
        *used_ptr = used & ~unlock;
        pick = RandomBitInField(unlock);
    }

    // Nothing unlocked. The caller re-rolls until the pick's used bit is clear, so course 0
    // is left selectable or it spins forever.
    if (pick < 0)
    {
        *used_ptr &= ~1;
        return 0;
    }

    OSReport("[GateTopRideStages] Picked %d (%s) (unlocked = %s, used = %s)\n",
             pick, TopRideCourse_Names[pick],
             MaskBits(unlock, TOPRIDE_NUM), MaskBits(used, TOPRIDE_NUM));
    return pick;
}

void GateTopRideStages_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x8003ca78);
    CODEPATCH_HOOKAPPLY(0x8003cd18);

    CODEPATCH_REPLACECALL(0x8003c798, GateTopRideStages_RandomPick); // in TopRide_CourseSelectRandomInit (0x8003c754)
    CODEPATCH_REPLACECALL(0x8003cac0, GateTopRideStages_RandomPick); // A on the random button, in TopRide_CourseSelectThink

    OSReport("[GateTopRideStages] Hooks installed\n");
}

int GateTopRideStages_UnlockStage(int course)
{
    if (course < 0 || course >= TOPRIDE_NUM)
        return 0;

    ap_save->topride_stage_unlocked_mask |= (1 << course);
    OSReport("[GateTopRideStages] Top Ride course %d (%s) unlocked (mask = %s)\n",
             course, TopRideCourse_Names[course], MaskBits(ap_save->topride_stage_unlocked_mask, TOPRIDE_NUM));
    APAnnounce_Grant("Unlocked Course: ", TopRideCourse_Names[course], tb_api->StageColor, NULL);
    return 1;
}

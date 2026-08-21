#include "game.h"
#include "os.h"
#include "audio.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_topride_stages.h"
#include "textbox_api.h"
#include "inline.h"

// Callers must pre-check the random-button case (course >= TOPRIDE_NUM).
static int GateTopRideStages_CheckCourseUnlocked(int course)
{
    return (ap_save->topride_stage_unlocked_mask & (1 << course)) ? 1 : 0;
}

// The course-select grid has 8 positions: 0-6 = courses, 7 = the random button (the
// grid-to-course table at 0x805d51a8 is identity for 0-6 and maps 7 to value 8). The
// random button needs at least one unlocked course.
static int IsGridPosSelectable(int pos)
{
    if (pos >= TOPRIDE_NUM)
        return ap_save->topride_stage_unlocked_mask != 0;
    return GateTopRideStages_CheckCourseUnlocked(pos);
}

static void AdjustCursorToUnlocked(void)
{
    if (!ap_save)
        return;
    u8 *cursor_ptr = &Gm_GetGameData()->topride_course_select.cursor;
    int pos = *cursor_ptr;

    if (IsGridPosSelectable(pos))
        return;

    for (int i = 1; i <= 8; i++)
    {
        int next = (pos + i) % 8;
        if (IsGridPosSelectable(next))
        {
            *cursor_ptr = (u8)next;
            return;
        }
    }
}

// Launch gate at 0x8003ca78 (`andi. r0, r7, 0x1160`) in TopRide_CourseSelectThink
// (0x8003c8bc, minor scene 7). Runs every frame, so it gates feedback on an actual
// launch press (0x1160 = the A/Start rising-edge mask) instead of buzzing every frame
// the cursor sits on a locked course. The cursor can only rest on a locked course when
// ALL courses are locked, since the movement hook then has nowhere selectable to move it.
//   no launch press, or unlocked course -> 0 (re-run andi, vanilla path)
//   locked course                       -> buzzer + textbox, 1 (skip launch)
static int GateTopRideStages_CourseSelectCanLaunch(u32 launch_buttons)
{
    if (!ap_save)
        return 1;

    if (!(launch_buttons & 0x1160))
        return 0;

    int cursor = Gm_GetGameData()->topride_course_select.cursor;
    if (IsGridPosSelectable(cursor))
        return 0;

    playSoundFX_errorNoise();
    if (cursor < TOPRIDE_NUM)
        tb_api->EnqueueColoredNoun("Unlock the ", TopRideCourse_Names[cursor], tb_api->StageColor, " course to play!");
    else
        tb_api->EnqueueColoredNoun("Unlock a ", "Top Ride course", tb_api->StageColor, " to play!");
    return 1;
}

// The block path (r3 != 0) branches to the D-pad handler at 0x8003cc18, which
// reads caller-saved r5 (direction bits); the allow path re-runs the clobbered
// `andi. r0, r7, 0x1160`, so r7 must survive too. The trampoline saves neither,
// so both are stashed on a scratch frame here.
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

// Cursor-movement convergence at 0x8003cd18 (`lbz r0, 0x2(r31)`), where all D-pad paths
// meet after writing topride_course_select.cursor. Adjusting the cursor before the
// clobbered lbz reads it makes the visual update highlight the corrected position.
static void GateTopRideStages_SkipLockedCursor(void)
{
    AdjustCursorToUnlocked();
}

CODEPATCH_HOOKCREATE(
    0x8003cd18,
    "",
    GateTopRideStages_SkipLockedCursor,
    "",
    0
);

// Replaces the vanilla HSD_Randi(7) course picks, which consult only the used-history
// bitmask. The returned pick is guaranteed unused, so the vanilla re-check after the
// call never re-rolls.
static int GateTopRideStages_RandomPick(int unused)
{
    (void)unused;
    if (!ap_save)
        return 0;
    u16 *used_ptr = &Gm_GetGameData()->topride_course_select.used_history_mask;
    u16 used = *used_ptr;
    u16 unlock = ap_save->topride_stage_unlocked_mask & 0x7F;

    int candidates[TOPRIDE_NUM];
    int count = 0;
    for (int i = 0; i < TOPRIDE_NUM; i++)
    {
        if ((unlock & (1 << i)) && !(used & (1 << i)))
            candidates[count++] = i;
    }

    // Every unlocked course is used - restart the cycle.
    if (count == 0)
    {
        *used_ptr = used & ~unlock;
        for (int i = 0; i < TOPRIDE_NUM; i++)
        {
            if (unlock & (1 << i))
                candidates[count++] = i;
        }
    }

    if (count == 0)
        return 0;

    int pick = candidates[HSD_Randi(count)];
    OSReport("[GateTopRideStages] Random pick %d (%s) from %d candidates (unlocked = %s, used = %s)\n",
             pick, TopRideCourse_Names[pick], count, MaskBits(unlock, 8), MaskBits(used, 8));
    return pick;
}

void GateTopRideStages_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x8003ca78);  // course select launch gate
    CODEPATCH_HOOKAPPLY(0x8003cd18);  // course select cursor skip

    CODEPATCH_REPLACECALL(0x8003c798, GateTopRideStages_RandomPick); // TopRide_CourseSelectRandomInit
    CODEPATCH_REPLACECALL(0x8003cac0, GateTopRideStages_RandomPick); // A on the random button

    OSReport("[GateTopRideStages] Top Ride stage gating installed\n");
}

int GateTopRideStages_UnlockStage(int course)
{
    if (course < 0 || course >= TOPRIDE_NUM)
        return 0;

    ap_save->topride_stage_unlocked_mask |= (1 << course);
    OSReport("[GateTopRideStages] Top Ride course %d (%s) unlocked (mask = %s)\n",
             course, TopRideCourse_Names[course], MaskBits(ap_save->topride_stage_unlocked_mask, 8));
    tb_api->EnqueueColoredNoun("Unlocked Course: ", TopRideCourse_Names[course], tb_api->StageColor, NULL);
    return 1;
}

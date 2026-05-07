#include "game.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_topride_stages.h"
#include "textbox.h"
#include "textbox_colors.h"
#include "inline.h"

// Returns 1 if the course at index `course` is unlocked, 0 otherwise.
// Callers must pre-check the random-button case (course >= TOPRIDE_NUM).
static int GateTopRideStages_CheckCourseUnlocked(int course)
{
    return (ap_save->topride_stage_unlocked_mask & (1 << course)) ? 1 : 0;
}

// True if the given grid position is currently selectable: an unlocked course,
// or the random button when at least one course is unlocked.
static int IsGridPosSelectable(int pos)
{
    if (pos >= TOPRIDE_NUM)
        return ap_save->topride_stage_unlocked_mask != 0;
    return GateTopRideStages_CheckCourseUnlocked(pos);
}

// Adjusts the cursor on the Top Ride CSS to skip locked courses.
// Scans forward (wrapping 0-7) until a selectable position is found.
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

// Course Select Screen (minor scene 7, TopRide_CourseSelectThink @ 0x8003c8bc)
// Grid has 8 positions: 0-6 = courses, 7 = random button.
// Grid-to-course table at 0x805d51a8: identity for 0-6, position 7 = value 8.

// Hook 1: A-button launch (0x8003ca78: andi. r0, r7, 0x1160).
// Returns 0 = allow (unlocked or random with candidates), 1 = block (locked).
static int GateTopRideStages_CourseSelectCanLaunch(void)
{
    if (!ap_save)
        return 1;
    return IsGridPosSelectable(Gm_GetGameData()->topride_course_select.cursor) ? 0 : 1;
}

CODEPATCH_HOOKCONDITIONALCREATE(
    0x8003ca78,                         // dol_addr: andi. r0, r7, 0x1160 (A-button test)
    "stwu 1, -16(1)\n\t"               // prologue: create mini stack frame
    "stw 7, 0x8(1)\n\t",               //           save r7 (combined pad data)
    GateTopRideStages_CourseSelectCanLaunch,
    "lwz 7, 0x8(1)\n\t"                // epilogue: restore r7 for clobbered andi.
    "addi 1, 1, 16\n\t",               //           pop mini stack frame
    0,                                  // exit_addr: normal exit (run clobbered andi.)
    0x8003cc18                          // exit_addr_alt: skip to cursor movement (locked)
);

// Hook 2: Cursor movement convergence (0x8003cd18: lbz r0, 0x2(r31)).
// All D-pad movement paths write to topride_course_select.cursor then
// converge here. We adjust the cursor before the game reads it, so locked
// positions are skipped and the visual update highlights the corrected position.
static void GateTopRideStages_SkipLockedCursor(void)
{
    AdjustCursorToUnlocked();
}

CODEPATCH_HOOKCREATE(
    0x8003cd18,                         // dol_addr: lbz r0, 0x2(r31) (read new cursor)
    "",                                 // prologue: none needed
    GateTopRideStages_SkipLockedCursor,
    "",                                 // epilogue: none needed
    0                                   // exit_addr: normal exit (run clobbered lbz)
);

// Replaces the HSD_Randi(7) call at 0x8003c798 in TopRide_CourseSelectRandomInit.
// The vanilla random loop picks from all 7 courses and only checks a "used" history
// bitmask (topride_course_select.used_history_mask), with no unlock check. Our replacement picks only from
// courses that are both unlocked AND not in the used history. If all unlocked courses
// are used, it resets the used bits for unlocked courses so the cycle can restart.
// The vanilla loop at 0x8003c7a0 re-checks the used mask after we return — since we
// already ensured the pick isn't used, it won't re-roll.
static int GateTopRideStages_RandomPick(int unused)
{
    (void)unused;
    if (!ap_save)
        return 0;
    u16 *used_ptr = &Gm_GetGameData()->topride_course_select.used_history_mask;
    u16 used = *used_ptr;
    u16 unlock = ap_save->topride_stage_unlocked_mask & 0x7F;

    // Build candidates: unlocked AND not recently used
    int candidates[TOPRIDE_NUM];
    int count = 0;
    for (int i = 0; i < TOPRIDE_NUM; i++)
    {
        if ((unlock & (1 << i)) && !(used & (1 << i)))
            candidates[count++] = i;
    }

    // All unlocked courses are used — reset used bits for unlocked courses
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
    OSReport("[TopRideStages] RandomPick: unlock=%s, used=%s, candidates=%d, pick=%d\n",
             MaskBits(unlock, 8), MaskBits(used, 8), count, pick);
    return pick;
}

void GateTopRideStages_OnBoot()
{
    // Course select screen: block A-button when course is locked.
    CODEPATCH_HOOKAPPLY(0x8003ca78);

    // Course select screen: skip locked courses during cursor movement.
    CODEPATCH_HOOKAPPLY(0x8003cd18);

    // Random selection: replace HSD_Randi(7) calls with our function that
    // only picks from unlocked courses and handles used-history properly.
    // There are two call sites:
    // 1) TopRide_CourseSelectRandomInit (0x8003c798): called on scene init
    //    when random was previously selected.
    // 2) TopRide_CourseSelectThink (0x8003cac0): called inline when A is
    //    pressed on the random button — this is the main random path.
    CODEPATCH_REPLACECALL(0x8003c798, GateTopRideStages_RandomPick);
    CODEPATCH_REPLACECALL(0x8003cac0, GateTopRideStages_RandomPick);

    OSReport("[TopRideStages] Top Ride stage gating installed\n");
}

int GateTopRideStages_UnlockStage(int course)
{
    if (course < 0 || course >= TOPRIDE_NUM)
        return 0;

    ap_save->topride_stage_unlocked_mask |= (1 << course);
    OSReport("[TopRideStages] Top Ride course %d (%s) unlocked (mask = %s)\n",
             course, TopRideCourse_Names[course], MaskBits(ap_save->topride_stage_unlocked_mask, 8));
    TextBox_EnqueueColoredNoun(NULL, TopRideCourse_Names[course], TextBox_StageColor, NULL);
    return 1;
}

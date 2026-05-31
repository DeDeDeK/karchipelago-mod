#include "game.h"
#include "os.h"
#include "audio.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_topride_stages.h"
#include "textbox_api.h"
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
// Returns 0 = allow (run the vanilla launch test), 1 = block (locked + pressed).
//
// IMPORTANT: this hook sits on the per-frame input-dispatch instruction, so it
// runs EVERY frame — not only when A is pressed. The cursor can only ever rest
// on a locked course when ALL courses are locked (the skip-cursor in Hook 2 has
// nowhere selectable to move to, so it leaves the cursor put). In that state we
// still want the player to be able to hover the locked course; pressing A just
// shouldn't launch — and should explain why.
//
// `launch_buttons` (passed in from r7 by the prologue) is the combined launch
// button word; 0x1160 is the same A/Start rising-edge mask the clobbered
// `andi. r0, r7, 0x1160` tests. Gating on it is essential: it both ties the
// feedback to an actual press (the buzzer/textbox would otherwise retrigger
// every frame the cursor sits on a locked course, never producing a clean
// notification) and lets every non-launch frame fall through to the vanilla
// andi → D-pad handler unchanged.
//   no launch press        -> return 0 (re-run andi; falls to D-pad)
//   press, course unlocked  -> return 0 (re-run andi; vanilla launch)
//   press, course locked    -> buzzer + textbox, return 1 (skip launch)
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

// Register preservation: the block path (r3 != 0) branches to 0x8003cc18, the
// D-pad movement handler, which reads `r5 & 0x3000C` at 0x8003cc24 — r5 holds
// the combined controller direction bits and is caller-saved. The allow path
// re-executes the clobbered `andi. r0, r7, 0x1160`, so r7 must survive too.
// The hoshi codepatch trampoline saves no registers around its `bl`, so both
// volatiles are stashed on a scratch frame here. The final `mr 3, 7` forwards
// r7 to the C function as `launch_buttons` (r3 = first arg) without disturbing
// the saved copy.
CODEPATCH_HOOKCONDITIONALCREATE(
    0x8003ca78,                         // dol_addr: andi. r0, r7, 0x1160 (A-button test)
    "stwu 1, -16(1)\n\t"               // prologue: create mini stack frame
    "stw 7, 0x8(1)\n\t"                //           save r7 (combined pad data, for clobbered andi.)
    "stw 5, 0xc(1)\n\t"                //           save r5 (direction bits, for block-path d-pad handler)
    "mr 3, 7\n\t",                     //           pass r7 (launch buttons) as the C arg
    GateTopRideStages_CourseSelectCanLaunch,
    "lwz 7, 0x8(1)\n\t"                // epilogue: restore r7 for clobbered andi.
    "lwz 5, 0xc(1)\n\t"                //           restore r5 for 0x8003cc18
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
    tb_api->EnqueueColoredNoun("Unlocked Course: ", TopRideCourse_Names[course], tb_api->StageColor, NULL);
    return 1;
}

#include "game.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_topride_stages.h"
#include "textbox.h"

static const char *topride_stage_names[TOPRIDE_NUM] = {
    [TOPRIDE_GRASS]  = "Grass",
    [TOPRIDE_SAND]   = "Sand",
    [TOPRIDE_SKY]    = "Sky",
    [TOPRIDE_FIRE]   = "Fire",
    [TOPRIDE_LIGHT]  = "Light",
    [TOPRIDE_WATER]  = "Water",
    [TOPRIDE_METAL]  = "Metal",
};

// Returns 1 if unlocked, 0 if locked.
static int GateTopRideStages_CheckCourseUnlocked(int course)
{
    if (course < 0 || course >= TOPRIDE_NUM)
        return 1;
    return (save_data->topride_stage_unlocked_mask & (1 << course)) ? 1 : 0;
}

// Adjusts the cursor at GameData[0xf8] to skip locked courses.
// Scans forward (wrapping 0-7) until an unlocked course or the random
// button (position 7) is found.
static void AdjustCursorToUnlocked(void)
{
    u8 *cursor_ptr = &((u8 *)Gm_GetGameData())[0xf8];
    int pos = *cursor_ptr;

    // Random button (pos 7+) is always valid
    if (pos >= TOPRIDE_NUM)
        return;

    // Already on an unlocked course
    if (GateTopRideStages_CheckCourseUnlocked(pos))
        return;

    // Scan forward, wrapping through all 8 positions
    for (int i = 1; i <= 8; i++)
    {
        int next = (pos + i) % 8;
        if (next >= TOPRIDE_NUM || GateTopRideStages_CheckCourseUnlocked(next))
        {
            *cursor_ptr = (u8)next;
            return;
        }
    }
    // Fallback (all courses locked) — land on random button
    *cursor_ptr = 7;
}

// --- Course Select Screen (minor scene 7, zz_8003c8bc_) ---
// Grid has 8 positions: 0-6 = courses, 7 = random button.
// Grid-to-course table at 0x805d51a8: identity for 0-6, position 7 = value 8.

// Hook 1: A-button launch (0x8003ca78: andi. r0, r7, 0x1160).
// Returns 0 = allow (unlocked or random), 1 = block (locked).
int GateTopRideStages_CourseSelectCanLaunch(void)
{
    int cursor_pos = ((u8 *)Gm_GetGameData())[0xf8];
    if (cursor_pos >= TOPRIDE_NUM)
        return 0;
    return GateTopRideStages_CheckCourseUnlocked(cursor_pos) ? 0 : 1;
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
// All D-pad movement paths write to GameData[0xf8] then converge here.
// We adjust the cursor before the game reads it, so locked positions are
// skipped and the visual update highlights the corrected position.
void GateTopRideStages_SkipLockedCursor(void)
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

void GateTopRideStages_OnBoot()
{
    // Course select screen: block A-button when course is locked.
    CODEPATCH_HOOKAPPLY(0x8003ca78);

    // Course select screen: skip locked courses during cursor movement.
    CODEPATCH_HOOKAPPLY(0x8003cd18);

    OSReport("Top Ride stage gating installed\n");
}

int GateTopRideStages_UnlockStage(int course)
{
    if (course < 0 || course >= TOPRIDE_NUM)
        return 0;

    save_data->topride_stage_unlocked_mask |= (1 << course);
    OSReport("Top Ride course %d (%s) unlocked (mask = 0x%04x)\n",
             course, topride_stage_names[course], save_data->topride_stage_unlocked_mask);
    TextBox_Enqueue(topride_stage_names[course]);
    return 1;
}

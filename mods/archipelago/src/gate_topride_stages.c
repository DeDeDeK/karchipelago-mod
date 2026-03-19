#include "game.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_topride_stages.h"
#include "textbox.h"

// WIP — hooks disabled pending audio heap crash on Top Ride re-entry.
// The crash is pre-existing (reproduces without these patches) and unrelated to our hooks.

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

// REPLACECALL target for TopRide_PreGameThink (0x8002c628).
// Vanilla passes (mode=1, clear_kind) — we ignore both, check AP mask directly.
int GateTopRideStages_CheckCourse_PreGame(int mode, int clear_kind)
{
    int course = TopRide_GetSelectedCourse();
    return GateTopRideStages_CheckCourseUnlocked(course);
}

// HOOKCONDITIONALCREATE target for TopRide_OnCourseSelect (0x8002cc78).
// Returns 0 = allow (unlocked), 1 = block (locked, skip to browsing).
int GateTopRideStages_CanLaunch(void)
{
    int course = TopRide_GetSelectedCourse();
    return GateTopRideStages_CheckCourseUnlocked(course) ? 0 : 1;
}

CODEPATCH_HOOKCONDITIONALCREATE(
    0x8002cc78,                         // dol_addr: rlwinm. r0, r0, 0, 19, 19 (START button test)
    "stwu 1, -16(1)\n\t"               // prologue: create mini stack frame (16 bytes)
    "stw 0, 0x8(1)\n\t",               //           save r0 (pad trigger data) into our frame
    GateTopRideStages_CanLaunch,        // func: returns 0=allow, 1=block
    "lwz 0, 0x8(1)\n\t"                // epilogue: restore r0 for clobbered rlwinm.
    "addi 1, 1, 16\n\t",               //           pop mini stack frame
    0,                                  // exit_addr: normal exit (run clobbered instruction)
    0x8002cddc                          // exit_addr_alt: skip to browsing path (locked)
);

void GateTopRideStages_OnBoot()
{
    // WIP: Top Ride stage gating — disabled pending audio heap crash on Top Ride re-entry.
    // The crash is pre-existing (reproduces without these patches) and unrelated to our hooks.
    // Call site 1: character-select launch path (0x8002c628)
    // CODEPATCH_REPLACECALL(0x8002c628, GateTopRideStages_CheckCourse_PreGame);
    // Call site 2: course-select launch path (0x8002cc78)
    // CODEPATCH_HOOKAPPLY(0x8002cc78);

    OSReport("Top Ride stage gating: WIP (hooks disabled)\n");
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

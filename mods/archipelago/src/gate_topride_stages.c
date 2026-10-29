#include "game.h"
#include "os.h"
#include "audio.h"
#include "scene.h"
#include "menu.h"
#include "code_patch/code_patch.h"
#include "hoshi/func.h"

#include "main.h"
#include "gate_topride_stages.h"
#include "textbox_api.h"
#include "inline.h"
#include "ap_announce.h"

// A, Start, L and R rising edges, the launch press TopRide_CourseSelectThink tests.
#define TR_LAUNCH_BUTTONS (PAD_BUTTON_A | PAD_BUTTON_START | PAD_TRIGGER_L | PAD_TRIGGER_R)

static void (*course_select_load_vanilla)();
static void (*course_select_exit_vanilla)(void *data);
static void (*course_select_think_vanilla)();

// The random button needs at least one unlocked course.
static int GateTopRideStages_IsGridPosSelectable(int pos)
{
    if (pos >= TOPRIDE_NUM)
        return ap_save->topride_stage_unlocked_mask != 0;
    return (ap_save->topride_stage_unlocked_mask & (1 << pos)) ? 1 : 0;
}

static s8 GateTopRideStages_VanillaGridValue(int pos)
{
    return pos < TOPRIDE_NUM ? pos : TOPRIDE_GRID_RANDOM;
}

static s8 GateTopRideStages_GridValue(int pos)
{
    if (!GateTopRideStages_IsGridPosSelectable(pos))
        return TOPRIDE_GRID_LOCKED;
    return GateTopRideStages_VanillaGridValue(pos);
}

// Minor 7 cb_Load wrapper. Everything the scene shows for a position comes from the grid
// table, so a locked one gets the lock icon, a blank panorama and the random button's
// laps and best time.
static void GateTopRideStages_CourseSelectLoad(void)
{
    for (int pos = 0; pos < TOPRIDE_GRID_NUM; pos++)
        stc_topride_course_grid[pos] = GateTopRideStages_GridValue(pos);

    course_select_load_vanilla();
}

// Minor 7 cb_Exit wrapper. With course selection Off, TopRide_CourseSelectRandomInit picks
// the course in place of this scene and maps the pick through the table, where a course
// unlocked since would still read locked.
static void GateTopRideStages_CourseSelectExit(void *data)
{
    course_select_exit_vanilla(data);

    for (int pos = 0; pos < TOPRIDE_GRID_NUM; pos++)
        stc_topride_course_grid[pos] = GateTopRideStages_VanillaGridValue(pos);
}

// Minor 7 cb_ThinkPreGObjProc wrapper. Applies unlocks that arrive while the screen is up,
// before TopRide_CourseSelectThink reads the table for a launch.
static void GateTopRideStages_CourseSelectThink(void)
{
    GameData *gd = Gm_GetGameData();
    ScMenuCommon *menu = Gm_GetMenuData();

    for (int pos = 0; pos < TOPRIDE_GRID_NUM; pos++)
    {
        s8 value = GateTopRideStages_GridValue(pos);
        if (stc_topride_course_grid[pos] == value)
            continue;

        stc_topride_course_grid[pos] = value;
        if (pos < TOPRIDE_NUM)
            gd->topride_course_select.laps[pos] = value < TOPRIDE_NUM ? TopRide_GetCourseDefaultLaps(value) : -1;

        GOBJ *icon = menu->main.topride_course_select.icon_gobj[pos];
        if (icon != 0)
            MainMenu_SetTexAnimFrame(icon->hsd_object, value, 0.0f);
        if (pos == gd->topride_course_select.cursor)
            TopRide_CourseSelectSetPanorama(value);
    }

    course_select_think_vanilla();
}

// It runs every frame, so the buzzer waits for a launch press. Returns 1 to block the launch.
static int GateTopRideStages_CourseSelectCanLaunch(u32 launch_buttons)
{
    if (!(launch_buttons & TR_LAUNCH_BUTTONS))
        return 0;

    if (GateTopRideStages_IsGridPosSelectable(Gm_GetGameData()->topride_course_select.cursor))
        return 0;

    playSoundFX_errorNoise();
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
    MinorSceneDesc *minor_descs = Hoshi_GetMinorScenes();
    MinorSceneDesc *desc = &minor_descs[MNRKIND_TOPRIDECOURSESELECT];

    course_select_load_vanilla = desc->cb_Load;
    desc->cb_Load = GateTopRideStages_CourseSelectLoad;
    course_select_exit_vanilla = desc->cb_Exit;
    desc->cb_Exit = GateTopRideStages_CourseSelectExit;
    course_select_think_vanilla = desc->cb_ThinkPreGObjProc;
    desc->cb_ThinkPreGObjProc = GateTopRideStages_CourseSelectThink;

    CODEPATCH_HOOKAPPLY(0x8003ca78);

    // The scene tests a grid value with cmpwi 8 / bne before indexing per-course data with
    // it; blt sends TOPRIDE_GRID_LOCKED down the random button's path as well.
    CODEPATCH_REPLACEINSTRUCTION(0x8003d150, 0x41800010); // blt, laps in TopRide_CourseSelectInit
    CODEPATCH_REPLACEINSTRUCTION(0x8003d20c, 0x4180000c); // blt, Time Attack best time at load
    CODEPATCH_REPLACEINSTRUCTION(0x8003d354, 0x4180000c); // blt, Free Run best time at load
    CODEPATCH_REPLACEINSTRUCTION(0x8003cdbc, 0x4180000c); // blt, Time Attack best time in TopRide_CourseSelectThink
    CODEPATCH_REPLACEINSTRUCTION(0x8003cf04, 0x4180000c); // blt, Free Run best time in TopRide_CourseSelectThink

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

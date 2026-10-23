#include "game.h"
#include "obj.h"
#include "os.h"
#include "scene.h"
#include "code_patch/code_patch.h"
#include "hoshi/func.h"

#include "custom_checklist.h"

#define CC_IS_VANILLA_TAB(minor) ((minor) >= MNRKIND_AIRRIDECHECKLIST && (minor) <= MNRKIND_CITYCHECKLIST)

// Tab being built by CC_MinorLoad (-1 otherwise). While set, the clear-data accessor
// redirects CITYTRIAL to that tab's block so Checklist_Init populates from it.
static int build_tab = -1;

// Raised while the checklist session was entered from a run, not menu navigation.
static int postrun = 0;

int CCScene_FindTab(int minor)
{
    for (int i = 0; i < cc_tab_num; i++)
        if (cc_tabs[i].minor_id == minor)
            return i;
    return -1;
}

int CCScene_GetBuildMode(void)
{
    return build_tab >= 0 ? CC_TAB_MODE(build_tab) : -1;
}

// First tab other than exclude_minor's with an unviewed unlock, or -1.
static int CC_FirstPending(int exclude_minor)
{
    for (int i = 0; i < cc_tab_num; i++)
        if (cc_tabs[i].minor_id != exclude_minor && CCBoard_HasPendingUnlock(cc_tabs[i].clear))
            return i;
    return -1;
}

// REPLACEFUNC for gmGetClearcheckerTypeP (0x800076a0). Unknown modes return NULL
// instead of tripping vanilla's mode >= 3 assert.
static GameClearData *CC_GetClearcheckerTypeP(GameMode mode)
{
    GameData *gd = Gm_GetGameData();
    switch (mode)
    {
    case GMMODE_AIRRIDE:   return &gd->airride_clear.clear;
    case GMMODE_TOPRIDE:   return &gd->topride_clear.clear;
    case GMMODE_CITYTRIAL: return build_tab >= 0 ? cc_tabs[build_tab].clear : &gd->city_clear.clear;
    default:
    {
        int idx = (int)mode - GMMODE_NUM;
        return idx >= 0 && idx < cc_tab_num ? cc_tabs[idx].clear : NULL;
    }
    }
}

// REPLACEFUNC for Checklist_GetRewardNum (0x80049c20): 0 for custom tabs gates the reward
// loops off and dodges the vanilla mode >= 3 assert. A build reads CITYTRIAL, so answer 0
// there too or Checklist_SetRewardFlagOnUnlocks sets has_reward on the tab's cells.
static u8 CC_GetRewardNum(GameMode mode)
{
    if (build_tab >= 0 && mode == GMMODE_CITYTRIAL)
        return 0;
    return (unsigned)mode < GMMODE_NUM ? stc_reward_num[mode] : 0;
}

// REPLACEFUNC for Checklist_GetClearKindFromRewardIndex (0x80049c84): 0 for custom tabs
// keeps Checklist_ProcessUnlock's new-unlock scan inert so the cell animation can run.
static u8 CC_GetClearKindFromRewardIndex(GameMode mode, u8 reward_index)
{
    if ((unsigned)mode >= GMMODE_NUM)
        return 0;
    return stc_reward_table_ptrs[mode][reward_index].clear_kind;
}

// REPLACECALL at Checklist_Think's only call of ClearChecker_GetRewardFromClearKind
// (0x80049ec4), which bounds the mode itself and asserts on a custom tab's completed cell.
// Patched at the call site so another mod can own the entry. Vanilla leaves
// out_reward_param alone on a miss.
static void CC_GetRewardFromClearKind(GameMode mode, u8 clear_kind,
                                      u8 *out_reward_index, u8 *out_reward_param)
{
    if ((unsigned)mode >= GMMODE_NUM)
    {
        *out_reward_index = 0xFF;
        return;
    }
    ClearChecker_GetRewardFromClearKind(mode, clear_kind, out_reward_index, out_reward_param);
}

// REPLACECALL at the four JObj_AddSetAnim calls in Checklist_UpdateCellInfo (0x80181d70)
// that pick the Prize1 animation as ClearCheckerUI.mode * 2, +1 with a reward shown. The
// Prize1 set holds only the three real modes' pairs, so a custom tab plays City Trial's.
static void CC_AddSetPrizeAnim(JOBJ *jobj, int anim_id, JOBJSet *set, float frame, float rate)
{
    if (anim_id >= GMMODE_NUM * 2)
        anim_id = GMMODE_CITYTRIAL * 2 + (anim_id & 1);
    JObj_AddSetAnim(jobj, anim_id, set, frame, rate);
}

// REPLACEFUNC for ClearChecker_CheckForNewUnlocks (0x8004a1a4), the gate each mode's
// *_MinorExit consults. OR-ing in the custom tabs routes a run that completed only a
// custom check into the checklist.
static int CC_CheckForNewUnlocks(GameMode mode)
{
    if (Net_IsSessionActive())
        return 0;
    return CCBoard_HasPendingUnlock(gmGetClearcheckerTypeP(mode)) || CC_FirstPending(-1) >= 0;
}

static int CC_IsChecklistMinor(int minor)
{
    return CC_IS_VANILLA_TAB(minor) || CCScene_FindTab(minor) >= 0;
}

// REPLACEFUNC for Scene_SetNextMinor (0x800088c8); vanilla only stores the id to
// GameData.minor_next. A run's exit into the checklist retargets to a pending custom tab
// when the played mode has nothing to animate. CC_MinorThink's tab steps re-enter here too,
// so a step from a checklist tab is left alone, or one onto a vanilla tab would bounce back
// to the pending custom one.
static void CC_SetNextMinor(int minor)
{
    if (cc_tab_num > 0 && CC_IS_VANILLA_TAB(minor) &&
        Scene_GetCurrentMajor() != MJRKIND_MENU &&
        !CC_IsChecklistMinor(Scene_GetCurrentMinor()))
    {
        postrun = 1;
        GameMode mode = (GameMode)(minor - MNRKIND_AIRRIDECHECKLIST);
        if (!CCBoard_HasPendingUnlock(gmGetClearcheckerTypeP(mode)))
        {
            int idx = CC_FirstPending(-1);
            if (idx >= 0)
                minor = cc_tabs[idx].minor_id;
        }
    }
    Gm_GetGameData()->minor_next = (MinorKind)minor;
}

// cb_Load for every custom tab, in place of Checklist_MinorLoad (0x8004a768). Checklist_Init
// runs as City Trial - a valid mode, so no assert and no archetype-slot collision - while
// the accessor serves the tab's block.
static void CC_MinorLoad(void)
{
    int idx = CCScene_FindTab(Scene_GetCurrentMinor());

    Checklist_PrepMenuData();

    // fresh_flag 1 opens on the new-unlock presentation; a custom tab opens on it whenever
    // it holds an unviewed unlock, however it is reached.
    build_tab = idx;
    Checklist_Init(GMMODE_CITYTRIAL, CCBoard_HasPendingUnlock(cc_tabs[idx].clear));
    build_tab = -1;

    CCLabels_Apply(idx);

    // After the build, so its setup can't reset the per-scene heap under the load.
    CCArt_Load(idx);

    ClearCheckerUI *ui = Gm_GetMenuData()->clearchecker.bg_gobj->userdata;
    ui->mode = (GameMode)CC_TAB_MODE(idx);

    if (Scene_GetCurrentMajor() == MJRKIND_MENU)
        loadMainMenuMusic();
}

// Step along AR, TR, CT, then the custom tabs in registry order, with wrap.
static int CC_RingStep(int minor, int dir)
{
    int ring[3 + CC_TAB_MAX];
    int n = 0;
    ring[n++] = MNRKIND_AIRRIDECHECKLIST;
    ring[n++] = MNRKIND_TOPRIDECHECKLIST;
    ring[n++] = MNRKIND_CITYCHECKLIST;
    for (int i = 0; i < cc_tab_num; i++)
        ring[n++] = cc_tabs[i].minor_id;

    for (int i = 0; i < n; i++)
        if (ring[i] == minor)
            return ring[(i + dir + n) % n];
    return minor;
}

// REPLACEFUNC for Checklist_MinorThink (0x8004a648), shared by every checklist tab. With
// no tabs registered the ring is just AR/TR/CT and this matches vanilla.
static void CC_MinorThink(void)
{
    ClearCheckerPhase phase = Gm_GetClearCheckerPhase();
    int minor = Scene_GetCurrentMinor();

    switch (phase)
    {
    case CLEARCHECKER_PHASE_EXIT:
        // Post-run, detour to a tab with an unviewed unlock so it animates before leaving;
        // it raises is_unlocked once shown, so the next exit press falls through.
        if (postrun)
        {
            int idx = CC_FirstPending(minor);
            if (idx >= 0)
            {
                Scene_SetNextMinor(cc_tabs[idx].minor_id);
                Scene_ExitMinor();
                break;
            }
        }
        postrun = 0;
        Scene_SetNextMinor(-1);
        Scene_ExitMinor();
        break;

    case CLEARCHECKER_PHASE_NEXTTAB:
    case CLEARCHECKER_PHASE_PREVTAB:
        SFX_PlayFullVolume(CLEARCHECKER_TAB_SFX);
        Scene_SetNextMinor(CC_RingStep(minor, phase == CLEARCHECKER_PHASE_NEXTTAB ? 1 : -1));
        Scene_ExitMinor();
        break;

    case CLEARCHECKER_PHASE_ENDING:
        // Only the three real tabs have an ending; CC_GetRewardFromClearKind finds no
        // reward on a custom tab's cells, so none raises this phase.
        postrun = 0;
        MainMenu_ClearSoundTestSongThunk();
        Scene_SetNextMinor(MNRKIND_AIRRIDEENDING + (minor - MNRKIND_AIRRIDECHECKLIST));
        Scene_ExitMinor();
        break;

    default:
        break;
    }
}

// Clone the City Trial checklist descriptor with this cb_Load, and the art pass in its
// empty post-proc slot. hoshi appends past MNRKIND_NUM and cannot fail.
int CCScene_InstallMinor(void)
{
    MinorSceneDesc d = Hoshi_GetMinorScenes()[MNRKIND_CITYCHECKLIST];
    d.cb_Load = CC_MinorLoad;
    d.cb_ThinkPostGObjProc2 = CCArt_Apply;
    return Hoshi_InstallMinorScene(&d);
}

void CCScene_InstallHooks(void)
{
    CODEPATCH_REPLACEFUNC(gmGetClearcheckerTypeP, CC_GetClearcheckerTypeP);
    CODEPATCH_REPLACEFUNC(Checklist_GetRewardNum, CC_GetRewardNum);
    CODEPATCH_REPLACEFUNC(Checklist_GetClearKindFromRewardIndex, CC_GetClearKindFromRewardIndex);
    CODEPATCH_REPLACEFUNC(Checklist_MinorThink, CC_MinorThink);
    CODEPATCH_REPLACEFUNC(ClearChecker_CheckForNewUnlocks, CC_CheckForNewUnlocks);
    CODEPATCH_REPLACEFUNC(Scene_SetNextMinor, CC_SetNextMinor);
    CODEPATCH_REPLACECALL(0x801804dc, CC_GetRewardFromClearKind); // in Checklist_Think (0x8017f3bc)
    CODEPATCH_REPLACECALL(0x80181fb8, CC_AddSetPrizeAnim);        // in Checklist_UpdateCellInfo (0x80181d70)
    CODEPATCH_REPLACECALL(0x80181fe4, CC_AddSetPrizeAnim);
    CODEPATCH_REPLACECALL(0x80182070, CC_AddSetPrizeAnim);
    CODEPATCH_REPLACECALL(0x80182098, CC_AddSetPrizeAnim);
}

#include "game.h"
#include "hsd.h"
#include "os.h"
#include "stadium.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_stadiums.h"
#include "textbox_api.h"
#include "inline.h"

// gd->city.prev_stadium_kind[] is sized 5, but vanilla only uses 4 entries
// for history exclusion (offsets 1118-1121). We size our history to match.
#define STADIUM_HISTORY_SIZE 4

// Replacement for CityTrial_DecideStadium (0x8003f808).
// Fixes the vanilla history buffer issue where few unlocked stadiums
// cause the history to exclude all candidates, leading to HSD_Randi(0).
static void GateStadiums_DecideStadium()
{
    GameData *gd = Gm_GetGameData();
    gmDataAll *gda = *stc_gmdataall;
    u32 mask = ap_save->stadium_unlocked_mask;
    u8 menu_selection = gd->city.menu_stadium_selection;

    // Count unlocked stadiums to dynamically size the history buffer.
    // With N unlocked, history size = min(N-1, 4) so at least 1 is selectable.
    int unlocked_count = 0;
    for (int i = 0; i < STKIND_NUM; i++)
    {
        if (mask & (1 << i))
            unlocked_count++;
    }

    int history_size = unlocked_count - 1;
    if (history_size > STADIUM_HISTORY_SIZE)
        history_size = STADIUM_HISTORY_SIZE;
    if (history_size < 0)
        history_size = 0;

    // Build candidate list
    int candidate_kinds[STKIND_NUM];
    int candidate_weights[STKIND_NUM];
    int num_candidates = 0;
    int weight_total = 0;

    for (int i = 0; i < STKIND_NUM; i++)
    {
        if (!(mask & (1 << i)))
            continue;

        if (menu_selection == 0)
        {
            // Shuffle mode: exclude recent history
            int in_history = 0;
            for (int j = 0; j < history_size; j++)
            {
                if (gd->city.prev_stadium_kind[j] == i)
                {
                    in_history = 1;
                    break;
                }
            }
            if (in_history)
                continue;
        }
        else
        {
            // Specific group mode: only include matching group
            StadiumGroup group = Gm_GetStadiumGroupFromKind(i);
            if (group != menu_selection - 1)
                continue;
        }

        candidate_kinds[num_candidates] = i;
        candidate_weights[num_candidates] = gda->stadium_weights->weights[i];
        weight_total += candidate_weights[num_candidates];
        num_candidates++;
    }

    // Fallback: if no candidates in the selected group, try all unlocked
    if (num_candidates == 0)
    {
        for (int i = 0; i < STKIND_NUM; i++)
        {
            if (mask & (1 << i))
            {
                candidate_kinds[num_candidates] = i;
                candidate_weights[num_candidates] = gda->stadium_weights->weights[i];
                weight_total += candidate_weights[num_candidates];
                num_candidates++;
            }
        }
    }

    // Weighted random selection
    u8 selected = 0;
    if (weight_total > 0 && num_candidates > 0)
    {
        int roll = HSD_Randi(weight_total);
        int cumulative = 0;
        for (int i = 0; i < num_candidates; i++)
        {
            cumulative += candidate_weights[i];
            if (roll < cumulative)
            {
                selected = (u8)candidate_kinds[i];
                break;
            }
        }
    }

    // Shift history buffer
    for (int i = STADIUM_HISTORY_SIZE - 1; i > 0; i--)
        gd->city.prev_stadium_kind[i] = gd->city.prev_stadium_kind[i - 1];
    gd->city.prev_stadium_kind[0] = selected;

    gd->city.stadium_kind = selected;

    OSReport("[GateStadiums] Selected %d (%s) from %d candidates (unlocked=%d, group=%d)\n",
             selected, StadiumKind_Names[selected], num_candidates,
             unlocked_count, menu_selection);
}

// Replaces the four vanilla unlock-check functions. ap_save is NULL before
// OnSaveLoaded runs, but Gm_StadiumCheckUnlocked is called during early init -
// return 0 in that window.
static int GateStadiums_IsUnlocked(StadiumKind kind)
{
    if (!ap_save || kind < 0 || kind >= STKIND_NUM)
        return 0;
    return (ap_save->stadium_unlocked_mask & (1 << kind)) != 0;
}

void GateStadiums_OnBoot()
{
    // Replace all four stadium unlock check functions with our mask check.
    // Gm_StadiumIsAvailable inlines its own jump tables for the IsDefault and
    // IsUnlocked checks, so replacing only the underlying funcs would not
    // redirect it - all four must be replaced independently.
    CODEPATCH_REPLACEFUNC(Gm_StadiumIsDefaultUnlocked, GateStadiums_IsUnlocked);
    CODEPATCH_REPLACEFUNC(Gm_StadiumIsUnlocked,        GateStadiums_IsUnlocked);
    CODEPATCH_REPLACEFUNC(Gm_StadiumIsAvailable,       GateStadiums_IsUnlocked);
    CODEPATCH_REPLACEFUNC(Gm_StadiumCheckUnlocked,     GateStadiums_IsUnlocked);

    // Replace CityTrial_DecideStadium to fix the vanilla history-buffer bug:
    // with fewer than 5 stadiums unlocked, the fixed-size 4-entry exclusion
    // can leave zero candidates, causing HSD_Randi(0).
    CODEPATCH_REPLACEFUNC(CityTrial_DecideStadium, GateStadiums_DecideStadium);

    // CityTrial_BuildStadiumList has two vanilla side-channels that bypass
    // our unlock-check replacement:
    //
    // 1) Phase 1 auto-unlock loop (0x80046e34): when CT progress hits a late
    //    threshold, the loop iterates over every locked stadium and calls
    //    Gm_StadiumWriteUnlocked + Gm_StadiumWriteNewLabel. The unlock write
    //    is harmless (we ignore that bitfield), but the new-label write
    //    causes "NEW" badges to appear on locked stadiums in the UI.
    //    Skip the loop entirely by turning the entry blt into an
    //    unconditional branch to its fallthrough target.
    //    Original: blt 0x80046e6c   (skip loop if progress < 3)
    //    Replace:  b   0x80046e6c
    CODEPATCH_REPLACEINSTRUCTION(0x80046e1c, 0x48000050);

    // 2) Phase 2 checklist fallback (0x80046ef8): when our mask says locked,
    //    vanilla falls through to Checklist_CheckCachedUnlock_CityTrial /
    //    ClearChecker_CheckUnlocked, which can re-add the stadium to the
    //    list. Redirect the "locked" case straight to the next iteration.
    //    Original: beq 0x80046f44   (locked -> checklist fallback)
    //    Replace:  beq 0x80046fc4   (locked -> next iteration)
    CODEPATCH_REPLACEINSTRUCTION(0x80046ef8, 0x418200CC);

    OSReport("[GateStadiums] Hooks installed\n");
}

int GateStadiums_UnlockStadium(StadiumKind kind, int announce)
{
    if (kind < 0 || kind >= STKIND_NUM)
        return 0;

    ap_save->stadium_unlocked_mask |= (1 << kind);
    // Set the vanilla "NEW" bitfield so the checklist UI shows the badge -
    // its read function (Gm_StadiumCheckNewLabel) is not replaced.
    Gm_StadiumSetNewLabelDirect(kind);
    OSReport("[GateStadiums] Stadium %d (%s) unlocked (mask = %s)\n",
             kind, StadiumKind_Names[kind], MaskBits(ap_save->stadium_unlocked_mask, STKIND_NUM));
    if (announce)
        tb_api->EnqueueColoredNoun("Unlocked Stadium: ", StadiumKind_Names[kind], tb_api->StadiumColor, NULL);
    return 1;
}

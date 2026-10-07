#include "game.h"
#include "hsd.h"
#include "os.h"
#include "stage.h"
#include "stadium.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_stadiums.h"
#include "textbox_api.h"
#include "inline.h"
#include "ap_announce.h"

// prev_stadium_kind[] holds 5, but vanilla excludes only the last 4.
#define STADIUM_HISTORY_SIZE 4

// Replaces CityTrial_DecideStadium (0x8003f808), whose fixed-size history exclusion can
// leave no candidate when few stadiums are unlocked and reach HSD_Randi(0).
static void GateStadiums_DecideStadium()
{
    GameData *gd = Gm_GetGameData();
    gmDataAll *gda = *stc_gmdataall;
    u32 mask = ap_save->stadium_unlocked_mask;
    u8 menu_selection = gd->city.menu_stadium_selection;

    // History size is min(unlocked - 1, 4) so at least one stadium stays selectable.
    int unlocked_count = Popcount64(mask & ((1u << STKIND_NUM) - 1));
    int history_size = unlocked_count - 1;
    if (history_size > STADIUM_HISTORY_SIZE)
        history_size = STADIUM_HISTORY_SIZE;
    if (history_size < 0)
        history_size = 0;

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
            // Shuffle
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
            // One group
            StadiumGroup group = Gm_GetStadiumGroupFromKind(i);
            if (group != menu_selection - 1)
                continue;
        }

        candidate_kinds[num_candidates] = i;
        candidate_weights[num_candidates] = gda->stadium_weights->weights[i];
        weight_total += candidate_weights[num_candidates];
        num_candidates++;
    }

    // No unlocked stadium in the selected group - fall back to all unlocked.
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

    u8 selected = 0;
    if (weight_total > 0 && num_candidates > 0)
    {
        int idx = Gm_Roll(candidate_weights, num_candidates);
        if (idx >= 0)
            selected = (u8)candidate_kinds[idx];
    }

    for (int i = STADIUM_HISTORY_SIZE - 1; i > 0; i--)
        gd->city.prev_stadium_kind[i] = gd->city.prev_stadium_kind[i - 1];
    gd->city.prev_stadium_kind[0] = selected;

    gd->city.stadium_kind = selected;

    OSReport("[GateStadiums] Selected %d (%s) from %d candidates (unlocked=%d, group=%d)\n",
             selected, StadiumKind_Names[selected], num_candidates,
             unlocked_count, menu_selection);
}

// Replaces the four vanilla unlock checks. Gm_InitData reaches Gm_StadiumCheckUnlocked
// before OnSaveInit sets ap_save.
static int GateStadiums_IsUnlocked(StadiumKind kind)
{
    if (!ap_save || kind < 0 || kind >= STKIND_NUM)
        return 0;
    return (ap_save->stadium_unlocked_mask & (1 << kind)) != 0;
}

void GateStadiums_OnBoot()
{
    // Gm_StadiumIsAvailable inlines its own copies of the IsDefault and IsUnlocked jump
    // tables, so all four are replaced.
    CODEPATCH_REPLACEFUNC(Gm_StadiumIsDefaultUnlocked, GateStadiums_IsUnlocked);
    CODEPATCH_REPLACEFUNC(Gm_StadiumIsUnlocked,        GateStadiums_IsUnlocked);
    CODEPATCH_REPLACEFUNC(Gm_StadiumIsAvailable,       GateStadiums_IsUnlocked);
    CODEPATCH_REPLACEFUNC(Gm_StadiumCheckUnlocked,     GateStadiums_IsUnlocked);

    CODEPATCH_REPLACEFUNC(CityTrial_DecideStadium, GateStadiums_DecideStadium);

    // CityTrial_BuildStadiumList (0x80046df0) bypasses the unlock checks twice. Its debug
    // unlock-all (stc_dblevel >= 3 with R + D-Up held) is skipped outright:
    // blt 0x80046e6c -> b 0x80046e6c.
    CODEPATCH_REPLACEINSTRUCTION(0x80046e1c, 0x48000050);

    // Its checklist fallback re-adds locked stadiums, so the locked case goes to the next
    // iteration instead: beq 0x80046f44 -> beq 0x80046fc4.
    CODEPATCH_REPLACEINSTRUCTION(0x80046ef8, 0x418200CC);

    OSReport("[GateStadiums] Hooks installed\n");
}

int GateStadiums_UnlockStadium(StadiumKind kind, int announce)
{
    if (kind < 0 || kind >= STKIND_NUM)
        return 0;

    ap_save->stadium_unlocked_mask |= (1 << kind);
    if (ap_regrant_quiet)
        return 1;

    // The "NEW" badge, read through the unreplaced Gm_StadiumCheckNewLabel. A re-grant is
    // not new.
    *stc_stadium_new_label |= (1 << kind);
    OSReport("[GateStadiums] Stadium %d (%s) unlocked (mask = %s)\n",
             kind, StadiumKind_Names[kind], MaskBits(ap_save->stadium_unlocked_mask, STKIND_NUM));
    if (announce)
        APAnnounce_Grant("Unlocked Stadium: ", StadiumKind_Names[kind], tb_api->StadiumColor, NULL);
    return 1;
}

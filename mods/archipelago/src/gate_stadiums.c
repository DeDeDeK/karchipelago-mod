#include "game.h"
#include "hsd.h"
#include "os.h"
#include "stadium.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_stadiums.h"
#include "textbox_api.h"
#include "inline.h"

// gd->city.prev_stadium_kind[] is sized 5, but vanilla only uses 4 entries for history
// exclusion.
#define STADIUM_HISTORY_SIZE 4

// Replaces CityTrial_DecideStadium (0x8003f808), whose fixed-size history exclusion
// can leave zero candidates when few stadiums are unlocked, reaching HSD_Randi(0).
static void GateStadiums_DecideStadium()
{
    GameData *gd = Gm_GetGameData();
    gmDataAll *gda = *stc_gmdataall;
    u32 mask = ap_save->stadium_unlocked_mask;
    u8 menu_selection = gd->city.menu_stadium_selection;

    // History size is min(unlocked - 1, 4) so at least one stadium stays selectable.
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
            // Shuffle mode
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
            // Specific group mode
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

    for (int i = STADIUM_HISTORY_SIZE - 1; i > 0; i--)
        gd->city.prev_stadium_kind[i] = gd->city.prev_stadium_kind[i - 1];
    gd->city.prev_stadium_kind[0] = selected;

    gd->city.stadium_kind = selected;

    OSReport("[GateStadiums] Selected %d (%s) from %d candidates (unlocked=%d, group=%d)\n",
             selected, StadiumKind_Names[selected], num_candidates,
             unlocked_count, menu_selection);
}

// Replaces the four vanilla unlock-check functions. ap_save is NULL before OnSaveLoaded
// runs, but Gm_StadiumCheckUnlocked is called during early init.
static int GateStadiums_IsUnlocked(StadiumKind kind)
{
    if (!ap_save || kind < 0 || kind >= STKIND_NUM)
        return 0;
    return (ap_save->stadium_unlocked_mask & (1 << kind)) != 0;
}

void GateStadiums_OnBoot()
{
    // Gm_StadiumIsAvailable inlines its own copies of the IsDefault and IsUnlocked jump
    // tables, so all four must be replaced independently.
    CODEPATCH_REPLACEFUNC(Gm_StadiumIsDefaultUnlocked, GateStadiums_IsUnlocked);
    CODEPATCH_REPLACEFUNC(Gm_StadiumIsUnlocked,        GateStadiums_IsUnlocked);
    CODEPATCH_REPLACEFUNC(Gm_StadiumIsAvailable,       GateStadiums_IsUnlocked);
    CODEPATCH_REPLACEFUNC(Gm_StadiumCheckUnlocked,     GateStadiums_IsUnlocked);

    CODEPATCH_REPLACEFUNC(CityTrial_DecideStadium, GateStadiums_DecideStadium);

    // CityTrial_BuildStadiumList has two side-channels that bypass the unlock-check
    // replacement. Its phase 1 auto-unlock loop (0x80046e34) badges every locked
    // stadium "NEW" past a late CT-progress threshold, so its entry blt becomes an
    // unconditional branch (blt 0x80046e6c -> b 0x80046e6c).
    CODEPATCH_REPLACEINSTRUCTION(0x80046e1c, 0x48000050);

    // Its phase 2 checklist fallback re-adds locked stadiums via
    // Checklist_CheckCachedUnlock_CityTrial / ClearChecker_CheckUnlocked, so the locked
    // case is retargeted to the next iteration (beq 0x80046f44 -> beq 0x80046fc4).
    CODEPATCH_REPLACEINSTRUCTION(0x80046ef8, 0x418200CC);

    OSReport("[GateStadiums] Hooks installed\n");
}

int GateStadiums_UnlockStadium(StadiumKind kind, int announce)
{
    if (kind < 0 || kind >= STKIND_NUM)
        return 0;

    ap_save->stadium_unlocked_mask |= (1 << kind);
    // Gm_StadiumCheckNewLabel is not replaced, so the checklist UI still reads the
    // vanilla "NEW" bitfield for the badge.
    *stc_stadium_new_label |= (1 << kind);
    OSReport("[GateStadiums] Stadium %d (%s) unlocked (mask = %s)\n",
             kind, StadiumKind_Names[kind], MaskBits(ap_save->stadium_unlocked_mask, STKIND_NUM));
    if (announce)
        tb_api->EnqueueColoredNoun("Unlocked Stadium: ", StadiumKind_Names[kind], tb_api->StadiumColor, NULL);
    return 1;
}

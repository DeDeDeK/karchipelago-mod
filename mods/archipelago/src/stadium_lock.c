#include "game.h"
#include "hsd.h"
#include "os.h"
#include "stadium.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "stadium_lock.h"

// Vanilla uses 4 of the 5 prev_stadium_kind entries for history exclusion
#define STADIUM_HISTORY_SIZE 4

// Replacement for CityTrial_DecideStadium (0x8003f808).
// Fixes the vanilla history buffer issue where few unlocked stadiums
// cause the history to exclude all candidates, leading to HSD_Randi(0).
// Uses only our AP stadium_unlocked_mask as the source of truth.
void StadiumLock_DecideStadium()
{
    GameData *gd = Gm_GetGameData();
    gmDataAll *gda = *stc_gmdataall;
    u32 mask = save_data->stadium_unlocked_mask;
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
        // Must be unlocked by AP
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

    // Set the stadium kind for this round
    gd->city.stadium_kind = selected;
}

// Apply patches needed for stadium locking
void StadiumLock_OnBoot()
{
    OSReport("Applying stadium lock patches...\n");
    // Replace Gm_StadiumIsDefaultUnlocked (hardcoded switch for 18 stadiums)
    // with Gm_StadiumCheckUnlocked (reads the runtime bitfield). This makes
    // any remaining callers respect our AP unlock state.
    CODEPATCH_REPLACEFUNC(Gm_StadiumIsDefaultUnlocked, Gm_StadiumCheckUnlocked);

    // Replace CityTrial_DecideStadium entirely to fix history buffer overflow
    // and use our AP mask as the sole unlock source.
    CODEPATCH_REPLACEFUNC(CityTrial_DecideStadium, StadiumLock_DecideStadium);
}

// Sync the game's stadium bitfield to match our saved unlock mask.
// The game persists the bitfield in its own save data, but we override it
// from our own saved mask to ensure AP unlocks are correctly applied.
void StadiumLock_OnSaveLoaded()
{
    OSReport("Syncing stadium unlocks from save (mask=0x%08X).\n", save_data->stadium_unlocked_mask);
    for (int i = 0; i < STKIND_NUM; i++)
    {
        if (save_data->stadium_unlocked_mask & (1 << i))
        {
            Gm_StadiumSetUnlockedDirect(i);
        }
        else
        {
            Gm_StadiumClearUnlockedDirect(i);
            Gm_StadiumClearNewLabelDirect(i);
        }
    }
}

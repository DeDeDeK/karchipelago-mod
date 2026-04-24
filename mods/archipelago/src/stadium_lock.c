#include "game.h"
#include "hsd.h"
#include "os.h"
#include "stadium.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "stadium_lock.h"
#include "textbox.h"
#include "inline.h"

static const char *stadium_names[STKIND_NUM] = {
    [STKIND_DRAG1]         = "Drag Race 1",
    [STKIND_DRAG2]         = "Drag Race 2",
    [STKIND_DRAG3]         = "Drag Race 3",
    [STKIND_DRAG4]         = "Drag Race 4",
    [STKIND_AIRGLIDER]     = "Air Glider",
    [STKIND_TARGETFLIGHT]  = "Target Flight",
    [STKIND_HIGHJUMP]      = "High Jump",
    [STKIND_MELEE1]        = "Kirby Melee 1",
    [STKIND_MELEE2]        = "Kirby Melee 2",
    [STKIND_DESTRUCTION1]  = "Destruction Derby 1",
    [STKIND_DESTRUCTION2]  = "Destruction Derby 2",
    [STKIND_DESTRUCTION3]  = "Destruction Derby 3",
    [STKIND_DESTRUCTION4]  = "Destruction Derby 4",
    [STKIND_DESTRUCTION5]  = "Destruction Derby 5",
    [STKIND_SINGLERACE1]   = "Single Race 1",
    [STKIND_SINGLERACE2]   = "Single Race 2",
    [STKIND_SINGLERACE3]   = "Single Race 3",
    [STKIND_SINGLERACE4]   = "Single Race 4",
    [STKIND_SINGLERACE5]   = "Single Race 5",
    [STKIND_SINGLERACE6]   = "Single Race 6",
    [STKIND_SINGLERACE7]   = "Single Race 7",
    [STKIND_SINGLERACE8]   = "Single Race 8",
    [STKIND_SINGLERACE9]   = "Single Race: Nebula Belt",
    [STKIND_VSKINGDEDEDE]  = "Vs. King Dedede",
};

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

// Replacement for both Gm_StadiumIsDefaultUnlocked and Gm_StadiumIsUnlocked.
// Checks our save mask directly — the game's internal bitfield and checklist
// system are completely bypassed.
static u32 stl_last_logged_mask = 0xDEADBEEF;

static int StadiumLock_IsUnlocked(StadiumKind kind)
{
    if (!ap_save || kind < 0 || kind >= STKIND_NUM)
        return 0;
    u32 mask = ap_save->stadium_unlocked_mask;
    if (mask != stl_last_logged_mask)
    {
        OSReport("[Stadium] IsUnlocked called, mask=%s (was %s)\n", MaskBits(mask, 32), MaskBits(stl_last_logged_mask, 32));
        stl_last_logged_mask = mask;
    }
    return (mask & (1 << kind)) != 0;
}

// Apply patches needed for stadium locking
void StadiumLock_OnBoot()
{
    OSReport("[Stadium] Applying stadium lock patches...\n");
    // Replace all four stadium unlock check functions with our mask check.
    // Gm_StadiumIsDefaultUnlocked (0x8000C148): hardcoded switch for default stadiums.
    // Gm_StadiumIsUnlocked (0x8000C17C): checklist-based check.
    // Gm_StadiumIsAvailable (0x8000C228): composite check (default + checklist + bitfield).
    // Gm_StadiumCheckUnlocked (0x80007EE4): reads game's internal bitfield at 0x80536EE8.
    CODEPATCH_REPLACEFUNC(Gm_StadiumIsDefaultUnlocked, StadiumLock_IsUnlocked);
    CODEPATCH_REPLACEFUNC(Gm_StadiumIsUnlocked, StadiumLock_IsUnlocked);
    CODEPATCH_REPLACEFUNC(Gm_StadiumIsAvailable, StadiumLock_IsUnlocked);
    CODEPATCH_REPLACEFUNC(Gm_StadiumCheckUnlocked, StadiumLock_IsUnlocked);

    // Replace CityTrial_DecideStadium entirely to fix history buffer overflow
    // and use our AP mask as the sole unlock source.
    CODEPATCH_REPLACEFUNC(CityTrial_DecideStadium, StadiumLock_DecideStadium);

    // Patch CityTrial_BuildStadiumList to remove two vanilla fallback paths
    // that bypass our mask check:
    //
    // 1) Phase 1 auto-unlock loop (0x80046e1c): vanilla checks a progress
    //    condition and force-unlocks all stadiums via Gm_StadiumWriteUnlocked
    //    if our mask check returns 0. Always skip this loop.
    //    Original: blt 0x80046e6c (skip if progress < 3)
    //    Replace:  b 0x80046e6c (always skip)
    CODEPATCH_REPLACEINSTRUCTION(0x80046e1c, 0x48000050); // b 0x80046e6c

    // 2) Phase 2 checklist fallback (0x80046ef8): when our mask says locked,
    //    vanilla falls through to ClearChecker_CheckUnlocked and adds the
    //    stadium anyway if the checklist says unlocked. Redirect "locked"
    //    to the next loop iteration instead of the fallback.
    //    Original: beq 0x80046f44 (if locked -> checklist fallback)
    //    Replace:  beq 0x80046fc4 (if locked -> next iteration)
    CODEPATCH_REPLACEINSTRUCTION(0x80046ef8, 0x418200CC); // beq 0x80046fc4
}

int GateStadium_UnlockStadium(StadiumKind kind)
{
    if (kind < 0 || kind >= STKIND_NUM)
        return 0;

    ap_save->stadium_unlocked_mask |= (1 << kind);
    Gm_StadiumSetUnlockedDirect(kind);
    Gm_StadiumSetNewLabelDirect(kind);
    OSReport("[Stadium] Stadium %d (%s) unlocked (mask = %s)\n",
             kind, stadium_names[kind], MaskBits(ap_save->stadium_unlocked_mask, STKIND_NUM));
    TextBox_Enqueue(stadium_names[kind]);
    return 1;
}


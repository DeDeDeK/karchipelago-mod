#include <string.h>

#include "game.h"
#include "os.h"
#include "inline.h"

#include "custom_checklist.h"

// Set by OnSaveLoaded once the stamp is checked; until then save_ptr may still be hoshi's
// default block, which the card load replaces.
static CCSave *cc_save = NULL;

// A blank board: every cell hidden, grid_mapping freshly shuffled. grid_mapping must be a
// full bijection over all 120 clear_kinds or Checklist_Update's reverse scan trips the
// "Clearchecker Number 120" assert.
static void CC_InitClear(GameClearData *cd)
{
    memset(cd, 0, sizeof(*cd));
    for (int k = 0; k < CLEAR_KIND_NUM; k++)
        cd->grid_mapping[k] = (u8)k;
    RandomShuffle(cd->grid_mapping, CLEAR_KIND_NUM, sizeof(cd->grid_mapping[0]));
}

void CCBoard_Init(int idx)
{
    CCTab *t = &cc_tabs[idx];
    CC_InitClear(&t->ram_clear);
    t->clear = &t->ram_clear;
}

// A consumer's OnSaveLoaded runs ahead of this mod's, so a tab can take writes before it
// binds. Those are per-cell flag sets and filler grants, never positional, so they merge
// into the saved board whatever its layout.
static void CC_MergeBoard(GameClearData *dst, const GameClearData *src)
{
    for (int k = 0; k < CLEAR_KIND_NUM; k++)
        ((u8 *)dst->clear)[k] |= ((const u8 *)src->clear)[k];
    dst->checkbox_filler_num += src->checkbox_filler_num;
    int len = dst->checkbox_filler_list_len + src->checkbox_filler_list_len;
    dst->checkbox_filler_list_len = len < CHECKBOX_FILLER_LIST_MAX ? len : CHECKBOX_FILLER_LIST_MAX;
}

// Move the tab off its RAM block onto its save slot, claiming and laying out an empty one if
// it has none.
void CCBoard_Bind(int idx)
{
    CCTab *t = &cc_tabs[idx];
    if (!cc_save || t->clear != &t->ram_clear)
        return;

    int s, empty = -1;
    for (s = 0; s < CC_TAB_MAX; s++)
    {
        if (cc_save->slots[s].name_hash == t->name_hash)
            break;
        if (empty < 0 && cc_save->slots[s].name_hash == 0)
            empty = s;
    }
    if (s == CC_TAB_MAX)
    {
        if (empty < 0)
        {
            OSReport("[CustomChecklist] '%s': no free save slot, board not saved\n", t->desc.name);
            return;
        }
        s = empty;
        cc_save->slots[s].name_hash = t->name_hash;
        CC_InitClear(&cc_save->slots[s].clear);
    }
    CC_MergeBoard(&cc_save->slots[s].clear, &t->ram_clear);
    t->clear = &cc_save->slots[s].clear;
}

void CCBoard_InitSave(CCSave *save)
{
    memset(save, 0, sizeof(*save));
    save->stamp = CC_SAVE_STAMP;
}

void CCBoard_LoadSave(CCSave *save)
{
    cc_save = save;
    if (save->stamp != CC_SAVE_STAMP)
    {
        OSReport("[CustomChecklist] Save block initialized\n");
        CCBoard_InitSave(save);
    }

    for (int i = 0; i < cc_tab_num; i++)
        CCBoard_Bind(i);
}

int CCBoard_HasPendingUnlock(const GameClearData *cd)
{
    for (int k = 0; k < CLEAR_KIND_NUM; k++)
        if (cd->clear[k].is_new && !cd->clear[k].is_unlocked)
            return 1;
    return 0;
}

// Show every cell backed by a check; returns how many were newly shown. Cells with no check
// stay hidden - a revealed empty box reads as an objective that can never be completed.
int CCBoard_RevealChecks(int idx)
{
    CCTab *t = &cc_tabs[idx];
    int n = 0;
    for (int c = 0; c < t->desc.check_num; c++)
    {
        int ck = t->desc.checks[c].clear_kind;
        if (t->clear->clear[ck].is_visible)
            continue;
        t->clear->clear[ck].is_visible = 1;
        n++;
    }
    return n;
}

// Show the cell at a physical grid slot, resolved back through grid_mapping.
static void CC_RevealSlot(GameClearData *cd, int slot)
{
    for (int k = 0; k < CLEAR_KIND_NUM; k++)
    {
        if (cd->grid_mapping[k] == (u8)slot)
        {
            cd->clear[k].is_visible = 1;
            return;
        }
    }
}

// The expansion Checklist_ProcessUnlock performs as it animates an unlock, repeated for
// cells that reach the board already complete, which the engine never animates.
static void CC_RevealNeighbors(GameClearData *cd, int clear_kind)
{
    int slot = cd->grid_mapping[clear_kind];
    int col = slot % CHECKLIST_GRID_COLS;
    int row = slot / CHECKLIST_GRID_COLS;

    if (col > 0)
        CC_RevealSlot(cd, slot - 1);
    if (col < CHECKLIST_GRID_COLS - 1)
        CC_RevealSlot(cd, slot + 1);
    if (row > 0)
        CC_RevealSlot(cd, slot - CHECKLIST_GRID_COLS);
    if (row < CHECKLIST_GRID_ROWS - 1)
        CC_RevealSlot(cd, slot + CHECKLIST_GRID_COLS);
}

// Shares ClearChecker_SetNewUnlock's one-frame cooldown, so a record_complete that routes
// through it can't double-play.
static void CC_PlayUnlockSfx(void)
{
    int frame = Gm_GetEngineFrames();
    if (*stc_clearchecker_sfx_last_frame == frame)
        return;
    SFX_PlayFullVolume(CLEARCHECKER_UNLOCK_SFX);
    *stc_clearchecker_sfx_last_frame = frame;
}

// Complete any check whose predicate now holds, and put recorded checks on the board.
void CCBoard_Evaluate(void)
{
    for (int i = 0; i < cc_tab_num; i++)
    {
        CCTab *t = &cc_tabs[i];
        if (t->desc.is_ready && !t->desc.is_ready())
            continue;

        GameClearData *cd = t->clear;
        for (int c = 0; c < t->desc.check_num; c++)
        {
            const CustomCheck *chk = &t->desc.checks[c];
            int ck = chk->clear_kind;
            int on_board = cd->clear[ck].is_new || cd->clear[ck].is_unlocked;

            if (t->desc.is_recorded(ck))
            {
                // Recorded with no board entry - a blank block, or the RAM fallback each
                // boot - shows complete without an animation.
                if (!on_board)
                {
                    cd->clear[ck].is_unlocked = 1;
                    CC_RevealNeighbors(cd, ck);
                }
            }
            else if (!on_board && chk->is_complete(ck))
            {
                t->desc.record_complete(ck);

                // Only ClearChecker_SetNewUnlock sets is_new, and a tab need not record
                // through it. A LAN session suppresses it, as vanilla does.
                if (!Net_IsSessionActive())
                {
                    cd->clear[ck].is_new = 1;
                    CC_PlayUnlockSfx();
                }
            }
        }
    }
}

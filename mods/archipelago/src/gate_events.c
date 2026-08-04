#include "game.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_events.h"
#include "textbox_api.h"
#include "inline.h"

// Hook body at 0x800ede24 in CityEvent_Decide: chance_arr is the 16-entry stack chance
// array (sp+0x08), already filled from the weights table but before history adjustment.
void GateEvents_FilterChances(int *chance_arr, EventCheckData *ev_chk)
{
    u32 mask = ap_save->event_unlocked_mask;
    int enabled_count = 0;

    for (int i = 0; i < EVKIND_NUM; i++)
    {
        if (!(mask & (1 << i)))
            chance_arr[i] = 0;
        else if (chance_arr[i] > 0)
            enabled_count++;
    }

    // The game never repeats a recent event, so a history longer than the enabled-event
    // count deadlocks selection. Capping at ~62.5% always leaves a candidate.
    int max_history = (enabled_count * 5) / 8;
    int old_history = ev_chk->prev_kind_num;
    if (ev_chk->prev_kind_num > max_history)
        ev_chk->prev_kind_num = max_history;

    OSReport("[Events] CityEvent_Decide called: mask=%s, enabled=%d, history=%d->%d\n",
             MaskBits(mask, EVKIND_NUM), enabled_count, old_history, ev_chk->prev_kind_num);
    for (int i = 0; i < EVKIND_NUM; i++)
    {
        if (chance_arr[i] > 0)
            OSReport("  [%2d] %s: weight=%d\n", i, EventKind_Names[i], chance_arr[i]);
    }
}

// Clobbered: lwz r0, 64(r26) (prev_kind_num), re-executed after so the lowered value
// feeds the history-adjustment loop.
CODEPATCH_HOOKCREATE(0x800ede24,
    "addi 3, 1, 8\n\t"
    "mr 4, 26\n\t",
    GateEvents_FilterChances,
    "",
    0
)

void GateEvents_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x800ede24);
    OSReport("[Events] Event gating hook installed at CityEvent_Decide\n");
}

int GateEvents_UnlockEvent(int kind)
{
    if (kind < 0 || kind >= EVKIND_NUM)
        return 0;

    const char *name = EventKind_Names[kind];
    ap_save->event_unlocked_mask |= (1 << kind);
    OSReport("[Events] Event %d (%s) unlocked (mask = %s)\n",
             kind, name, MaskBits(ap_save->event_unlocked_mask, EVKIND_NUM));
    tb_api->EnqueueColoredNoun("Unlocked Event: ", name, tb_api->EventColor, NULL);
    return 1;
}

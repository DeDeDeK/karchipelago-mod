#include "game.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_events.h"
#include "textbox_api.h"
#include "inline.h"

// Called from the hook at 0x800ede24 in CityEvent_Decide.
// At this point, the local chance array on the stack has been populated from the
// weights table but history adjustments have not yet been applied.
// Zeroing out locked events before history adjustment is correct — if an event
// is locked, we don't care about its history weight.
//
// chance_arr: pointer to the 16-entry int array of event chances (sp+0x08).
// ev_chk: pointer to EventCheckData (r26).
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

    // Adjust event history buffer to prevent deadlock when few events are enabled.
    // The game won't pick an event that's in the recent history. If the history is
    // larger than the number of enabled events, no event can ever trigger.
    // Cap history at ~62.5% of enabled events (matches KAR Deluxe formula).
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

// Hook at 0x800ede24 in CityEvent_Decide.
// Clobbered instruction: lwz r0, 64(r26)  (loads prev_kind_num into r0).
// At hook point: r1+0x08 = chance array on original function's stack, r26 = EventCheckData*.
// After our function returns, the clobbered instruction reloads prev_kind_num (which we
// may have modified) into r0 for the subsequent history adjustment loop.
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
    tb_api->EnqueueColoredNoun(NULL, name, tb_api->EventColor, NULL);
    return 1;
}

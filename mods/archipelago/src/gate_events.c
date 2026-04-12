#include "game.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_events.h"
#include "textbox.h"
#include "mask_fmt.h"

static const char *event_names[EVKIND_NUM] = {
    [EVKIND_DYNABLADE]        = "Dyna Blade",
    [EVKIND_TAC]              = "Tac",
    [EVKIND_METEOR]           = "Meteor",
    [EVKIND_PILLAR]           = "Pillar",
    [EVKIND_RUNAMOK]          = "Run Amok",
    [EVKIND_RESTORATIONAREA]  = "Restoration Area",
    [EVKIND_RAILFIRE]         = "Rail Fire",
    [EVKIND_SAMEITEM]         = "All Same Item",
    [EVKIND_LIGHTHOUSE]       = "Lighthouse",
    [EVKIND_SECRETCHAMBER]    = "Secret Chamber",
    [EVKIND_PREDICTION]       = "Prediction",
    [EVKIND_MACHINEFORMATION] = "Machine Formation",
    [EVKIND_UFO]              = "UFO",
    [EVKIND_BOUNCE]           = "Bounce",
    [EVKIND_FOG]              = "Fog",
    [EVKIND_FAKEPOWERUPS]     = "Fake Powerups",
};

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
             MaskBits(mask, 20), enabled_count, old_history, ev_chk->prev_kind_num);
    for (int i = 0; i < EVKIND_NUM; i++)
    {
        if (chance_arr[i] > 0)
            OSReport("  [%2d] %s: weight=%d\n", i, event_names[i], chance_arr[i]);
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

void GateEvents_LogEnabledEvents(void)
{
    u32 mask = ap_save->event_unlocked_mask;
    OSReport("[Events] Enabled events (mask=%s):\n", MaskBits(mask, 20));
    for (int i = 0; i < EVKIND_NUM; i++)
    {
        if (mask & (1 << i))
            OSReport("  [%2d] %s\n", i, event_names[i]);
    }
    if (custom_events)
    {
        for (int i = 0; i < custom_events->event_count; i++)
        {
            int bit = EVKIND_NUM + i;
            OSReport("  [%2d] %s: %s\n", bit, custom_events->params[i].label,
                     (mask & (1 << bit)) ? "enabled" : "disabled");
        }
    }
}

// Weight filter: gates custom events by AP unlock mask
static int APEventWeightFilter(int event_index, int default_weight)
{
    int bit = EVKIND_NUM + event_index;
    if (!(ap_save->event_unlocked_mask & (1 << bit)))
        return 0;
    return default_weight;
}

CustomEventWeightFilter GateEvents_GetWeightFilter(void)
{
    return APEventWeightFilter;
}

void GateEvents_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x800ede24);
    OSReport("[Events] Event gating hook installed at CityEvent_Decide\n");
}

int GateEvents_UnlockEvent(int kind)
{
    const char *name;

    if (kind < EVKIND_NUM)
        name = event_names[kind];
    else if (custom_events && kind < CUSTOM_EVKIND_NUM)
        name = custom_events->params[kind - EVKIND_NUM].label;
    else
        return 0;

    ap_save->event_unlocked_mask |= (1 << kind);
    OSReport("[Events] Event %d (%s) unlocked (mask = %s)\n",
             kind, name, MaskBits(ap_save->event_unlocked_mask, 20));
    TextBox_Enqueue(name);
    return 1;
}

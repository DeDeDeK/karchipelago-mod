#include "game.h"
#include "os.h"
#include "inline.h"
#include "text.h"
#include "audio.h"
#include "stage.h"
#include "code_patch/code_patch.h"
#include "hoshi/mod.h"

#include "custom_events.h"
#include "event_waddle_dee_swarm.h"
#include "event_gravity_change.h"
#include "event_scale_change.h"
#include "event_gourmet_race.h"

#define SIS_CITYTRIAL_ENTRY_COUNT 42

// Offset for custom event SIS IDs in the event name lookup table (0x804a7b98).
// Indices 16..39 are the vanilla prediction event's stadium name lookups, so
// custom entries must start after them.
#define CUSTOM_SIS_TABLE_OFFSET (EVKIND_NUM + STKIND_NUM)

CustomEventParam custom_params[CUSTOM_EVENT_COUNT] = {
    [CUSTOM_EVKIND_WADDLE_DEE_SWARM - EVKIND_NUM] = {
        .duration = 1800,   // ~30 seconds
        .is_siren = 1,
        .sky_preset = 5,   // Dark Vignette
        .bgm_file = 0x34,  // Runamok BGM
        .weight = 20,
        .label = "Waddle Dee Swarm",
        .hud_text = "Waddle Dee swarm incoming!",
    },
    [CUSTOM_EVKIND_GRAVITY_CHANGE - EVKIND_NUM] = {
        .duration = 900,   // ~15 seconds
        .is_siren = 1,
        .sky_preset = 8,   // Pink Sky
        .bgm_file = 0x31,  // Meteor BGM
        .weight = 20,
        .label = "Gravity Change",
        .hud_text = "Gravity is changing!",
    },
    [CUSTOM_EVKIND_SCALE_CHANGE - EVKIND_NUM] = {
        .duration = 900,   // ~15 seconds
        .is_siren = 1,
        .sky_preset = 3,   // Dusk 2
        .bgm_file = 0x32,  // Dyna Blade BGM
        .weight = 20,
        .label = "Scale Change",
        .hud_text = "The world is growing!",
    },
    [CUSTOM_EVKIND_GOURMET_RACE - EVKIND_NUM] = {
        .duration = 3600,  // ~60 seconds
        .is_siren = 1,
        .sky_preset = -1,  // No sky change
        .bgm_file = 0x34,  // Runamok BGM
        .weight = 20,
        .label = "Gourmet Race",
        .hud_text = "Gourmet Race!",
    },
};

static CustomEventFunc custom_functions[CUSTOM_EVENT_COUNT] = {
    [CUSTOM_EVKIND_WADDLE_DEE_SWARM - EVKIND_NUM] = {
        .start = WaddleDeeSwarm_Start,
        .active = WaddleDeeSwarm_Active,
        .end2 = WaddleDeeSwarm_End2,
    },
    [CUSTOM_EVKIND_GRAVITY_CHANGE - EVKIND_NUM] = {
        .start = GravityChange_Start,
        .active = GravityChange_Active,
        .end2 = GravityChange_End2,
    },
    [CUSTOM_EVKIND_SCALE_CHANGE - EVKIND_NUM] = {
        .start = ScaleChange_Start,
        .active = ScaleChange_Active,
        .end = ScaleChange_End,
        .end2 = ScaleChange_End2,
    },
    [CUSTOM_EVKIND_GOURMET_RACE - EVKIND_NUM] = {
        .start = GourmetRace_Start,
        .active = GourmetRace_Active,
        .end2 = GourmetRace_End2,
    },
};

static void *extended_sis_ptrs[SIS_CITYTRIAL_ENTRY_COUNT + CUSTOM_EVENT_COUNT];
static u8 custom_sis_text[CUSTOM_EVENT_COUNT][128];

// NULL = no gating, all events use their default weights.
static CustomEventWeightFilter weight_filter = NULL;

static void SetWeightFilter(CustomEventWeightFilter filter)
{
    weight_filter = filter;
    OSReport("[CustomEvents] Weight filter %s\n", filter ? "installed" : "removed");
}

// SIS text: opcodes < 0x20 are commands, characters are 2-byte codes >= 0x20.
static void ComposeSisText(u8 *buf, const char *str)
{
    u8 *p = buf;

    *p++ = 0x12; // ALIGN_LEFT
    *p++ = 0x18; // FIT_ON
    *p++ = 0x16; // KERNING_ON
    *p++ = 0x0c;
    *p++ = 0xbb;
    *p++ = 0xbb;
    *p++ = 0xbb; // COLOR gray
    *p++ = 0x0e;
    *p++ = 0x00;
    *p++ = 0xb3;
    *p++ = 0x00;
    *p++ = 0xb3; // SCALE ~0.70

    while (*str)
    {
        if (*str == ' ')
        {
            *p++ = 0x1a; // SIS space command
        }
        else
        {
            int cmd = Text_CharToCommand(*str);
            if (cmd != -1)
            {
                *p++ = (cmd >> 8) & 0xFF;
                *p++ = cmd & 0xFF;
            }
        }
        str++;
    }

    // LINEBREAK, SCALE_POP, COLOR_POP, KERNING_OFF, FIT_OFF, ALIGN_POP, TERMINATE
    *p++ = 0x03;
    *p++ = 0x0f;
    *p++ = 0x0d;
    *p++ = 0x17;
    *p++ = 0x19;
    *p++ = 0x13;
    *p++ = 0x00;
}

void CustomEvents_InitSis(void)
{
    // stc_sis_data[0] is City Trial's SIS pointer array (42 entries).
    void **original = (void *)stc_sis_data[0];
    if (!original)
    {
        OSReport("[CustomEvents] InitSis: stc_sis_data[0] is NULL\n");
        return;
    }

    for (int i = 0; i < SIS_CITYTRIAL_ENTRY_COUNT; i++)
        extended_sis_ptrs[i] = original[i];

    for (int i = 0; i < CUSTOM_EVENT_COUNT; i++)
    {
        ComposeSisText(custom_sis_text[i], custom_params[i].hud_text);
        int sis_idx = SIS_CITYTRIAL_ENTRY_COUNT + i;
        extended_sis_ptrs[sis_idx] = custom_sis_text[i];
    }

    // Text_InitPremadeText resolves entries through this pointer.
    stc_sis_data[0] = (SISData *)extended_sis_ptrs;

    int *sis_id_table = stc_event_sis_id_table;
    for (int i = 0; i < CUSTOM_EVENT_COUNT; i++)
        sis_id_table[CUSTOM_SIS_TABLE_OFFSET + i] = SIS_CITYTRIAL_ENTRY_COUNT + i;

    OSReport("[CustomEvents] InitSis: extended SIS array with %d custom entries\n",
             CUSTOM_EVENT_COUNT);
}

typedef void (*StateHandler)(EventCheckData *);
static StateHandler orig_state1;
static StateHandler orig_state2;
static StateHandler orig_state3;

// Vanilla kinds delegate to the original handler; custom kinds are handled
// entirely here, so they never index the vanilla 16-entry per-kind arrays.
static void CustomEvent_State1Wrapper(EventCheckData *ev_chk)
{
    if (ev_chk->cur_kind < EVKIND_NUM)
    {
        orig_state1(ev_chk);
        return;
    }

    // Siren period.
    int starting_delay = ev_chk->data->event->starting_delay;
    if ((int)ev_chk->timer < starting_delay)
        return;

    int idx = ev_chk->cur_kind - EVKIND_NUM;

    ev_chk->state = 2;
    ev_chk->timer = 0;

    // stadiumPrediction (downstream of CityEvent_ShowHudText) looks the text up
    // as sis_id_table[arg], so pass the remapped table index, not the raw kind.
    int hud_frames = ev_chk->data->event->hud_display_frames;
    CityEvent_ShowHudText(CUSTOM_SIS_TABLE_OFFSET + idx, hud_frames);

    // Secondary BGM pauses the main BGM.
    if (custom_params[idx].bgm_file != 0)
        BGM_PlaySecondaryFile(custom_params[idx].bgm_file);

    if (custom_functions[idx].start)
        custom_functions[idx].start(ev_chk);

    OSReport("[CustomEvents] Event %d started (SIS index %d)\n",
             ev_chk->cur_kind, SIS_CITYTRIAL_ENTRY_COUNT + idx);
}

static void CustomEvent_State2Wrapper(EventCheckData *ev_chk)
{
    if (ev_chk->cur_kind < EVKIND_NUM)
    {
        orig_state2(ev_chk);
        return;
    }

    int idx = ev_chk->cur_kind - EVKIND_NUM;

    if (custom_functions[idx].active)
        custom_functions[idx].active(ev_chk);

    if ((int)ev_chk->timer >= custom_params[idx].duration)
    {
        ev_chk->state = 3;
        ev_chk->timer = 0;

        if (custom_params[idx].is_siren && custom_params[idx].sky_preset != -1)
            Sky_RestoreGlobal();
    }
}

static void CustomEvent_State3Wrapper(EventCheckData *ev_chk)
{
    if (ev_chk->cur_kind < EVKIND_NUM)
    {
        orig_state3(ev_chk);
        return;
    }

    int idx = ev_chk->cur_kind - EVKIND_NUM;

    // Gradual cleanup, each frame.
    if (custom_functions[idx].end)
        custom_functions[idx].end(ev_chk);

    int cleanup_delay = ev_chk->data->event->cleanup_delay;
    if ((int)ev_chk->timer < cleanup_delay)
        return;

    // Final one-time cleanup.
    if (custom_functions[idx].end2)
        custom_functions[idx].end2(ev_chk);

    if (custom_params[idx].bgm_file != 0)
        BGM_StopSecondary();

    int delay_min = ev_chk->data->event->delay_min;
    int delay_max = ev_chk->data->event->delay_max;
    int delay = delay_min + HSD_Randi(delay_max - delay_min + 1);

    ev_chk->event_time = delay;
    ev_chk->state = 0;
    ev_chk->cur_kind = -1;
    ev_chk->timer = 0;

    OSReport("[CustomEvents] Cleanup complete, next delay = %d frames\n", delay);
}

// Replaces the Gm_Roll(chance_arr, 16) call inside CityEvent_Decide at 0x800ee098,
// adding the custom events to the pool. Returning -1 tells vanilla "no event
// selected, set a new delay".
static int CustomEvents_ExtendedRoll(int *chance_arr, int count)
{
    // Already filtered by gate + history + once-only.
    int vanilla_total = 0;
    for (int i = 0; i < count; i++)
        vanilla_total += chance_arr[i];

    int custom_weights[CUSTOM_EVENT_COUNT];
    int custom_total = 0;
    for (int i = 0; i < CUSTOM_EVENT_COUNT; i++)
    {
        int w = custom_params[i].weight;
        if (weight_filter)
            w = weight_filter(i, w);
        custom_weights[i] = w;
        custom_total += custom_weights[i];
    }

    int grand_total = vanilla_total + custom_total;
    if (grand_total == 0)
        return -1;

    int roll = HSD_Randi(grand_total);

    if (roll < vanilla_total)
    {
        // Delegate to Gm_Roll so the vanilla weighting is applied.
        int result = Gm_Roll(chance_arr, count);
        OSReport("[CustomEvents] ExtendedRoll: vanilla event %d (roll=%d, vanilla=%d, custom=%d)\n",
                 result, roll, vanilla_total, custom_total);
        return result;
    }

    roll -= vanilla_total;
    for (int i = 0; i < CUSTOM_EVENT_COUNT; i++)
    {
        roll -= custom_weights[i];
        if (roll < 0)
        {
            int kind = EVKIND_NUM + i;
            if (CustomEvent_Do(kind))
            {
                OSReport("[CustomEvents] ExtendedRoll: custom event %d (%s) selected (vanilla=%d, custom=%d)\n",
                         kind, custom_params[i].label, vanilla_total, custom_total);
                return -1;
            }
            // CustomEvent_Do failed (e.g. another event active).
            OSReport("[CustomEvents] ExtendedRoll: custom event %d failed, falling back to vanilla\n", kind);
            return Gm_Roll(chance_arr, count);
        }
    }

    return Gm_Roll(chance_arr, count);
}

static CustomEventsAPI api = {
    .Do = CustomEvent_Do,
    .params = custom_params,
    .event_count = CUSTOM_EVENT_COUNT,
    .SetWeightFilter = SetWeightFilter,
};

void CustomEvents_OnBoot(void)
{
    StateHandler *state_table = (StateHandler *)stc_event_state_table;

    orig_state1 = state_table[1];
    orig_state2 = state_table[2];
    orig_state3 = state_table[3];

    state_table[1] = CustomEvent_State1Wrapper;
    state_table[2] = CustomEvent_State2Wrapper;
    state_table[3] = CustomEvent_State3Wrapper;

    CODEPATCH_REPLACECALL(0x800ee098, CustomEvents_ExtendedRoll);

    ScaleChange_InstallHooks();

    Hoshi_ExportMod(&api);

    OSReport("[CustomEvents] Hooks installed\n");
}

int CustomEvent_Do(int kind)
{
    if (kind < EVKIND_NUM || kind >= CUSTOM_EVKIND_NUM)
        return 0;

    if (!stc_eventcheck_gobj || !*stc_eventcheck_gobj)
        return 0;

    GOBJ *g = *stc_eventcheck_gobj;
    EventCheckData *ev_chk = g->userdata;

    // Another event is already running.
    if (ev_chk->state != 0)
        return 0;

    int idx = kind - EVKIND_NUM;

    if (custom_functions[idx].check && !custom_functions[idx].check(ev_chk))
        return 0;

    // State 1 is the starting/siren phase.
    ev_chk->state = 1;
    ev_chk->cur_kind = kind;
    ev_chk->timer = 0;

    if (custom_params[idx].is_siren)
    {
        Gm_FadeOutMusic(ev_chk->data->event->music_fadeout_frames);
        SFX_Play(0x130002);

        if (custom_params[idx].sky_preset != -1)
            Sky_TransitionGlobal(custom_params[idx].sky_preset);
    }

    OSReport("[CustomEvents] Event %d triggered\n", kind);
    return 1;
}

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
// Must be placed AFTER the prediction stadium name range. The vanilla prediction
// event remaps event kind 10 to sis_id_table[stadium_kind + EVKIND_NUM], which
// spans indices 16 through 16+STKIND_NUM-1 (=39). Placing custom entries at
// EVKIND_NUM + STKIND_NUM avoids colliding with stadium name lookups.
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

// Extended pointer array: original 42 entries + our custom entries.
static void *extended_sis_ptrs[SIS_CITYTRIAL_ENTRY_COUNT + CUSTOM_EVENT_COUNT];

// Pre-composed SIS binary text for each custom event.
static u8 custom_sis_text[CUSTOM_EVENT_COUNT][128];

// Weight filter callback (NULL = standalone mode, all events use default weights).
static CustomEventWeightFilter weight_filter = NULL;

static void SetWeightFilter(CustomEventWeightFilter filter)
{
    weight_filter = filter;
    OSReport("[CustomEvents] Weight filter %s\n", filter ? "installed" : "removed");
}

// Compose a SIS-format text entry from a C string.
static void ComposeSisText(u8 *buf, const char *str)
{
    u8 *p = buf;

    // Header (matches vanilla event text formatting)
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

    // Trailer
    *p++ = 0x03;
    *p++ = 0x0f;
    *p++ = 0x0d;
    *p++ = 0x17;
    *p++ = 0x19;
    *p++ = 0x13;
    *p++ = 0x00; // TERMINATE
}

// Called from On3DLoadEnd when in City Trial.
// Extends the SIS pointer array with pre-composed custom event text.
void CustomEvents_InitSis(void)
{
    // stc_sis_data[0] points to the SIS pointer array for City Trial.
    // Each entry is a pointer to the binary SIS text data for that index.
    void **original = (void *)stc_sis_data[0];
    if (!original)
    {
        OSReport("[CustomEvents] InitSis: stc_sis_data[0] is NULL\n");
        return;
    }

    // Copy original entries
    for (int i = 0; i < SIS_CITYTRIAL_ENTRY_COUNT; i++)
        extended_sis_ptrs[i] = original[i];

    // Compose and register custom event text entries
    for (int i = 0; i < CUSTOM_EVENT_COUNT; i++)
    {
        ComposeSisText(custom_sis_text[i], custom_params[i].hud_text);
        int sis_idx = SIS_CITYTRIAL_ENTRY_COUNT + i;
        extended_sis_ptrs[sis_idx] = custom_sis_text[i];
    }

    // Replace SIS data pointer so Text_InitPremadeText can find our entries
    stc_sis_data[0] = (SISData *)extended_sis_ptrs;

    // Write custom SIS IDs into the event name lookup table at 0x804a7b98.
    // Indices 0-15 are vanilla event names. Indices 16-39 are used by the
    // vanilla prediction event (kind 10) as stadium name lookups: it stores
    // (stadium_kind + EVKIND_NUM) into the HUD control and the per-frame
    // update re-reads the table. We must place custom entries AFTER the
    // stadium range to avoid collision.
    int *sis_id_table = stc_event_sis_id_table;
    for (int i = 0; i < CUSTOM_EVENT_COUNT; i++)
        sis_id_table[CUSTOM_SIS_TABLE_OFFSET + i] = SIS_CITYTRIAL_ENTRY_COUNT + i;

    OSReport("[CustomEvents] InitSis: extended SIS array with %d custom entries\n",
             CUSTOM_EVENT_COUNT);
}

// Original state handler function pointers (saved from dispatch table).
typedef void (*StateHandler)(EventCheckData *);
static StateHandler orig_state1;
static StateHandler orig_state2;
static StateHandler orig_state3;

// State 1 wrapper: handles the starting -> active transition.
// For custom events: set state=2, show HUD text, call custom start.
// For vanilla events: delegate to original handler.
static void CustomEvent_State1Wrapper(EventCheckData *ev_chk)
{
    if (ev_chk->cur_kind < EVKIND_NUM)
    {
        orig_state1(ev_chk);
        return;
    }

    // --- Custom event: state 1 -> 2 transition ---

    // Wait for starting delay (siren period) before transitioning
    int starting_delay = ev_chk->data->event->starting_delay;
    if ((int)ev_chk->timer < starting_delay)
        return;

    int idx = ev_chk->cur_kind - EVKIND_NUM;

    // Transition to state 2
    ev_chk->state = 2;
    ev_chk->timer = 0;

    // Show HUD text via the vanilla popup pipeline.
    // CityEvent_ShowHudText -> stadiumPrediction manages the popup frame,
    // slide-in animation, and timing. It reads the SIS ID from the table
    // at 0x804a7b98[kind], which we pre-populated in InitSis.
    // Pass the remapped SIS table index (not the raw event kind) so
    // stadiumPrediction reads our custom entry, not a stadium name slot.
    int hud_frames = ev_chk->data->event->hud_display_frames;
    CityEvent_ShowHudText(CUSTOM_SIS_TABLE_OFFSET + idx, hud_frames);

    // Play secondary BGM if specified (pauses main BGM)
    if (custom_params[idx].bgm_file != 0)
        BGM_PlaySecondaryFile(custom_params[idx].bgm_file);

    // Call custom start function if defined
    if (custom_functions[idx].start)
        custom_functions[idx].start(ev_chk);

    OSReport("[CustomEvents] Event %d started (SIS index %d)\n",
             ev_chk->cur_kind, SIS_CITYTRIAL_ENTRY_COUNT + idx);
}

// State 2 wrapper: active phase.
// For custom events: call custom active, end when duration expires.
// For vanilla events: delegate to original handler.
static void CustomEvent_State2Wrapper(EventCheckData *ev_chk)
{
    if (ev_chk->cur_kind < EVKIND_NUM)
    {
        orig_state2(ev_chk);
        return;
    }

    int idx = ev_chk->cur_kind - EVKIND_NUM;

    // Call custom active function if defined
    if (custom_functions[idx].active)
        custom_functions[idx].active(ev_chk);

    // Check duration and end
    if ((int)ev_chk->timer >= custom_params[idx].duration)
    {
        // Transition to state 3 (cleanup)
        ev_chk->state = 3;
        ev_chk->timer = 0;

        if (custom_params[idx].is_siren && custom_params[idx].sky_preset != -1)
            Sky_RestoreGlobal();
    }
}

// State 3 wrapper: cleanup phase.
// For custom events: wait for cleanup delay, call end/end2, reset.
// For vanilla events: delegate to original handler.
static void CustomEvent_State3Wrapper(EventCheckData *ev_chk)
{
    if (ev_chk->cur_kind < EVKIND_NUM)
    {
        orig_state3(ev_chk);
        return;
    }

    int idx = ev_chk->cur_kind - EVKIND_NUM;

    // Call custom end function each frame (gradual cleanup)
    if (custom_functions[idx].end)
        custom_functions[idx].end(ev_chk);

    // Wait for cleanup delay
    int cleanup_delay = ev_chk->data->event->cleanup_delay;
    if ((int)ev_chk->timer < cleanup_delay)
        return;

    // Call custom end2 function (one-time final cleanup)
    if (custom_functions[idx].end2)
        custom_functions[idx].end2(ev_chk);

    // Stop secondary BGM and resume main BGM
    if (custom_params[idx].bgm_file != 0)
        BGM_StopSecondary();

    // Calculate new random delay and reset to idle
    int delay_min = ev_chk->data->event->delay_min;
    int delay_max = ev_chk->data->event->delay_max;
    int delay = delay_min + HSD_Randi(delay_max - delay_min + 1);

    ev_chk->event_time = delay;
    ev_chk->state = 0;
    ev_chk->cur_kind = -1;
    ev_chk->timer = 0;

    OSReport("[CustomEvents] Cleanup complete, next delay = %d frames\n", delay);
}

// Replaces the Gm_Roll(chance_arr, 16) call inside CityEvent_Decide at 0x800ee098.
// Extends the roll to include custom events alongside vanilla events.
// If a custom event wins the roll, triggers it via CustomEvent_Do and returns -1
// to vanilla code (which interprets -1 as "no event selected, set new delay").
static int CustomEvents_ExtendedRoll(int *chance_arr, int count)
{
    // Sum vanilla weights (already filtered by gate + history + once-only)
    int vanilla_total = 0;
    for (int i = 0; i < count; i++)
        vanilla_total += chance_arr[i];

    // Sum custom event weights (filtered by installed weight filter if any)
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
        // Vanilla event won - delegate to Gm_Roll for proper weighted selection
        int result = Gm_Roll(chance_arr, count);
        OSReport("[CustomEvents] ExtendedRoll: vanilla event %d (roll=%d, vanilla=%d, custom=%d)\n",
                 result, roll, vanilla_total, custom_total);
        return result;
    }

    // Custom event won - find which one
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
            // CustomEvent_Do failed (e.g. another event active) - fall back to vanilla
            OSReport("[CustomEvents] ExtendedRoll: custom event %d failed, falling back to vanilla\n", kind);
            return Gm_Roll(chance_arr, count);
        }
    }

    // Should not reach here
    return Gm_Roll(chance_arr, count);
}

// Exported API instance.
static CustomEventsAPI api = {
    .Do = CustomEvent_Do,
    .params = custom_params,
    .event_count = CUSTOM_EVENT_COUNT,
    .SetWeightFilter = SetWeightFilter,
};

void CustomEvents_OnBoot(void)
{
    // Replace state handler function pointers in the dispatch table.
    StateHandler *state_table = (StateHandler *)stc_event_state_table;

    orig_state1 = state_table[1];
    orig_state2 = state_table[2];
    orig_state3 = state_table[3];

    state_table[1] = CustomEvent_State1Wrapper;
    state_table[2] = CustomEvent_State2Wrapper;
    state_table[3] = CustomEvent_State3Wrapper;

    // Replace Gm_Roll call in CityEvent_Decide with extended roll
    // that includes custom events in the selection pool.
    CODEPATCH_REPLACECALL(0x800ee098, CustomEvents_ExtendedRoll);

    // Per-event persistent patches (Scale Change's camera-distance shim).
    ScaleChange_InstallHooks();

    // Export API for other mods to use
    Hoshi_ExportMod(&api);

    OSReport("[CustomEvents] Hooks installed\n");
}

int CustomEvent_Do(int kind)
{
    if (kind < EVKIND_NUM || kind >= CUSTOM_EVKIND_NUM)
        return 0;

    // Check if event system is initialized
    if (!stc_eventcheck_gobj || !*stc_eventcheck_gobj)
        return 0;

    GOBJ *g = *stc_eventcheck_gobj;
    EventCheckData *ev_chk = g->userdata;

    // Ensure no event is currently active
    if (ev_chk->state != 0)
        return 0;

    int idx = kind - EVKIND_NUM;

    // Run custom check function if defined
    if (custom_functions[idx].check && !custom_functions[idx].check(ev_chk))
        return 0;

    // Start the event (enters state 1 - starting/siren phase)
    ev_chk->state = 1;
    ev_chk->cur_kind = kind;
    ev_chk->timer = 0;

    // Siren, music fade, sky transition for siren events
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

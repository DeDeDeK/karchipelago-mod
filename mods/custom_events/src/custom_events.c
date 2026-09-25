#include <string.h>

#include "game.h"
#include "os.h"
#include "hsd.h"
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

#define CUSTOM_EVENT_COUNT (CUSTOM_EVKIND_NUM - EVKIND_NUM)

// SisCitytrial.dat's entry count; the custom announcements are appended after it.
#define SIS_CITYTRIAL_ENTRY_COUNT 42
#define SIS_ID_VANILLA_COUNT (EVKIND_NUM + STKIND_NUM)

_Static_assert(SIS_CITYTRIAL_ENTRY_COUNT + CUSTOM_EVENT_COUNT <= 128,
               "the game reads event SIS ids as a signed byte");

// The wrappers call these in the vanilla lifecycle order.
typedef struct CustomEventDesc
{
    const char *label;
    const char *hud_text;
    int duration;         // frames in state 2
    int sky_preset;       // -1 = keep the current sky
    int bgm_file;         // secondary BGM file index
    int weight;           // natural roll weight, 0 = never rolled
    void (*start)(void);  // state 1 -> 2
    void (*active)(void); // every frame of state 2, optional
    void (*end)(void);    // every frame of state 3, optional
    void (*end2)(void);   // once, when state 3 finishes
    void (*abort)(void);  // scene exited before end2; must not touch GObjs. Optional
} CustomEventDesc;

static const CustomEventDesc events[CUSTOM_EVENT_COUNT] = {
    [CUSTOM_EVKIND_WADDLE_DEE_SWARM - EVKIND_NUM] = {
        .label = "Waddle Dee Swarm",
        .hud_text = "Waddle Dee swarm incoming!",
        .duration = 1800,
        .sky_preset = 5,  // Dark Vignette
        .bgm_file = 0x34, // event_supercharge
        .weight = 20,
        .start = WaddleDeeSwarm_Start,
        .active = WaddleDeeSwarm_Active,
        .end2 = WaddleDeeSwarm_End2,
    },
    [CUSTOM_EVKIND_GRAVITY_CHANGE - EVKIND_NUM] = {
        .label = "Gravity Change",
        .hud_text = "Gravity is changing!",
        .duration = 900,
        .sky_preset = 8,  // Pink Sky
        .bgm_file = 0x31, // event_meteo
        .weight = 20,
        .start = GravityChange_Start,
        .end2 = GravityChange_End2,
        .abort = GravityChange_End2,
    },
    [CUSTOM_EVKIND_SCALE_CHANGE - EVKIND_NUM] = {
        .label = "Scale Change",
        .hud_text = "The world is growing!",
        .duration = 900,
        .sky_preset = 3,  // Dusk 2
        .bgm_file = 0x32, // event_monster
        .weight = 20,
        .start = ScaleChange_Start,
        .active = ScaleChange_Active,
        .end = ScaleChange_End,
        .end2 = ScaleChange_End2,
        .abort = ScaleChange_Abort,
    },
    [CUSTOM_EVKIND_GOURMET_RACE - EVKIND_NUM] = {
        .label = "Gourmet Race",
        .hud_text = "Gourmet Race!",
        .duration = 3600,
        .sky_preset = -1,
        .bgm_file = 0x34, // event_supercharge
        .weight = 20,
        .start = GourmetRace_Start,
        .end2 = GourmetRace_End2,
    },
};

// The events[] index of the custom event in states 1-3, -1 = none.
static int running_idx = -1;

static int sis_id_table[SIS_ID_VANILLA_COUNT + CUSTOM_EVENT_COUNT];
static u8 custom_sis_text[CUSTOM_EVENT_COUNT][128];
static void *extended_sis_ptrs[SIS_CITYTRIAL_ENTRY_COUNT + CUSTOM_EVENT_COUNT];

// The lis/addi r3 pairs that load stc_event_sis_id_table, in CityEvent_HudPredictionShow
// (0x80127624), CityEvent_HudPredictionThink (0x801276c0) and stadiumPrediction (0x80127864).
static const int sis_id_table_loads[][2] = {
    {0x80127660, 0x80127664},
    {0x80127794, 0x8012779c},
    {0x801278cc, 0x801278d4},
};

// Vanilla data follows the 40-entry table, so the custom ids go in a copy the
// game's three readers are repointed at.
static void RelocateSisIdTable(void)
{
    for (int i = 0; i < SIS_ID_VANILLA_COUNT; i++)
        sis_id_table[i] = stc_event_sis_id_table[i];
    for (int i = 0; i < CUSTOM_EVENT_COUNT; i++)
        sis_id_table[SIS_ID_VANILLA_COUNT + i] = SIS_CITYTRIAL_ENTRY_COUNT + i;

    u32 addr = (u32)sis_id_table;
    int lis = 0x3c600000 | (((addr + 0x8000) >> 16) & 0xffff); // lis r3,addr@ha
    int addi = 0x38630000 | (addr & 0xffff);                    // addi r3,r3,addr@l
    for (int i = 0; i < (int)(sizeof(sis_id_table_loads) / sizeof(sis_id_table_loads[0])); i++)
    {
        CODEPATCH_REPLACEINSTRUCTION(sis_id_table_loads[i][0], lis);
        CODEPATCH_REPLACEINSTRUCTION(sis_id_table_loads[i][1], addi);
    }
}

static void ComposeSisText(u8 *buf, int size, const char *str)
{
    static const u8 open[] = {
        TEXTCMD_ALIGNLEFT, TEXTCMD_FIT, TEXTCMD_KERNING,
        TEXTCMD_COLOR, 0xbb, 0xbb, 0xbb,
        TEXTCMD_SCALE, 0x00, 0xb3, 0x00, 0xb3, // ~0.70
    };
    static const u8 close[] = {
        TEXTCMD_LINEBREAK, TEXTCMD_SCALEEND, TEXTCMD_COLOREND, TEXTCMD_KERNINGEND,
        TEXTCMD_FITEND, TEXTCMD_ALIGNLEFTEND, TEXTCMD_TERMINATE,
    };

    u8 *p = buf;
    u8 *glyph_end = buf + size - sizeof(close);
    memcpy(p, open, sizeof(open));
    p += sizeof(open);

    for (; *str && p + 2 <= glyph_end; str++)
    {
        // A space is a command, not a glyph code.
        if (*str == ' ')
        {
            *p++ = TEXTCMD_SPACE;
            continue;
        }

        int cmd = Text_CharToCommand(*str);
        if (cmd == -1)
            continue;
        *p++ = (cmd >> 8) & 0xff;
        *p++ = cmd & 0xff;
    }

    memcpy(p, close, sizeof(close));
}

void CustomEvents_InitSis(void)
{
    // Every 3D scene reloads slot 0 with SisCitytrial.dat before On3DLoadEnd.
    void **original = (void **)stc_sis_data[0];

    for (int i = 0; i < SIS_CITYTRIAL_ENTRY_COUNT; i++)
        extended_sis_ptrs[i] = original[i];
    for (int i = 0; i < CUSTOM_EVENT_COUNT; i++)
        extended_sis_ptrs[SIS_CITYTRIAL_ENTRY_COUNT + i] = custom_sis_text[i];

    stc_sis_data[0] = (SISData *)extended_sis_ptrs;
}

typedef void (*StateHandler)(EventCheckData *);
static StateHandler orig_state1;
static StateHandler orig_state2;
static StateHandler orig_state3;

// Vanilla kinds go to the original handlers. Custom kinds never reach them, since
// they index 16-entry per-kind arrays (occurrence_count, the prev_kind history,
// stc_event_function, the per-kind start sound).
static void CustomEvent_State1Wrapper(EventCheckData *ev_chk)
{
    if (ev_chk->cur_kind < EVKIND_NUM)
    {
        orig_state1(ev_chk);
        return;
    }

    if (ev_chk->timer < ev_chk->data->event->starting_delay)
        return;

    int idx = ev_chk->cur_kind - EVKIND_NUM;
    ev_chk->state = 2;
    ev_chk->timer = 0;

    // stadiumPrediction looks the text up as sis_id_table[arg].
    CityEvent_ShowHudText(SIS_ID_VANILLA_COUNT + idx, ev_chk->data->event->hud_display_frames);
    BGM_PlaySecondaryFile(events[idx].bgm_file);
    events[idx].start();
}

static void CustomEvent_State2Wrapper(EventCheckData *ev_chk)
{
    if (ev_chk->cur_kind < EVKIND_NUM)
    {
        orig_state2(ev_chk);
        return;
    }

    const CustomEventDesc *desc = &events[ev_chk->cur_kind - EVKIND_NUM];
    if (desc->active)
        desc->active();

    if (ev_chk->timer < desc->duration)
        return;

    ev_chk->state = 3;
    ev_chk->timer = 0;

    // What CityEvent_EndWithSkyRestore (0x800ee660) does for a siren event.
    Gm_FadeInMusic(ev_chk->data->event->cleanup_delay);
    if (desc->sky_preset != -1)
        Sky_RestoreGlobal();
}

static void CustomEvent_State3Wrapper(EventCheckData *ev_chk)
{
    if (ev_chk->cur_kind < EVKIND_NUM)
    {
        orig_state3(ev_chk);
        return;
    }

    const CustomEventDesc *desc = &events[ev_chk->cur_kind - EVKIND_NUM];
    if (desc->end)
        desc->end();

    if (ev_chk->timer < ev_chk->data->event->cleanup_delay)
        return;

    desc->end2();
    BGM_StopSecondary();

    int delay_min = ev_chk->data->event->delay_min;
    int delay_max = ev_chk->data->event->delay_max;
    int delay = delay_min + HSD_Randi(delay_max - delay_min + 1);

    ev_chk->event_time = delay;
    ev_chk->state = 0;
    ev_chk->cur_kind = -1;
    ev_chk->timer = 0;
    running_idx = -1;

    OSReport("[CustomEvents] %s ended, next event in %d frames\n", desc->label, delay);
}

static int CustomEvent_Do(int kind)
{
    if (kind < EVKIND_NUM || kind >= CUSTOM_EVKIND_NUM)
        return 0;

    // NULL when the round has City Trial events turned off.
    GOBJ *g = *stc_eventcheck_gobj;
    if (!g)
        return 0;

    EventCheckData *ev_chk = g->userdata;
    if (ev_chk->state != 0)
        return 0;

    int idx = kind - EVKIND_NUM;
    ev_chk->state = 1;
    ev_chk->cur_kind = kind;
    ev_chk->timer = 0;
    running_idx = idx;

    // The siren intro CityEvent_Decide plays for a vanilla pick.
    Gm_FadeOutMusic(ev_chk->data->event->music_fadeout_frames);
    SFX_PlayFullVolume(EVENT_SIREN_SFX);
    if (events[idx].sky_preset != -1)
        Sky_TransitionGlobal(events[idx].sky_preset);

    OSReport("[CustomEvents] %s triggered\n", events[idx].label);
    return 1;
}

// Replaces the bl Gm_Roll(chance_arr, EVKIND_NUM) at 0x800ee098 in CityEvent_Decide
// (0x800edcf8), whose weights are already filtered by history and once-only. A
// custom pick starts its event and returns -1, which Decide treats as "no event"
// and answers by only setting a new delay.
static int CustomEvents_ExtendedRoll(int *chance_arr, int count)
{
    int vanilla_total = 0;
    for (int i = 0; i < count; i++)
        vanilla_total += chance_arr[i];

    int custom_total = 0;
    for (int i = 0; i < CUSTOM_EVENT_COUNT; i++)
        custom_total += events[i].weight;

    if (vanilla_total + custom_total == 0)
        return -1;

    int roll = HSD_Randi(vanilla_total + custom_total);
    if (roll < vanilla_total)
        return Gm_Roll(chance_arr, count);

    roll -= vanilla_total;
    for (int i = 0; i < CUSTOM_EVENT_COUNT; i++)
    {
        roll -= events[i].weight;
        if (roll < 0)
        {
            CustomEvent_Do(EVKIND_NUM + i);
            break;
        }
    }
    return -1;
}

// Vanilla has no cleanup for an event the scene exit cuts off (the EventCheckData
// destructor is a bare HSD_Free), so end2 never runs for it.
void CustomEvents_On3DExit(void)
{
    if (running_idx < 0)
        return;

    const CustomEventDesc *desc = &events[running_idx];
    if (desc->abort)
        desc->abort();
    running_idx = -1;

    OSReport("[CustomEvents] %s was cut off by the scene exit\n", desc->label);
}

static CustomEventsAPI api = {
    .Do = CustomEvent_Do,
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
    RelocateSisIdTable();

    for (int i = 0; i < CUSTOM_EVENT_COUNT; i++)
        ComposeSisText(custom_sis_text[i], sizeof(custom_sis_text[i]), events[i].hud_text);

    ScaleChange_InstallHooks();

    Hoshi_ExportMod(&api);

    OSReport("[CustomEvents] Hooks installed\n");
}

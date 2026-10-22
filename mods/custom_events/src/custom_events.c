#include <string.h>

#include "game.h"
#include "os.h"
#include "hsd.h"
#include "text.h"
#include "audio.h"
#include "stage.h"
#include "inline.h"
#include "code_patch/code_patch.h"
#include "hoshi/mod.h"

#include "custom_events.h"
#include "event_waddle_dee_swarm.h"
#include "event_gravity_change.h"
#include "event_scale_change.h"
#include "event_gourmet_race.h"
#include "event_planet.h"
#include "event_giant_tiny.h"
#include "event_truck_storm.h"
#include "event_cannon.h"
#include "event_minefield.h"
#include "event_danger_rain.h"
#include "event_jumping_items.h"
#include "event_stat_shuffle.h"
#include "event_mini_ufos.h"
#include "event_treasure_rain.h"
#include "event_bomb_tacs.h"
#include "event_drive_backwards.h"
#include "event_reverse_controls.h"
#include "event_sky_rings.h"
#include "player_scale.h"

#define CUSTOM_EVENT_COUNT (CUSTOM_EVKIND_NUM - EVKIND_NUM)

_Static_assert(SIS_CITYTRIAL_NUM + CUSTOM_EVENT_COUNT <= 128,
               "the game reads event SIS ids as a signed byte");

typedef struct CustomEventDesc
{
    const char *label;
    const char *hud_text;
    int duration;           // frames in state 2
    int sky_preset;         // -1 = keep the current sky
    BGMKind bgm_file;       // secondary BGM
    int weight;             // natural roll weight, 0 = never rolled
    int (*can_start)(void); // whether the event can run this round, optional
    void (*start)(void);    // state 1 -> 2
    void (*active)(void);   // every frame of state 2, optional
    void (*end)(void);      // every frame of state 3, optional
    void (*end2)(void);     // once, when state 3 finishes, optional
    void (*abort)(void);    // scene exited before end2; must not touch GObjs. Optional
} CustomEventDesc;

static const CustomEventDesc events[CUSTOM_EVENT_COUNT] = {
    [CUSTOM_EVKIND_WADDLE_DEE_SWARM - EVKIND_NUM] = {
        .label = "Waddle Dee Swarm",
        .hud_text = "Waddle Dee swarm incoming!",
        .duration = 1800,
        .sky_preset = CITYSKY_DARK_VIGNETTE,
        .bgm_file = BGM_EVENT_SUPERCHARGE,
        .weight = 20,
        .start = WaddleDeeSwarm_Start,
        .active = WaddleDeeSwarm_Active,
        .end2 = WaddleDeeSwarm_End2,
    },
    [CUSTOM_EVKIND_GRAVITY_CHANGE - EVKIND_NUM] = {
        .label = "Gravity Change",
        .hud_text = "Gravity is changing!",
        .duration = 900,
        .sky_preset = CITYSKY_PINK_SKY,
        .bgm_file = BGM_EVENT_METEO,
        .weight = 20,
        .start = GravityChange_Start,
        .end2 = GravityChange_End2,
        .abort = GravityChange_End2,
    },
    [CUSTOM_EVKIND_SCALE_CHANGE - EVKIND_NUM] = {
        .label = "Scale Change",
        .hud_text = "The world is growing!",
        .duration = 900,
        .sky_preset = CITYSKY_DUSK_2,
        .bgm_file = BGM_EVENT_MONSTER,
        .weight = 20,
        .start = ScaleChange_Start,
        .active = ScaleChange_Active,
        .end = ScaleChange_End,
        .end2 = ScaleChange_End2,
        .abort = PlayerScale_Forget,
    },
    [CUSTOM_EVKIND_GOURMET_RACE - EVKIND_NUM] = {
        .label = "Gourmet Race",
        .hud_text = "Gourmet Race!",
        .duration = 3600,
        .sky_preset = -1,
        .bgm_file = BGM_EVENT_SUPERCHARGE,
        .weight = 20,
        .start = GourmetRace_Start,
        .end2 = GourmetRace_End2,
    },
    [CUSTOM_EVKIND_PLANET - EVKIND_NUM] = {
        .label = "Planet",
        .hud_text = "A planet has appeared!",
        .duration = 3600,
        .sky_preset = CITYSKY_BLUE_SKY,
        .bgm_file = BGM_EVENT_METEO,
        .weight = 20,
        .start = Planet_Start,
        .end = Planet_End,
        .end2 = Planet_End2,
        .abort = Planet_Abort,
    },
    // Giant and Tiny Mode split one event's roll weight.
    [CUSTOM_EVKIND_GIANT_MODE - EVKIND_NUM] = {
        .label = "Giant Mode",
        .hud_text = "Everyone is Giant!",
        .duration = 900,
        .sky_preset = CITYSKY_DUSK,
        .bgm_file = BGM_EVENT_KYOSEKI,
        .weight = 10,
        .start = GiantTiny_StartGiant,
        .active = GiantTiny_Active,
        .end = GiantTiny_End,
        .end2 = GiantTiny_End2,
        .abort = PlayerScale_Forget,
    },
    [CUSTOM_EVKIND_TINY_MODE - EVKIND_NUM] = {
        .label = "Tiny Mode",
        .hud_text = "Everyone is Tiny!",
        .duration = 900,
        .sky_preset = CITYSKY_DAY_2,
        .bgm_file = BGM_EVENT_ITEMBOUND,
        .weight = 10,
        .start = GiantTiny_StartTiny,
        .active = GiantTiny_Active,
        .end = GiantTiny_End,
        .end2 = GiantTiny_End2,
        .abort = PlayerScale_Forget,
    },
    [CUSTOM_EVKIND_TRUCK_STORM - EVKIND_NUM] = {
        .label = "Truck Storm",
        .hud_text = "Waddle Dee trucks incoming!",
        .duration = 1800,
        .sky_preset = CITYSKY_DUSKY_CLOUDS,
        .bgm_file = BGM_EVENT_GORDO,
        .weight = 20,
        .start = TruckStorm_Start,
        .active = TruckStorm_Active,
        .end = TruckStorm_End,
        .end2 = TruckStorm_End2,
        .abort = TruckStorm_End2,
    },
    [CUSTOM_EVKIND_CANNON - EVKIND_NUM] = {
        .label = "Cannon",
        .hud_text = "A cannon has appeared!",
        .duration = 1800,
        .sky_preset = CITYSKY_DAY_2,
        .bgm_file = BGM_EVENT_SUPERCHARGE,
        .weight = 20,
        .start = Cannon_Start,
        .end2 = Cannon_End2,
    },
    // The mines outlast the event and go off on their own fuses.
    [CUSTOM_EVKIND_MINEFIELD - EVKIND_NUM] = {
        .label = "Minefield",
        .hud_text = "A minefield has been laid!",
        .duration = 1800,
        .sky_preset = CITYSKY_GRAY_SKY,
        .bgm_file = BGM_EVENT_STATIONFIRE,
        .weight = 20,
        .start = Minefield_Start,
    },
    // Weapons outlast the event like the mines; meteors and dragons land within it.
    [CUSTOM_EVKIND_DANGER_RAIN - EVKIND_NUM] = {
        .label = "Danger Rain",
        .hud_text = "Danger Rain!",
        .duration = 1800,
        .sky_preset = CITYSKY_RED_VIGNETTE,
        .bgm_file = BGM_EVENT_METEO,
        .weight = 20,
        .start = DangerRain_Start,
        .active = DangerRain_Active,
        .end2 = DangerRain_End2,
        .abort = DangerRain_Abort,
    },
    [CUSTOM_EVKIND_JUMPING_ITEMS - EVKIND_NUM] = {
        .label = "Jumping Items",
        .hud_text = "Items are jumping!",
        .duration = 1800,
        .sky_preset = CITYSKY_PINK_SKY,
        .bgm_file = BGM_EVENT_ITEMBOUND,
        .weight = 20,
        .start = JumpingItems_Start,
        .active = JumpingItems_Active,
        .end2 = JumpingItems_End2,
    },
    // Own stats come back in state 3, with whatever was gained or lost on the borrowed ones.
    [CUSTOM_EVKIND_STAT_SHUFFLE - EVKIND_NUM] = {
        .label = "Stat Shuffle",
        .hud_text = "Player stats are swapped!",
        .duration = 1800,
        .sky_preset = CITYSKY_MIDNIGHT,
        .bgm_file = BGM_EVENT_SYOUKINKUBI,
        .weight = 20,
        .can_start = StatShuffle_CanStart,
        .start = StatShuffle_Start,
        .end = StatShuffle_End,
        .end2 = StatShuffle_End2,
        .abort = StatShuffle_Abort,
    },
    // The UFO event's sky and music. The UFOs fly off in state 3.
    [CUSTOM_EVKIND_MINI_UFOS - EVKIND_NUM] = {
        .label = "Mini UFOs",
        .hud_text = "Mini UFOs have appeared! Grab the items!",
        .duration = 3600,
        .sky_preset = CITYSKY_BLUE_SKY,
        .bgm_file = BGM_EVENT_STATIONFIRE,
        .weight = 20,
        .can_start = MiniUFOs_CanStart,
        .start = MiniUFOs_Start,
        .end = MiniUFOs_End,
        .end2 = MiniUFOs_End2,
        .abort = MiniUFOs_Abort,
    },
    // The rain's items outlast the event on their own lifetimes.
    [CUSTOM_EVKIND_TREASURE_RAIN - EVKIND_NUM] = {
        .label = "Treasure Rain",
        .hud_text = "Treasure Rain!",
        .duration = 1800,
        .sky_preset = CITYSKY_DUSK,
        .bgm_file = BGM_EVENT_ITEMBOUND,
        .weight = 20,
        .start = TreasureRain_Start,
        .active = TreasureRain_Active,
        .end = TreasureRain_End,
        .end2 = TreasureRain_End2,
    },
    // TAC's own sky and music. The bombs left go off on their fuses after the TACs leave.
    [CUSTOM_EVKIND_BOMB_TACS - EVKIND_NUM] = {
        .label = "Bomb TACs",
        .hud_text = "TACs are dropping bombs!",
        .duration = 3600,
        .sky_preset = CITYSKY_DARK_VIGNETTE,
        .bgm_file = BGM_EVENT_SYOUKINKUBI,
        .weight = 20,
        .start = BombTacs_Start,
        .active = BombTacs_Active,
        .end = BombTacs_End,
        .end2 = BombTacs_End2,
        .abort = BombTacs_Abort,
    },
    // Both end with state 2, as the sky and music go back.
    [CUSTOM_EVKIND_DRIVE_BACKWARDS - EVKIND_NUM] = {
        .label = "Drive Backwards",
        .hud_text = "Everyone is driving backwards!",
        .duration = 900,
        .sky_preset = CITYSKY_DARK_PURPLE,
        .bgm_file = BGM_EVENT_FOG,
        .weight = 20,
        .start = DriveBackwards_Start,
        .end = DriveBackwards_Stop,
        .abort = DriveBackwards_Stop,
    },
    [CUSTOM_EVKIND_REVERSE_CONTROLS - EVKIND_NUM] = {
        .label = "Reverse Controls",
        .hud_text = "Controls are reversed!",
        .duration = 900,
        .sky_preset = CITYSKY_NIGHT,
        .bgm_file = BGM_EVENT_TOUDAI,
        .weight = 20,
        .start = ReverseControls_Start,
        .end = ReverseControls_Stop,
        .abort = ReverseControls_Stop,
    },
    // The rings and their waiting items go in state 3.
    [CUSTOM_EVKIND_SKY_RINGS - EVKIND_NUM] = {
        .label = "Sky Rings",
        .hud_text = "Rings in the sky have appeared! Get the items!",
        .duration = 3600,
        .sky_preset = CITYSKY_BLUE_SKY,
        .bgm_file = BGM_EVENT_SUPERCHARGE,
        .weight = 20,
        .can_start = SkyRings_CanStart,
        .start = SkyRings_Start,
        .active = SkyRings_Active,
        .end = SkyRings_End,
        .end2 = SkyRings_End2,
    },
};

// The round's event GObj, NULL outside a City Trial round with events on. The game's own
// pointer is never cleared and outlives the round it belongs to.
static GOBJ *event_gobj;

// The events[] index of the custom event in states 1-3, -1 = none.
static int running_idx = -1;

static int sis_id_table[EVENT_SIS_ID_NUM + CUSTOM_EVENT_COUNT];
static u8 custom_sis_text[CUSTOM_EVENT_COUNT][128];
static SISEntry extended_sis_ptrs[SIS_CITYTRIAL_NUM + CUSTOM_EVENT_COUNT];

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
    memcpy(sis_id_table, stc_event_sis_id_table, EVENT_SIS_ID_NUM * sizeof(int));
    for (int i = 0; i < CUSTOM_EVENT_COUNT; i++)
        sis_id_table[EVENT_SIS_ID_NUM + i] = SIS_CITYTRIAL_NUM + i;

    for (int i = 0; i < GetElementsIn(sis_id_table_loads); i++)
        CODEPATCH_REPLACEADDRESS(sis_id_table_loads[i][0], sis_id_table_loads[i][1], sis_id_table);
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
    memcpy(p, open, sizeof(open));
    p = Text_WriteSisString(p + sizeof(open), buf + size - sizeof(close), str);
    memcpy(p, close, sizeof(close));
}

void CustomEvents_OnCityLoad(void)
{
    event_gobj = *stc_eventcheck_gobj;

    // Every 3D scene reloads slot 0 with SisCitytrial.dat before On3DLoadEnd.
    Text_ExtendSis(0, extended_sis_ptrs, SIS_CITYTRIAL_NUM);
    for (int i = 0; i < CUSTOM_EVENT_COUNT; i++)
        extended_sis_ptrs[SIS_CITYTRIAL_NUM + i] = custom_sis_text[i];
}

static EventStateFn orig_state1;
static EventStateFn orig_state2;
static EventStateFn orig_state3;

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
    CityEvent_ShowHudText(EVENT_SIS_ID_NUM + idx, ev_chk->data->event->hud_display_frames);
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

    if (desc->end2)
        desc->end2();
    BGM_StopSecondary();

    ev_chk->event_time = RandomInRange(ev_chk->data->event->delay_min, ev_chk->data->event->delay_max);
    ev_chk->state = 0;
    ev_chk->cur_kind = -1;
    ev_chk->timer = 0;
    running_idx = -1;

    OSReport("[CustomEvents] %s ended, next event in %d frames\n", desc->label, ev_chk->event_time);
}

static int CustomEvent_CanStart(const CustomEventDesc *desc)
{
    return !desc->can_start || desc->can_start();
}

static int CustomEvent_Do(int kind)
{
    if (kind < EVKIND_NUM || kind >= CUSTOM_EVKIND_NUM || !event_gobj)
        return 0;

    EventCheckData *ev_chk = event_gobj->userdata;
    int idx = kind - EVKIND_NUM;
    if (ev_chk->state != 0 || !CustomEvent_CanStart(&events[idx]))
        return 0;

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

    int weight[CUSTOM_EVENT_COUNT];
    int custom_total = 0;
    for (int i = 0; i < CUSTOM_EVENT_COUNT; i++)
    {
        weight[i] = CustomEvent_CanStart(&events[i]) ? events[i].weight : 0;
        custom_total += weight[i];
    }

    if (vanilla_total + custom_total == 0)
        return -1;
    if (HSD_Randi(vanilla_total + custom_total) < vanilla_total)
        return Gm_Roll(chance_arr, count);

    CustomEvent_Do(EVKIND_NUM + Gm_Roll(weight, CUSTOM_EVENT_COUNT));
    return -1;
}

// Vanilla has no cleanup for an event the scene exit cuts off (the EventCheckData
// destructor is a bare HSD_Free), so end2 never runs for it.
void CustomEvents_On3DExit(void)
{
    event_gobj = NULL;
    Cannon_On3DExit();

    if (running_idx < 0)
        return;

    const CustomEventDesc *desc = &events[running_idx];
    if (desc->abort)
        desc->abort();
    running_idx = -1;

    OSReport("[CustomEvents] %s was cut off by the scene exit\n", desc->label);
}

static int CustomEvent_DoDangerRain(int rain)
{
    if (rain < 0 || rain >= DANGER_RAIN_NUM)
        return 0;

    DangerRain_SetNext(rain);
    if (CustomEvent_Do(CUSTOM_EVKIND_DANGER_RAIN))
        return 1;
    DangerRain_SetNext(-1);
    return 0;
}

static CustomEventsAPI api = {
    .Do = CustomEvent_Do,
    .DoDangerRain = CustomEvent_DoDangerRain,
};

void CustomEvents_OnBoot(void)
{
    orig_state1 = stc_event_state_table[1];
    orig_state2 = stc_event_state_table[2];
    orig_state3 = stc_event_state_table[3];

    stc_event_state_table[1] = CustomEvent_State1Wrapper;
    stc_event_state_table[2] = CustomEvent_State2Wrapper;
    stc_event_state_table[3] = CustomEvent_State3Wrapper;

    CODEPATCH_REPLACECALL(0x800ee098, CustomEvents_ExtendedRoll);
    RelocateSisIdTable();

    for (int i = 0; i < CUSTOM_EVENT_COUNT; i++)
        ComposeSisText(custom_sis_text[i], sizeof(custom_sis_text[i]), events[i].hud_text);

    PlayerScale_InstallHooks();
    Planet_InstallHooks();
    TruckStorm_InstallHooks();
    Cannon_InstallHooks();
    Minefield_InstallHooks();
    DangerRain_InstallHooks();
    BombTacs_InstallHooks();
    DriveBackwards_InstallHooks();
    ReverseControls_InstallHooks();

    Hoshi_ExportMod(&api);

    OSReport("[CustomEvents] Hooks installed\n");
}

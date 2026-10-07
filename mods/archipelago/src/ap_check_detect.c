#include "game.h"
#include "os.h"
#include "scene.h"
#include "stage.h"
#include "stadium.h"
#include "rider.h"
#include "item.h"
#include "hurt.h"
#include "weapon.h"
#include "event.h"
#include "machine.h"
#include "yakumono.h"
#include "topride.h"
#include "code_patch/code_patch.h"
#include "hoshi/func.h"

#include "inline.h"

#include "main.h"
#include "ap_check_detect.h"
#include "gate_ap_star.h"
#include "patch_cap.h"
#include "gate_topride_items.h"

// Objectives latched this boot, one bit per APCheckKind. Cross-boot ones read
// ap_save->checks.
static u64 ap_observed[2];
_Static_assert(APCK_NUM <= 128, "ap_observed is two u64 words");

static int Observed(int ck)
{
    return (ap_observed[ck >> 6] >> (ck & 63)) & 1ULL;
}

void APCheckDetect_Observe(int ck)
{
    if (ck < 0 || ck >= APCK_NUM || Observed(ck))
        return;
    ap_observed[ck >> 6] |= 1ULL << (ck & 63);
    OSReport("[APCheckDetect] Objective %d achieved\n", ck);
}

// hoshi's Ply_GetMachineKindAbs folds a custom star slot onto the bike range.
static MachineKind PlyMachineKind(int ply)
{
    return CustomMachines_ResolveKind(cm_api, Ply_GetMachineIsBike(ply), Ply_GetMachineKind(ply));
}

#define MACHINE_ANY     -1
// The Archipelago Star's MachineKind is whatever the registry handed it this boot.
#define MACHINE_AP_STAR -2

static int MachineMatches(int want, MachineKind mk)
{
    if (want == MACHINE_ANY)
        return 1;
    if (want == MACHINE_AP_STAR)
        want = GateApStar_MachineKind();
    return want >= 0 && mk == want;
}

// Targets of the objectives backed by ap_save->checks.
#define AP_ALLUP_TOTAL_NEED    5
#define AP_PURPLE_SR1_NEED     3
#define AP_RACE_COLOR_MASK_ALL ((1 << KIRBYCOLOR_NUM) - 1)
#define AP_TR_COURSE_MASK_ALL  ((1 << TOPRIDE_NUM) - 1)
#define AP_AR_COURSE_MASK_ALL  ((1 << AIRRIDE_NUM) - 1)
#define AP_DRAG_RACE_NUM       4
#define AP_DRAG_MASK_ALL       ((1 << AP_DRAG_RACE_NUM) - 1)
// Slot 21 never spawns; a give of it records as slot 12, the Party Ball a race hands out.
#define AP_TR_ITEM_MASK_ALL    ((1u << TRITEM_PARTY_BALL) - 1)

// Sets bit n of a cross-boot progress mask; 0 when it was already set.
#define LATCH_BIT(mask, n) (((mask) & (1u << (n))) ? 0 : ((mask) |= (1u << (n)), 1))

int APCheckDetect_IsSet(int ck)
{
    switch (ck)
    {
    case APCK_ALLUPS_5:           return ap_save->checks.allup_collect_total >= AP_ALLUP_TOTAL_NEED;
    case APCK_SR1_PURPLE_3X:      return ap_save->checks.purple_sr1_wins >= AP_PURPLE_SR1_NEED;
    case APCK_AIRRIDE_ALL_COLORS: return ap_save->checks.race_color_mask == AP_RACE_COLOR_MASK_ALL;
    case APCK_TR_ALL_COLORS:      return ap_save->checks.tr_color_mask == AP_RACE_COLOR_MASK_ALL;
    case APCK_TR_ALL_COURSES_STEER: return ap_save->checks.tr_steer_win_mask == AP_TR_COURSE_MASK_ALL;
    case APCK_DRAG_ALL_1ST:       return ap_save->checks.drag_win_mask == AP_DRAG_MASK_ALL;
    case APCK_AIRRIDE_ALL_COURSES_1ST: return ap_save->checks.ar_course_win_mask == AP_AR_COURSE_MASK_ALL;
    case APCK_TR_EVERY_ITEM:      return ap_save->checks.tr_item_mask == AP_TR_ITEM_MASK_ALL;
    default:
        if (ck < 0 || ck >= APCK_NUM)
            return 0;
        return Observed(ck);
    }
}

typedef struct FootVisitCheck
{
    u8 ck;
    Vec3 pos;
    float radius;
} FootVisitCheck;

static const FootVisitCheck foot_visit_checks[] = {
    // The flower's platform is tiny, so the sphere is tight.
    { APCK_CASTLE_FLOWER,   { 408.7f, 370.8f, -564.6f },  2.0f },
    { APCK_MODEL_CITY,      { -422.7f, 12.7f, -168.9f }, 10.0f },
    { APCK_VOLCANO_FLOWER,  { -107.0f, 205.1f, -847.3f },  5.0f },
    // The roof, not the whole garden.
    { APCK_SKY_GARDEN_TOP,  { -67.9f, 463.8f, -0.3f },    5.0f },
    // In front of Whispy's face, which looks down +Z, and above the ceiling of the model
    // city room under the forest.
    { APCK_WHISPY_WOODS,    { -445.2f, 35.7f, -59.9f },   3.0f },
};

#define FOOT_VISIT_NUM ((int)GetElementsIn(foot_visit_checks))

// The rotating head: within 30.5 of the tower's axis, floor Y 133.7 to 142.9. The tower
// below is all wall.
#define AP_LIGHTHOUSE_AXIS_X     191.8f
#define AP_LIGHTHOUSE_AXIS_Z     291.2f
#define AP_LIGHTHOUSE_TOP_RADIUS 31.0f
#define AP_LIGHTHOUSE_TOP_MIN_Y  132.0f
#define AP_LIGHTHOUSE_TOP_MAX_Y  146.0f

// The ledge under the waterwheel's axle and the ramp down from it to the river, between
// the wheel's rims. A machine counts too.
#define AP_WATERWHEEL_MIN_X     -342.2f
#define AP_WATERWHEEL_MAX_X     -301.4f
#define AP_WATERWHEEL_RAMP_Z     602.0f
#define AP_WATERWHEEL_LEDGE_Z    636.7f
#define AP_WATERWHEEL_WALL_Z     649.0f
#define AP_WATERWHEEL_RAMP_Y     -21.0f
#define AP_WATERWHEEL_LEDGE_Y     -3.9f
#define AP_WATERWHEEL_HEIGHT      12.0f

// On the descent of Fantasy Meadows' shortcut arc, wide enough for the line a machine at
// speed flies and still 26 units clear of the racing line 40-60 units below. Sampled in
// every Air Ride mode.
static const Vec3 meadows_shortcut_pos = { 249.7f, 120.0f, 19.2f };

#define AP_MEADOWS_SHORTCUT_RADIUS 25.0f

static int WithinSphere(const Vec3 *p, const Vec3 *centre, float radius)
{
    return VECSquareDistance((Vec3 *)p, (Vec3 *)centre) <= radius * radius;
}

// Under the city ceiling (Y 1040.3), far above the sky garden (464), the highest place
// reachable without flying.
#define AP_MAX_ALTITUDE_Y 1000.0f

// 0.10 seconds at 60fps.
#define AP_PHOTO_FINISH_FRAMES 6

#define AP_FEET_PER_METRE (1.0f / 0.3048f)

// A checklist time MM:SS:00 in frames at 60fps.
#define AP_MMSS_FRAMES(m, s) (((m) * 60 + (s)) * 60)

// Every threshold below is quoted in its cell label and AP location name, so changing one
// is a two-repo edit.

#define AP_NEBULA_FEET_NEED        5500.0f
#define AP_NEBULA_2LAP_FRAMES      AP_MMSS_FRAMES(2, 30)
#define AP_NEBULA_2LAP_FAST_FRAMES AP_MMSS_FRAMES(2, 6)
// 10 seconds at 60fps.
#define AP_NEBULA_AIR_FRAMES       600

// Time Attack finish and Free Run best-lap objectives.
typedef struct TimedRunCheck
{
    u8 ck;
    u8 mode;        // AIRRIDEMODE_TIME or AIRRIDEMODE_FREE
    u8 gr_kind;
    short machine;  // MachineKind, MACHINE_ANY or MACHINE_AP_STAR
    int frames;
} TimedRunCheck;

static const TimedRunCheck timed_run_checks[] = {
    { APCK_NEBULA_TA,           AIRRIDEMODE_TIME, GR_SPACE2,   MACHINE_ANY,           AP_MMSS_FRAMES(3, 35) },
    { APCK_NEBULA_TA_FAST,      AIRRIDEMODE_TIME, GR_SPACE2,   MACHINE_ANY,           AP_MMSS_FRAMES(3, 10) },
    { APCK_NEBULA_TA_HYDRA,     AIRRIDEMODE_TIME, GR_SPACE2,   VCKIND_HYDRA,          AP_MMSS_FRAMES(3, 15) },
    { APCK_NEBULA_FR,           AIRRIDEMODE_FREE, GR_SPACE2,   MACHINE_ANY,           AP_MMSS_FRAMES(1, 15) },
    { APCK_NEBULA_FR_FAST,      AIRRIDEMODE_FREE, GR_SPACE2,   MACHINE_ANY,           AP_MMSS_FRAMES(1, 3) },
    { APCK_NEBULA_FR_WARP,      AIRRIDEMODE_FREE, GR_SPACE2,   VCKIND_WARP,           AP_MMSS_FRAMES(1, 10) },
    { APCK_VALLEY_FR_AP_STAR,   AIRRIDEMODE_FREE, GR_VALLEY2,  MACHINE_AP_STAR,       AP_MMSS_FRAMES(1, 0) },
    { APCK_CHECKER_TA_FLIGHT,   AIRRIDEMODE_TIME, GR_CHECK2,   VCKIND_FLIGHT,         AP_MMSS_FRAMES(4, 0) },
    { APCK_PASSAGE_FR_COMPACT,  AIRRIDEMODE_FREE, GR_MACHINE2, VCKIND_COMPACT,        AP_MMSS_FRAMES(1, 5) },
    { APCK_FROZEN_TA_WHEELIE,   AIRRIDEMODE_TIME, GR_ICE1,     VCKIND_WHEELIEBIKE,    AP_MMSS_FRAMES(3, 0) },
    { APCK_SANDS_TA_FLIGHT,     AIRRIDEMODE_TIME, GR_DESERT1,  VCKIND_FLIGHT,         AP_MMSS_FRAMES(2, 50) },
    // Meta Knight only ever rides his own wing.
    { APCK_MAGMA_TA_METAKNIGHT, AIRRIDEMODE_TIME, GR_HEAT2,    VCKIND_WINGMETAKNIGHT, AP_MMSS_FRAMES(3, 15) },
};

#define TIMED_RUN_NUM ((int)GetElementsIn(timed_run_checks))

// item_collect is zeroed every 3D load, so it counts the run, permanent patches included.
typedef struct RunItemCheck
{
    u8 ck;
    u8 it_kind;
    u8 need;
} RunItemCheck;

// A box break bumps item_collect like a pickup.
static const RunItemCheck run_item_checks[] = {
    { APCK_HP_PATCHES_10,      ITKIND_HP,            10 },
    { APCK_OFFENSE_PATCHES_10, ITKIND_OFFENSE,       10 },
    { APCK_BOX_BLUE_20,        ITKIND_BOXBLUE,       20 },
    { APCK_BOX_GREEN_10,       ITKIND_BOXGREEN,      10 },
    { APCK_BOX_RED_10,         ITKIND_BOXRED,        10 },
    { APCK_FOOD_ICECREAM,      ITKIND_FOODICECREAM,   3 },
    { APCK_FOOD_RICEBALL,      ITKIND_FOODRICEBALL,   3 },
    { APCK_FOOD_CHICKEN,       ITKIND_FOODCHICKEN,    3 },
    { APCK_FOOD_CURRY,         ITKIND_FOODCURRY,      3 },
    { APCK_FOOD_RAMEN,         ITKIND_FOODRAMEN,      3 },
    { APCK_FOOD_OMELET,        ITKIND_FOODOMELET,     3 },
    { APCK_FOOD_HAMBURGER,     ITKIND_FOODHAMBURGER,  3 },
    { APCK_FOOD_APPLE,         ITKIND_FOODAPPLE,      3 },
};

#define RUN_ITEM_NUM ((int)GetElementsIn(run_item_checks))

static int prev_allup[PLY_NUM];

// Items each human has picked up out of Dyna Blade's drops this City Trial round.
static u8 dynablade_items[4];

#define AP_DYNABLADE_ITEMS_NEED 5

// One bit per MachineKind each human has ridden this City Trial round.
static u64 machines_ridden[4];

#define AP_MACHINES_RIDDEN_NEED 5
#define AP_COPY_KINDS_NEED      6
#define AP_CITY_AIRBORNE_FRAMES 1200 // 20 seconds

// Coral placed by the loaded stage, sampled once at load (0 outside City Trial),
// and how much of it anyone has broken this round.
static int coral_total;
static int coral_broken;

// Is a City Trial trial round loaded?
static int in_city_trial;

// Kirbys KO'd by each human King Dedede this Destruction Derby game.
static int dedede_kirby_kos[PLY_NUM];

#define AP_DEDEDE_KIRBY_KO_NEED 10

// Enemies each human defeated mid-Mic-blast this KIRBY MELEE round.
static int mic_enemy_kos[PLY_NUM];
static int in_kirby_melee;

#define AP_MIC_ENEMY_KO_NEED    10
#define AP_MELEE_DEDEDE_KO_NEED 30

// King Dedede's slot in his own stadium, the victim Ply_AddDeath stamps
// king_dedede_ko_frame for.
#define AP_VSKD_BOSS_PLY 4

// Humans KO'd this round, and humans hurt in VS. KING DEDEDE this round.
static u8 knocked_out[4];
static u8 vskd_hurt[4];

#define AP_DD_UNHURT_KO_NEED 5

// busted and riding: a MachineKind or MACHINE_AP_STAR.
typedef struct BustCheck
{
    u8 ck;
    short busted;
    short riding;
} BustCheck;

static const BustCheck bust_checks[] = {
    { APCK_BUST_REX_ON_SCOOTER,    VCKIND_REXWHEELIE, VCKIND_WHEELIESCOOTER },
    { APCK_BUST_WINGED_ON_FLIGHT,  VCKIND_WINGED,     VCKIND_FLIGHT },
    { APCK_BUST_SHADOW_ON_AP_STAR, VCKIND_SHADOW,     MACHINE_AP_STAR },
};

#define BUST_NUM ((int)GetElementsIn(bust_checks))

// A Trial's closing stadium keeps CITYMODE_TRIAL, so the mode alone misses it.
static int InStadium(void)
{
    return Scene_GetCurrentMajor() == MJRKIND_CITY && CityTrial_IsInStadium();
}

// GR_PLANTS1..GR_ICE1, or -1 outside Air Ride. Latched at load: its objectives are sampled
// after the round.
static int ar_course;

// Judged at the line: the hazard counters keep running under the finished-racer autopilot.
static u8 course_clean[4];

#define AP_RUNAMOK_FEET_NEED  1000.0f
#define AP_SAMEITEM_NEED      20
#define AP_BOUNCE_ITEMS_NEED  11 // "over 10"
#define AP_FAKE_PATCHES_NEED  5
#define AP_METEOR_MIN_FRAMES  60

// PlayerStats distance units in feet, by the factor vanilla's mileage cells use.
#define AP_DIST_UNIT_FEET (11.4285717f * 5280.0f / 160934.4f)

// Midpoint of each GrCity1 station's two Rail Fire fires. Every fire is within 52 units of
// its own and the closest two stations are 556 apart, so the nearest names a burn's
// station.
static const Vec3 rail_stations[] = {
    { -514.6f, 120.9f, -398.3f },
    {  463.8f,  70.2f, -561.2f },
    {  471.1f,  29.7f,   -6.3f },
    {  252.1f,  22.4f,  683.7f },
    { -468.2f,  16.2f,  456.1f },
};

#define RAIL_STATION_NUM  ((int)GetElementsIn(rail_stations))
#define RAIL_STATIONS_ALL ((1 << RAIL_STATION_NUM) - 1)

#define LIGHTHOUSE_STATE_LIT 3

// What each human has done during the City Trial event running now. Cleared whenever the
// active event changes, so one run of an event is one attempt.
typedef struct EventRun
{
    int kind;               // the active EventKind these belong to, -1 for none
    float dist_start;       // Run Amok: distance travelled when it began
    u8 stations;            // Rail Fire: one bit per station burned at
    u8 lights;              // Lighthouse: one bit per light stood under
    u8 items;               // Bounce: pickups; Fake Powerups: good patches
    u8 touched_fake;        // Fake Powerups
    u8 hurt;                // Meteor: took damage on a machine or on foot
    u16 frames;             // Meteor: frames sampled
    float onfoot_dmg_start; // Meteor: PlayerStats.onfoot_dmg_total when it began
    u8 same_item[ITKIND_NUM]; // Same Item: pickups per kind
} EventRun;

static EventRun event_runs[4];

// Which stadium a Prediction named when it guessed at random, -1 when it named the
// real one or none was made this trial.
static int predicted_stadium;

static float PlyDistance(int ply)
{
    PlayerStats *st = Ply_GetStats(ply);
    return st->total_distance_grounded + st->total_distance_airborne;
}

// City Trial rounds only: stc_eventcheck_gobj still points at the last round's GObj
// between scenes.
static EventRun *SyncEventRun(int ply)
{
    EventRun *ev = &event_runs[ply];
    int kind = CityEvent_GetActiveKind();
    if (ev->kind == kind)
        return ev;

    // No event starts over a running one, so leaving Fake Powerups means it ran its course.
    if (ev->kind == EVKIND_FAKEPOWERUPS && !ev->touched_fake &&
        ev->items >= AP_FAKE_PATCHES_NEED)
        APCheckDetect_Observe(APCK_EVENT_FAKE_NONE);
    // A meteor plan that fails to allocate ends the event on its first frame.
    if (ev->kind == EVKIND_METEOR && !ev->hurt && ev->frames >= AP_METEOR_MIN_FRAMES)
        APCheckDetect_Observe(APCK_EVENT_METEOR_NO_DAMAGE);

    memset(ev, 0, sizeof(*ev));
    ev->kind = kind;
    if (kind == EVKIND_RUNAMOK)
        ev->dist_start = PlyDistance(ply);
    if (kind == EVKIND_METEOR)
        ev->onfoot_dmg_start = Ply_GetStats(ply)->onfoot_dmg_total;
    return ev;
}

// The game's own beam test, run per light: CityLighthouse_InBeam's caller stops at the
// first light that holds the point and never says which.
static int UnderLighthouseLight(GOBJ *lighthouse, int light, const Vec3 *pos)
{
    YakumonoData *yd = lighthouse->userdata;
    typeof(yd->data_ptr->lighthouse) lp = yd->data_ptr->lighthouse;
    int joint = lp->light_joints[light];

    Vec3 origin, dir, end;
    Gr_GetNodeWorldPos(joint, &origin);
    Gr_GetNodeWorldAxisY(joint, &dir);
    VEC_NormalizeAndSnap(&dir, &dir);
    end.X = origin.X - dir.X * lp->beam_len;
    end.Y = origin.Y - dir.Y * lp->beam_len;
    end.Z = origin.Z - dir.Z * lp->beam_len;
    return CityLighthouse_InBeam((Vec3 *)pos, &origin, &end, lp->beam_slope, lp->beam_base);
}

static void SampleEvent(RiderData *rd, EventRun *ev)
{
    switch (ev->kind)
    {
    case EVKIND_RUNAMOK:
        if ((PlyDistance(rd->ply) - ev->dist_start) * AP_DIST_UNIT_FEET > AP_RUNAMOK_FEET_NEED)
            APCheckDetect_Observe(APCK_EVENT_RUNAMOK_DIST);
        break;

    // On foot has no HP; machine hits are caught by APCheckDetect_GiveDamage.
    case EVKIND_METEOR:
        if (ev->frames < 0xFFFF)
            ev->frames++;
        if (Ply_GetStats(rd->ply)->onfoot_dmg_total > ev->onfoot_dmg_start)
            ev->hurt = 1;
        break;

    case EVKIND_LIGHTHOUSE:
    {
        // Rewritten by this event's start, so current while the event is active.
        GOBJ *lh = *stc_lighthouse_gobj;
        if (!lh || YakumonoGObj_GetState(lh) != LIGHTHOUSE_STATE_LIT)
            break;
        YakumonoData *yd = lh->userdata;
        int light_num = yd->data_ptr->lighthouse->light_num;
        for (int k = 0; k < light_num && k < 8; k++)
        {
            if (UnderLighthouseLight(lh, k, &rd->pos))
                ev->lights |= 1 << k;
        }
        if (light_num > 0 && ev->lights == (1 << light_num) - 1)
            APCheckDetect_Observe(APCK_EVENT_LIGHTHOUSE_BOTH);
        break;
    }

    default:
        break;
    }
}

// The Mic's damage lands over the blast and its recovery, so both states count.
static int IsMidMicBlast(int ply)
{
    GOBJ *rg = Ply_GetRiderGObj(ply);
    if (!rg)
        return 0;

    RiderData *rd = rg->userdata;
    return rd->copy_kind == COPYKIND_MIKE &&
           (rd->status == RDSTATE_MIKESING || rd->status == RDSTATE_MIKEEND);
}

#define AP_SAME_COPY_NEED 3

// Every grant path appends to the history. A zeroed history reads COPYKIND_FIRE, so the
// entry count bounds the test.
static int SameCopyStreak(const PlayerStats *st)
{
    int n = COPY_HISTORY_NUM(st);
    if (n < AP_SAME_COPY_NEED)
        return 0;
    for (int i = n - AP_SAME_COPY_NEED; i < n - 1; i++)
    {
        if (st->copy_history[i] != st->copy_history[n - 1])
            return 0;
    }
    return 1;
}

// machine_mount_kind_num misses the starting machine and an assembled legendary, so the
// ridden kinds are sampled. Wing and Wheel Kirby are copy abilities.
static void RecordMachineRidden(RiderData *rd)
{
    if (!Rider_IsOnMachine(rd) || rd->machine_gobj == NULL)
        return;
    MachineData *md = rd->machine_gobj->userdata;
    MachineKind mk = CustomMachines_ResolveKind(cm_api, md->is_bike, md->kind);
    if (mk < 0 || mk >= 64 || mk == VCKIND_WINGKIRBY || mk == VCKIND_WHEELKIRBY)
        return;

    u64 *ridden = &machines_ridden[rd->ply];
    *ridden |= 1ULL << mk;
    if (Popcount64(*ridden) >= AP_MACHINES_RIDDEN_NEED)
        APCheckDetect_Observe(APCK_RIDE_5_MACHINES);
}

static int CopyKindsObtained(const PlayerStats *st)
{
    int n = 0;
    for (int k = 0; k < COPYKIND_NUM; k++)
    {
        if (st->copy_obtain_count[k] > 0)
            n++;
    }
    return n;
}

// Only the patch array is read: the timed Max items add to the stat ratio through arrays
// of their own.
static int AnyStatAtCap(const RiderData *rd)
{
    int cap = PatchCap_GetCap();
    for (int i = 0; i < PATCHKIND_NUM; i++)
    {
        if (PatchCap_IsStatAt(rd->stats.values, i, cap))
            return 1;
    }
    return 0;
}

static int UnderWaterwheel(const RiderData *rd)
{
    const Vec3 *p = &rd->pos;
    if (p->X < AP_WATERWHEEL_MIN_X || p->X > AP_WATERWHEEL_MAX_X
        || p->Z < AP_WATERWHEEL_RAMP_Z || p->Z > AP_WATERWHEEL_WALL_Z)
        return 0;

    float floor_y = AP_WATERWHEEL_LEDGE_Y;
    if (p->Z < AP_WATERWHEEL_LEDGE_Z)
        floor_y = AP_WATERWHEEL_RAMP_Y + (AP_WATERWHEEL_LEDGE_Y - AP_WATERWHEEL_RAMP_Y)
                * (p->Z - AP_WATERWHEEL_RAMP_Z) / (AP_WATERWHEEL_LEDGE_Z - AP_WATERWHEEL_RAMP_Z);
    return p->Y >= floor_y - 2.0f && p->Y <= floor_y + AP_WATERWHEEL_HEIGHT;
}

static int OnLighthouseTop(const RiderData *rd)
{
    if (!rd->is_grounded || rd->pos.Y < AP_LIGHTHOUSE_TOP_MIN_Y || rd->pos.Y > AP_LIGHTHOUSE_TOP_MAX_Y)
        return 0;
    float dx = rd->pos.X - AP_LIGHTHOUSE_AXIS_X;
    float dz = rd->pos.Z - AP_LIGHTHOUSE_AXIS_Z;
    return dx * dx + dz * dz <= AP_LIGHTHOUSE_TOP_RADIUS * AP_LIGHTHOUSE_TOP_RADIUS;
}

// GrCity1's rail ids: 0-9 the five station links, two per link (one per direction); 10-13
// the crater, volcano and jump-pad rails. 14-20, the river, is not a visible rail.
#define AP_CITY_STATION_LINKS 5
#define AP_CITY_GRIND_RAILS_SOLO 0x3C00

static int UsedEveryCityRail(const PlayerStats *st)
{
    u32 ids = st->rail_bits[0] | st->rail_bits[1] << 8;
    for (int link = 0; link < AP_CITY_STATION_LINKS; link++)
    {
        if (!(ids & (3u << (link * 2))))
            return 0;
    }
    return (ids & AP_CITY_GRIND_RAILS_SOLO) == AP_CITY_GRIND_RAILS_SOLO;
}

// Per-frame proc on each human rider during a City Trial round.
static void APCheckDetect_PerFrame(GOBJ *rg)
{
    RiderData *rd = rg->userdata;
    int ply = rd->ply;
    PlayerStats *st = Ply_GetStats(ply);

    for (int i = 0; i < RUN_ITEM_NUM; i++)
    {
        if (st->item_collect[run_item_checks[i].it_kind] >= (int)run_item_checks[i].need)
            APCheckDetect_Observe(run_item_checks[i].ck);
    }

    // flags_84d is zeroed with PlayerStats on scene load, so it is per round.
    if ((st->flags_84d & PLYSTATS_DRAGOON_ASSEMBLED) &&
        (st->flags_84d & PLYSTATS_HYDRA_ASSEMBLED) &&
        GateApStar_AssembledThisRound(ply))
        APCheckDetect_Observe(APCK_ASSEMBLE_ALL_LEGENDARY);

    // Counts across the whole save.
    int allup = st->item_collect[ITKIND_ALLUP];
    if (allup > prev_allup[ply] && ap_save->checks.allup_collect_total < AP_ALLUP_TOTAL_NEED)
    {
        ap_save->checks.allup_collect_total += (u16)(allup - prev_allup[ply]);
        OSReport("[APCheckDetect] All Ups collected: %d/%d\n",
                 ap_save->checks.allup_collect_total, AP_ALLUP_TOTAL_NEED);
    }
    prev_allup[ply] = allup;

    // Only the copy-wheel paths set copy_chance_mask.
    if (st->copy_chance_mask & COPY_CHANCE_BIT(COPYKIND_MIKE))
        APCheckDetect_Observe(APCK_MIC_COPY_CHANCE);

    if (SameCopyStreak(st))
        APCheckDetect_Observe(APCK_SAME_COPY_3X);

    if (CopyKindsObtained(st) >= AP_COPY_KINDS_NEED)
        APCheckDetect_Observe(APCK_COPY_6_KINDS);

    // The longest single stretch, which only a machine's airtime feeds.
    if (st->max_time_spent_airborne >= AP_CITY_AIRBORNE_FRAMES)
        APCheckDetect_Observe(APCK_AIRBORNE_20S);

    if (AnyStatAtCap(rd))
        APCheckDetect_Observe(APCK_MAX_A_STAT);

    if (rd->pos.Y >= AP_MAX_ALTITUDE_Y)
        APCheckDetect_Observe(APCK_MAX_ALTITUDE);

    if (ply < 4)
    {
        SampleEvent(rd, SyncEventRun(ply));
        RecordMachineRidden(rd);
    }

    if (UsedEveryCityRail(st))
        APCheckDetect_Observe(APCK_ALL_GRIND_RAILS);

    if (UnderWaterwheel(rd))
        APCheckDetect_Observe(APCK_UNDER_WATERWHEEL);

    if (!Rider_IsOnMachine(rd))
    {
        for (int i = 0; i < FOOT_VISIT_NUM; i++)
        {
            const FootVisitCheck *fv = &foot_visit_checks[i];
            if (WithinSphere(&rd->pos, &fv->pos, fv->radius))
                APCheckDetect_Observe(fv->ck);
        }
        if (OnLighthouseTop(rd))
            APCheckDetect_Observe(APCK_LIGHTHOUSE_TOP);
    }
}

// A Destruction Derby fall is a KO that never reaches Ply_AddDeath.
static void APCheckDetect_PerFrameDerby(GOBJ *rg)
{
    RiderData *rd = rg->userdata;
    if (Ply_CheckIfFallDead(rd->ply))
        APCheckDetect_OnKnockedOut(rd->ply);
}

static void APCheckDetect_PerFrameMeadows(GOBJ *rg)
{
    RiderData *rd = rg->userdata;

    if (WithinSphere(&rd->pos, &meadows_shortcut_pos, AP_MEADOWS_SHORTCUT_RADIUS))
        APCheckDetect_Observe(APCK_MEADOWS_SHORTCUT);
}

void APCheckDetect_On3DLoadEnd(void)
{
    coral_total = 0;
    coral_broken = 0;
    for (int i = 0; i < PLY_NUM; i++)
    {
        prev_allup[i] = 0;
        dedede_kirby_kos[i] = 0;
        mic_enemy_kos[i] = 0;
    }
    in_city_trial = 0;
    for (int i = 0; i < 4; i++)
    {
        memset(&event_runs[i], 0, sizeof(event_runs[i]));
        event_runs[i].kind = -1;
        dynablade_items[i] = 0;
        course_clean[i] = 0;
        machines_ridden[i] = 0;
        knocked_out[i] = 0;
        vskd_hurt[i] = 0;
    }

    StadiumKind st = Gm_GetCurrentStadiumKind();
    in_kirby_melee = InStadium() && (st == STKIND_MELEE1 || st == STKIND_MELEE2);

    // GameData.stage_kind is the menu's selection and goes stale outside a race.
    ar_course = -1;
    if (Scene_GetCurrentMajor() == MJRKIND_AIR && Gr_GetCurrentGrKind() <= GR_ICE1)
        ar_course = Gr_GetCurrentGrKind();

    // The attract demo has a CPU in every slot, but coral counts any breaker.
    if (Gm_IsAutoDemo())
        return;

    if (Scene_GetCurrentMajor() == MJRKIND_AIR)
    {
        if (Gr_GetCurrentGrKind() == GR_PLANTS1)
            OSReport("[APCheckDetect] Sampler attached to %d player(s) on Fantasy Meadows\n",
                     AP_AttachHumanRiderProcs(APCheckDetect_PerFrameMeadows));
        return;
    }

    // The stadium was decided before the trial's city loaded.
    if (InStadium() && Gm_GetCityMode() == CITYMODE_TRIAL)
    {
        if (predicted_stadium >= 0 && predicted_stadium != Gm_GetCurrentStadiumKind())
            APCheckDetect_Observe(APCK_EVENT_PREDICTION_WRONG);
        predicted_stadium = -1;
    }

    if (InStadium() && st >= STKIND_DESTRUCTION1 && st <= STKIND_DESTRUCTION5)
    {
        OSReport("[APCheckDetect] Sampler attached to %d player(s) in Destruction Derby\n",
                 AP_AttachHumanRiderProcs(APCheckDetect_PerFrameDerby));
        return;
    }

    // City Trial rounds only: "in one game" means one CT Trial run.
    if (!Gm_IsInCity() || Gm_GetCityMode() != CITYMODE_TRIAL)
        return;

    in_city_trial = 1;
    predicted_stadium = -1;
    coral_total = Gr_GetYakumonoSpawnTotal(YAKUKIND_CORAL);

    OSReport("[APCheckDetect] Sampler attached to %d player(s) in City Trial (coral total %d)\n",
             AP_AttachHumanRiderProcs(APCheckDetect_PerFrame), coral_total);
}

// The killer has to be on its machine as the KO lands.
static void ObserveBusts(int killer, MachineKind busted)
{
    GOBJ *rg = Ply_GetRiderGObj(killer);
    if (rg == NULL || !Rider_IsOnMachine(rg->userdata))
        return;

    MachineKind riding = PlyMachineKind(killer);
    for (int i = 0; i < BUST_NUM; i++)
    {
        if (MachineMatches(bust_checks[i].busted, busted) &&
            MachineMatches(bust_checks[i].riding, riding))
            APCheckDetect_Observe(bust_checks[i].ck);
    }
}

void APCheckDetect_OnKnockedOut(int ply)
{
    if (ply >= 0 && ply < 4)
        knocked_out[ply] = 1;
}

void APCheckDetect_AddDeath(int victim, DmgLog *dmg_log, int machine_kind)
{
    // Latched before the killer is vetted: a KO with no attacker reads a stale id or 5.
    APCheckDetect_OnKnockedOut(victim);

    int killer = dmg_log->attacker_ply;
    if (killer < 0 || killer >= 4 || killer == victim || Ply_GetPKind(killer) != PKIND_HMN)
        return;

    if (victim == AP_VSKD_BOSS_PLY)
    {
        if (InStadium() && Gm_GetCurrentStadiumKind() == STKIND_VSKINGDEDEDE)
        {
            if (Ply_GetRiderKind(killer) == RDKIND_METAKNIGHT)
                APCheckDetect_Observe(APCK_VSKD_METAKNIGHT_KO);
            if (!vskd_hurt[killer])
                APCheckDetect_Observe(APCK_VSKD_NO_DAMAGE);
        }
        return;
    }
    if (victim < 0 || victim >= 4)
        return;

    // The Panic Spin's hit comes from the spinning rider, which credits its own attack.
    if ((dmg_log->credited_attack & WP_ATTACK_CAUSE_MASK) == ATK_PANICSPIN)
        APCheckDetect_Observe(APCK_PANIC_SPIN_KO);

    if (in_city_trial)
    {
        // Fog has no logic of its own - the event is the sky preset alone.
        if (CityEvent_GetActiveKind() == EVKIND_FOG)
            APCheckDetect_Observe(APCK_EVENT_FOG_KO);
        ObserveBusts(killer, machine_kind);
        if (GateApStar_IsShotAttack(dmg_log->credited_attack) && Ply_GetPKind(victim) == PKIND_CPU)
            APCheckDetect_Observe(APCK_AP_STAR_SHOT_KO_CPU);
    }

    if (!Gm_IsDestructionDerby())
        return;
    if (Ply_GetRiderKind(killer) != RDKIND_DEDEDE || Ply_GetRiderKind(victim) != RDKIND_KIRBY)
        return;

    if (++dedede_kirby_kos[killer] >= AP_DEDEDE_KIRBY_KO_NEED)
        APCheckDetect_Observe(APCK_DD_DEDEDE_KO_KIRBY);
}

// Replaces the bl Ply_RecordEnemyDefeat at 0x802022ec in EventActor_ResolveHit
// (0x802021fc). The Mic has no enemy_defeat_by_method index, so the blast is read from the
// rider's state.
static void APCheckDetect_EnemyDefeat(int ply, void *attacker_log, GOBJ *enemy)
{
    Ply_RecordEnemyDefeat(ply, attacker_log, enemy);

    if (!in_kirby_melee)
        return;
    if (ply < 0 || ply >= 4 || Ply_GetPKind(ply) != PKIND_HMN)
        return;
    if (!IsMidMicBlast(ply))
        return;

    if (++mic_enemy_kos[ply] >= AP_MIC_ENEMY_KO_NEED)
        APCheckDetect_Observe(APCK_MIC_ENEMY_KOS);
}

// Replaces the bl Ply_IncrementYakumonoBreakCount at 0x80105da0 in
// YakumonoGObj_IncrementBreakCount (0x80105d80), which every break credits through. Coral
// counts any breaker, CPUs included.
static void APCheckDetect_YakumonoBreak(int ply, YakuKind kind)
{
    Ply_IncrementYakumonoBreakCount(ply, kind);

    if (kind != YAKUKIND_CORAL || coral_total <= 0)
        return;

    if (++coral_broken >= coral_total)
        APCheckDetect_Observe(APCK_BREAK_ALL_CORAL);
}

// Stadium_ComputeRank* skip slots whose gate byte is nonzero, leaving their
// placement and time stale.
static int SlotRecorded(const StadiumResults *r, int p)
{
    return Ply_GetPKind(p) != PKIND_NONE && r->rank_skip[p] == 0;
}

// Racers other than ply the rankers counted; with none, 1st place is free.
static int OpponentCount(const StadiumResults *r, int ply)
{
    int n = 0;
    for (int p = 0; p < 4; p++)
    {
        PKind k = Ply_GetPKind(p);
        if (p == ply || (k != PKIND_HMN && k != PKIND_CPU))
            continue;
        if (SlotRecorded(r, p))
            n++;
    }
    return n;
}

// A human within AP_PHOTO_FINISH_FRAMES of any other finisher, CPU or not.
static int PhotoFinish(const StadiumResults *r)
{
    for (int a = 0; a < 4; a++)
    {
        if (Ply_GetPKind(a) != PKIND_HMN)
            continue;
        if (!SlotRecorded(r, a) || !r->ply_finished[a] || r->ply_race_time[a] == 0)
            continue;
        for (int b = 0; b < 4; b++)
        {
            if (b == a || !SlotRecorded(r, b) || !r->ply_finished[b] || r->ply_race_time[b] == 0)
                continue;
            if (abs(r->ply_race_time[a] - r->ply_race_time[b]) <= AP_PHOTO_FINISH_FRAMES)
                return 1;
        }
    }
    return 0;
}

// Ply_GetDescColor is a KirbyColor only for a Kirby rider.
static void RecordRaceColor(int p)
{
    if (Ply_GetDescRiderKind(p) != RDKIND_KIRBY)
        return;

    int color = Ply_GetDescColor(p);
    if (color < 0 || color >= KIRBYCOLOR_NUM)
        return;

    if (!LATCH_BIT(ap_save->checks.race_color_mask, color))
        return;
    OSReport("[APCheckDetect] Air Ride race finished as %s (colors = %s)\n",
             KirbyColor_Names[color],
             MaskBits(ap_save->checks.race_color_mask, KIRBYCOLOR_NUM));
}

// The loaded course's cell that is the reverse of a vanilla one, or -1.
static int CourseAvoidanceCheck(void)
{
    switch (ar_course)
    {
    case GR_SKY2:    return APCK_BEANSTALK_FERRIS_LAPS;
    case GR_DESERT1: return APCK_SANDS_NO_QUICKSAND;
    case GR_CHECK2:  return APCK_CHECKER_NO_SPIN_PANELS;
    case GR_HEAT2:   return APCK_MAGMA_NO_BOOST_PANELS;
    default:         return -1;
    }
}

static int TouchedMagmaBoostPanel(const PlayerStats *st)
{
    const u8 *ref = GrMagma_GetBoostPanelRefMask();
    if (ref == NULL)
        return 0;
    for (int i = 0; i < (int)sizeof(st->zone_bits); i++)
    {
        if (st->zone_bits[i] & ref[i])
            return 1;
    }
    return 0;
}

// PlayerStats' per-race counters. laps_no_ferris is the longest run of laps without the
// wheel, so 0 means every lap rode it.
static int CourseHazardAvoided(int ply)
{
    const PlayerStats *st = Ply_GetStats(ply);
    switch (ar_course)
    {
    case GR_SKY2:    return st->laps_no_ferris == 0;
    case GR_DESERT1: return st->quicksand_entries == 0;
    case GR_CHECK2:  return st->spin_panel_uses == 0;
    case GR_HEAT2:   return !TouchedMagmaBoostPanel(st);
    default:         return 0;
    }
}

static void RecordAirRideCourseWin(int course)
{
    if (!LATCH_BIT(ap_save->checks.ar_course_win_mask, course))
        return;
    OSReport("[APCheckDetect] Air Ride course %d won (courses = %s)\n", course,
             MaskBits(ap_save->checks.ar_course_win_mask, AIRRIDE_NUM));
}

// Crossed the line, ranked 1st, and raced someone: the rankers rank racers who never
// finished too.
static int WonRace(const StadiumResults *r, int p)
{
    return r->ply_finished[p] && r->ply_placement[p] == 0 && OpponentCount(r, p) > 0;
}

static void SampleAirRide(const StadiumResults *r)
{
    for (int p = 0; p < 4; p++)
    {
        if (Ply_GetPKind(p) != PKIND_HMN || !SlotRecorded(r, p))
            continue;

        if (r->ply_finished[p])
            RecordRaceColor(p);

        int won = WonRace(r, p);

        if (won)
        {
            RiderKind rk = Ply_GetDescRiderKind(p);
            if (rk == RDKIND_METAKNIGHT)
                APCheckDetect_Observe(APCK_AIRRIDE_1ST_METAKNIGHT);
            else if (rk == RDKIND_DEDEDE)
                APCheckDetect_Observe(APCK_AIRRIDE_1ST_DEDEDE);
            if (MachineMatches(MACHINE_AP_STAR, PlyMachineKind(p)))
                APCheckDetect_Observe(APCK_AIRRIDE_1ST_AP_STAR);
            if (ar_course >= 0)
                RecordAirRideCourseWin(ar_course);
            int ck = CourseAvoidanceCheck();
            if (ck >= 0 && course_clean[p])
                APCheckDetect_Observe(ck);
        }

        if (ar_course != GR_SPACE2)
            continue;

        if (won)
        {
            APCheckDetect_Observe(APCK_NEBULA_1ST);
            if (PlyMachineKind(p) == VCKIND_WHEELIESCOOTER)
                APCheckDetect_Observe(APCK_NEBULA_1ST_SCOOTER);
        }

        // AirRide_CheckRaceDistanceObjectives' gates: a timed race set to 2 minutes.
        if (Gm_GetCityKind() == AIRRIDE_RULE_TIME &&
            Gm_GetRaceTimeLimitSeconds() == 120 &&
            Gm_GetPlayerRaceDistance(p) * AP_FEET_PER_METRE >= AP_NEBULA_FEET_NEED)
            APCheckDetect_Observe(APCK_NEBULA_DIST_2MIN);

        // The configured lap total, as AirRide_CheckRaceLapObjectives reads it.
        if (Gm_GetCityKind() == AIRRIDE_RULE_LAPS && Gm_GetRaceLapTotal() == 2 &&
            r->ply_race_time[p] != 0)
        {
            if (r->ply_race_time[p] <= AP_NEBULA_2LAP_FRAMES)
                APCheckDetect_Observe(APCK_NEBULA_2LAP_TIME);
            if (r->ply_race_time[p] <= AP_NEBULA_2LAP_FAST_FRAMES)
                APCheckDetect_Observe(APCK_NEBULA_2LAP_FAST);
        }

        // PlayerStats is zeroed only on the next 3D load, so this is still the race's
        // longest stretch.
        MachineKind mk = PlyMachineKind(p);
        if ((mk == VCKIND_DRAGOON || mk == VCKIND_FLIGHT || mk == VCKIND_WINGED) &&
            Ply_GetStats(p)->max_time_spent_airborne > AP_NEBULA_AIR_FRAMES)
            APCheckDetect_Observe(APCK_NEBULA_AIRBORNE);
    }
}

static void RecordDragWin(int drag)
{
    if (!LATCH_BIT(ap_save->checks.drag_win_mask, drag))
        return;
    OSReport("[APCheckDetect] DRAG RACE %d won (drag races = %s)\n", drag + 1,
             MaskBits(ap_save->checks.drag_win_mask, AP_DRAG_RACE_NUM));
}

static void SampleStadium(const StadiumResults *r, StadiumKind st)
{
    if (st >= STKIND_DRAG1 && st <= STKIND_DRAG4)
    {
        if (PhotoFinish(r))
            APCheckDetect_Observe(APCK_DRAG_PHOTO);
        for (int p = 0; p < 4; p++)
        {
            if (Ply_GetPKind(p) == PKIND_HMN && SlotRecorded(r, p) && WonRace(r, p))
                RecordDragWin(st - STKIND_DRAG1);
        }
        return;
    }

    for (int p = 0; p < 4; p++)
    {
        if (Ply_GetPKind(p) != PKIND_HMN || !SlotRecorded(r, p))
            continue;

        if (st >= STKIND_SINGLERACE1 && st <= STKIND_SINGLERACE9)
        {
            if (!WonRace(r, p))
                continue;
            APCheckDetect_Observe(APCK_SR1_FIRST + (st - STKIND_SINGLERACE1));
            if (st != STKIND_SINGLERACE1)
                continue;
            if (PlyMachineKind(p) == VCKIND_BULK)
                APCheckDetect_Observe(APCK_SR1_BULK);
            if (Ply_GetDescRiderKind(p) == RDKIND_KIRBY &&
                Ply_GetDescColor(p) == KIRBYCOLOR_PURPLE &&
                ap_save->checks.purple_sr1_wins < AP_PURPLE_SR1_NEED)
            {
                ap_save->checks.purple_sr1_wins++;
                OSReport("[APCheckDetect] Purple Kirby SINGLE RACE 1 wins: %d/%d\n",
                         ap_save->checks.purple_sr1_wins, AP_PURPLE_SR1_NEED);
            }
        }
        else if (st == STKIND_HIGHJUMP)
        {
            if (r->ply_dist[p] * AP_FEET_PER_METRE > 1500.0f)
                APCheckDetect_Observe(APCK_HIGHJUMP_1500);
        }
        else if (st == STKIND_AIRGLIDER)
        {
            if (r->ply_dist[p] * AP_FEET_PER_METRE > 2000.0f)
                APCheckDetect_Observe(APCK_AIRGLIDER_2000);
        }
        else if (st == STKIND_MELEE1 || st == STKIND_MELEE2)
        {
            int kos = r->ply_points[p];
            if (st == STKIND_MELEE1 && kos > 100)
                APCheckDetect_Observe(APCK_MELEE1_100);
            if (st == STKIND_MELEE2 && kos > 60)
                APCheckDetect_Observe(APCK_MELEE2_60);
            if (kos >= AP_MELEE_DEDEDE_KO_NEED &&
                Ply_GetDescRiderKind(p) == RDKIND_DEDEDE)
                APCheckDetect_Observe(APCK_MELEE_DEDEDE_KO_30);
        }
        else if (st >= STKIND_DESTRUCTION1 && st <= STKIND_DESTRUCTION5)
        {
            // ply_points is GameData.destruction_derby_ko_num here, the same field
            // the vanilla DD cells count against.
            if (st == STKIND_DESTRUCTION3 && r->ply_points[p] >= 10)
                APCheckDetect_Observe(APCK_DD3_KO_10);
            if (r->ply_points[p] >= AP_DD_UNHURT_KO_NEED && !knocked_out[p])
                APCheckDetect_Observe(APCK_DD_KO_5_UNHURT);
        }
    }
}

void APCheckDetect_On3DExit(void)
{
    GameData *gd = Gm_GetGameData();

    in_city_trial = 0;

    // Stadium_ExitMinor skips the results latch for a replay, leaving the previous
    // round's values in the block.
    if (Gm_IsReplay())
        return;

    if (InStadium())
    {
        SampleStadium(&gd->stadium_results, Gm_GetCurrentStadiumKind());
    }
    else if (Scene_GetCurrentMajor() == MJRKIND_AIR)
    {
        if (Gm_GetAirRideMode() != AIRRIDEMODE_RACE)
            return;
        if (PhotoFinish(&gd->stadium_results))
            APCheckDetect_Observe(APCK_AIRRIDE_PHOTO);
        SampleAirRide(&gd->stadium_results);
    }
}

// Unlike the vanilla dispatchers, no Net_IsSessionActive bail.
static int TimedRunCounts(int ply, AirRideMode mode)
{
    return !Gm_IsReplay() && Scene_GetCurrentMajor() == MJRKIND_AIR &&
           Ply_GetPKind(ply) == PKIND_HMN && Gm_GetAirRideMode() == mode;
}

static void SampleTimedRun(int ply, AirRideMode mode, int frames)
{
    if (frames == 0)
        return;

    GroundKind gr = Gr_GetCurrentGrKind();
    MachineKind mk = PlyMachineKind(ply);
    for (int i = 0; i < TIMED_RUN_NUM; i++)
    {
        const TimedRunCheck *c = &timed_run_checks[i];
        if (c->mode == mode && c->gr_kind == gr && frames <= c->frames &&
            MachineMatches(c->machine, mk))
            APCheckDetect_Observe(c->ck);
    }
}

// Replaces the bl AirRide_DispatchFreeRunObjectives at 0x80010418 in AirRide_OnLapComplete
// (0x800101f4), which runs once per completed lap, after the lap is folded into the
// best-lap time.
static void APCheckDetect_FreeRunLap(int ply)
{
    AirRide_DispatchFreeRunObjectives(ply);
    if (TimedRunCounts(ply, AIRRIDEMODE_FREE))
        SampleTimedRun(ply, AIRRIDEMODE_FREE, Gm_GetPlayerBestLapTime(ply));
}

// Replaces the bl AirRide_DispatchRaceTimeAttackObjectives at 0x80010d68 in
// race3D_isFinished (0x80010bbc), which runs as a player crosses the line with the finish
// time latched.
static void APCheckDetect_RaceFinish(int ply)
{
    AirRide_DispatchRaceTimeAttackObjectives(ply);
    if (TimedRunCounts(ply, AIRRIDEMODE_TIME))
        SampleTimedRun(ply, AIRRIDEMODE_TIME, Gm_GetPlayerFinishTime(ply));
    if (ply < 4 && TimedRunCounts(ply, AIRRIDEMODE_RACE))
        course_clean[ply] = (u8)CourseHazardAvoided(ply);
}

// Replaces the bl Ply_IncrementItemCollectNum at 0x801db928 in Machine_OnTouchItem
// (0x801db34c), which every pickup and box break passes through with its kind folded to
// the vanilla base and its spawn_type as src_tag. The AP Patch kinds skip the call.
static void APCheckDetect_ItemCollect(int ply, ItemKind kind, int src_tag)
{
    Ply_IncrementItemCollectNum(ply, kind, src_tag);

    if (!in_city_trial || ply < 0 || ply >= 4 || Ply_GetPKind(ply) != PKIND_HMN)
        return;
    if (kind <= ITKIND_BOXRED || kind >= ITKIND_NUM)
        return;

    // The last stop's items outlive the UFO by a few frames; src_tag keeps out the ring
    // spawners whose spawn_type comes from stage data.
    if (kind == ITKIND_ALLUP && src_tag == ITSPAWN_UFO && CityEvent_GetCurrentKind() == EVKIND_UFO)
        APCheckDetect_Observe(APCK_EVENT_UFO_ALLUP);

    if (src_tag == ITSPAWN_DYNABLADE && dynablade_items[ply] < 255 &&
        ++dynablade_items[ply] >= AP_DYNABLADE_ITEMS_NEED)
        APCheckDetect_Observe(APCK_EVENT_DYNABLADE_ITEMS);

    EventRun *ev = SyncEventRun(ply);
    switch (ev->kind)
    {
    case EVKIND_SAMEITEM:
        if (ev->same_item[kind] < 255 && ++ev->same_item[kind] >= AP_SAMEITEM_NEED)
            APCheckDetect_Observe(APCK_EVENT_SAMEITEM_20);
        break;

    case EVKIND_BOUNCE:
        if (ev->items < 255 && ++ev->items >= AP_BOUNCE_ITEMS_NEED)
            APCheckDetect_Observe(APCK_EVENT_BOUNCE_ITEMS);
        break;

    case EVKIND_FAKEPOWERUPS:
        // Archipelago items spawn ITSPAWN_DIRECT and neither fail nor pad the run.
        if (src_tag == ITSPAWN_DIRECT)
            break;
        if (kind >= ITKIND_ACCELFAKE && kind <= ITKIND_WEIGHTFAKE)
            ev->touched_fake = 1;
        else if (CityItem_IsGoodPatch(kind) && ev->items < 255)
            ev->items++;
        break;

    default:
        break;
    }
}

// Replaces the bl Ply_PlayRailFireHitSFX at 0x801d741c in Machine_ActOnHitCollision
// (0x801d7308) and 0x80196668 in Rider_ActOnHitCollision (0x8019655c), the one thing
// either does for a Rail Fire station's hit.
static void APCheckDetect_RailFireHit(int ply)
{
    Ply_PlayRailFireHitSFX(ply);

    if (!in_city_trial || ply < 0 || ply >= 4 || Ply_GetPKind(ply) != PKIND_HMN)
        return;
    GOBJ *rg = Ply_GetRiderGObj(ply);
    if (!rg)
        return;

    EventRun *ev = SyncEventRun(ply);
    if (ev->kind != EVKIND_RAILFIRE)
        return;

    RiderData *rd = rg->userdata;
    int nearest = 0;
    float nearest_d2 = 0.0f;
    for (int i = 0; i < RAIL_STATION_NUM; i++)
    {
        float d2 = VECSquareDistance(&rd->pos, (Vec3 *)&rail_stations[i]);
        if (i == 0 || d2 < nearest_d2)
        {
            nearest = i;
            nearest_d2 = d2;
        }
    }

    if (ev->stations & (1 << nearest))
        return;
    ev->stations |= 1 << nearest;
    OSReport("[APCheckDetect] Player %d burned at rail station %d (stations = %s)\n",
             ply + 1, nearest, MaskBits(ev->stations, RAIL_STATION_NUM));
    if (ev->stations == RAIL_STATIONS_ALL)
        APCheckDetect_Observe(APCK_EVENT_RAILFIRE_ALL);
}

// Replaces both bl Machine_GiveDamage in Machine_DmgApply (0x801c6834), with and without
// knockback, which every machine hit passes through. A knockback-only hit passes 0.
static void APCheckDetect_GiveDamage(MachineData *md, float damage, int *hit)
{
    Machine_GiveDamage(md, damage, hit);

    if (damage <= 0.0f)
        return;
    int ply = Machine_GetRiderPly(md);
    if (ply < 0 || ply >= 4 || Ply_GetPKind(ply) != PKIND_HMN)
        return;

    if (in_city_trial)
    {
        // Synced first, so a hit on the event's first frame survives the run's reset.
        EventRun *ev = SyncEventRun(ply);
        if (ev->kind == EVKIND_METEOR)
            ev->hurt = 1;
    }
    else if (InStadium() && Gm_GetCurrentStadiumKind() == STKIND_VSKINGDEDEDE)
    {
        vskd_hurt[ply] = 1;
    }
}

static int HumanRides(MachineData *md)
{
    int ply = Machine_GetRiderPly(md);
    return ply >= 0 && ply < 4 && Ply_GetPKind(ply) == PKIND_HMN;
}

// Runs as a bump commits, before either hit reaction takes a formation machine out of its
// flight slot.
static void APCheckDetect_MachineBump(MachineData *self, MachineData *other)
{
    if (!in_city_trial)
        return;
    if ((self->formation_slot != MACHINE_FORMATION_NONE && HumanRides(other)) ||
        (other->formation_slot != MACHINE_FORMATION_NONE && HumanRides(self)))
        APCheckDetect_Observe(APCK_EVENT_FORMATION_BUMP);
}

// Hook at 0x801dacb4 in Machine_CheckMachineBumpCollision (0x801daac4). r30 = the machine
// running the check; the clobbered mr r3, r28 (the other one) re-runs after.
CODEPATCH_HOOKCREATE(0x801dacb4,
    "mr 3, 30\n\t"
    "mr 4, 28\n\t",
    APCheckDetect_MachineBump,
    "",
    0
)

// Replaces the bl HSD_Randi(24) at 0x801279a0 in stadiumPrediction (0x80127864), the
// one-in-five arm that names any StadiumKind. Judged once the trial's stadium loads, since
// a random pick can still land on it.
static int APCheckDetect_PredictRandom(int n)
{
    int st = HSD_Randi(n);
    if (Gm_GetCityMode() == CITYMODE_TRIAL)
        predicted_stadium = st;
    return st;
}

#define AP_TR_PHOTO_FINISH_FRAMES 12 // 0.20 seconds
#define AP_TR_EVEN_LAP_FRAMES     60 // 1 second

// The CPU Level byte of a level-5 CPU, the value vanilla's per-course CPU cells test.
#define AP_TR_CPU_LEVEL_5 4

#define AP_TR_FREEZE_VICTIMS_NEED 3
#define AP_TR_HITS_NEED           5
#define AP_TR_SPEEDDOWN_NEED      3

// Top Ride Time Attack finishes and Free Run laps.
typedef struct TopRideTimedCheck
{
    u8 ck;
    u8 mode;    // TOPRIDEMODE_TIME or TOPRIDEMODE_FREE
    u8 course;  // TopRideCourse
    u8 machine; // TopRideMachineKind
    int frames;
} TopRideTimedCheck;

static const TopRideTimedCheck tr_timed_checks[] = {
    { APCK_TR_TA_GRASS_STEER, TOPRIDEMODE_TIME, TOPRIDE_GRASS, TR_MACHINE_STEER, AP_MMSS_FRAMES(0, 33) },
    { APCK_TR_TA_METAL_STEER, TOPRIDEMODE_TIME, TOPRIDE_METAL, TR_MACHINE_STEER, AP_MMSS_FRAMES(0, 57) },
    { APCK_TR_FR_SKY_STEER,   TOPRIDEMODE_FREE, TOPRIDE_SKY,   TR_MACHINE_STEER, AP_MMSS_FRAMES(0, 11) },
};

#define TR_TIMED_NUM ((int)GetElementsIn(tr_timed_checks))

#define TR_ABILITY_ITEMS_ALL ((1 << TR_ABILITY_ITEM_NUM) - 1)

// What each kirby slot has done this round. Only the state vtable is kept for CPUs,
// which is what finds a rival being frozen.
typedef struct TopRideRun
{
    void *prev_vt;       // state vtable at the last sample
    int laps;            // lap_progress at the last sample
    int lap_min;         // Time Attack: fastest and slowest lap so far
    int lap_max;
    u8 lap_num;          // Time Attack: laps timed
    u8 judged;           // the finish has been judged
    u8 burned;
    u8 ant_doom;
    u8 falls;
    u8 grinded;
    u8 hits;             // hit reactions entered
    u8 ability_items;    // bit per topride_ability_items entry applied
    u8 speed_downs;
    u8 fan_victims;      // slots frozen while this human holds the current Freeze Fan
} TopRideRun;

static TopRideRun tr_runs[4];

// Off for the attract demo, which has a CPU in every slot.
static int tr_armed;

// Hit states only: Ant Doom, the falls and Speed Down are course routes or pickups.
static int IsHitState(void *vt)
{
    return vt == TR_KSTATE_VT_PRESS || vt == TR_KSTATE_VT_CRUSH || vt == TR_KSTATE_VT_EXPLODE ||
           vt == TR_KSTATE_VT_STRIKE || vt == TR_KSTATE_VT_SPIN || vt == TR_KSTATE_VT_SANDSPIN ||
           vt == TR_KSTATE_VT_NUMB || vt == TR_KSTATE_VT_ELEC || vt == TR_KSTATE_VT_BURN ||
           vt == TR_KSTATE_VT_FREEZE || vt == TR_KSTATE_VT_CONFUSE;
}

// The Freeze Fan freezes through effectors that live inside its power state, so a kirby
// newly frozen while a human holds the fan was frozen by it.
static void CreditFreeze(TopRideKirbyMgr *mgr, int victim)
{
    for (int s = 0; s < 4; s++)
    {
        TopRideKirby *h = mgr->kirbys[s];
        if (s == victim || !h || TopRide_GetPlayerKind(s) != TR_PKIND_HMN)
            continue;
        if (TopRide_KirbyStateVtable(h) != TR_ITEMPOWER_VT_FREEZE_FAN)
            continue;
        tr_runs[s].fan_victims |= 1 << victim;
        if (Popcount64(tr_runs[s].fan_victims) >= AP_TR_FREEZE_VICTIMS_NEED)
            APCheckDetect_Observe(APCK_TR_FREEZE_FAN_3);
    }
}

static void SampleTopRideTimed(int slot, TopRideMode mode, int frames)
{
    if (frames == 0)
        return;

    int course = TopRide_GetSelectedCourse();
    TopRideMachineKind machine = TopRide_GetMachineKind(slot);
    for (int i = 0; i < TR_TIMED_NUM; i++)
    {
        const TopRideTimedCheck *c = &tr_timed_checks[i];
        if (c->mode == mode && c->course == course && c->machine == machine && frames <= c->frames)
            APCheckDetect_Observe(c->ck);
    }
}

static void OnTopRideLap(TopRideKirby *k, TopRideRun *run, TopRideMode mode)
{
    int t = k->prev_lap_frames;
    if (mode == TOPRIDEMODE_FREE)
    {
        SampleTopRideTimed(k->player_slot, mode, t);
        return;
    }
    if (mode != TOPRIDEMODE_TIME)
        return;

    if (run->lap_num == 0 || t < run->lap_min)
        run->lap_min = t;
    if (run->lap_num == 0 || t > run->lap_max)
        run->lap_max = t;
    if (run->lap_num < 255)
        run->lap_num++;
}

// Fastest and slowest lap of a finished Time Attack no further apart than the label's
// spread. Lap 1 is timed from the start signal, not from the line.
static void JudgeTimeAttack(TopRideKirby *k, TopRideRun *run)
{
    SampleTopRideTimed(k->player_slot, TOPRIDEMODE_TIME, k->finish_time);
    if (run->lap_num >= 2 && run->lap_max - run->lap_min <= AP_TR_EVEN_LAP_FRAMES)
        APCheckDetect_Observe(APCK_TR_TA_EVEN_LAPS);
}

static int TopRideRivals(TopRideKirbyMgr *mgr, int slot)
{
    int n = 0;
    for (int s = 0; s < 4; s++)
    {
        if (s != slot && mgr->kirbys[s])
            n++;
    }
    return n;
}

static int RivalsAllLevel5(int slot)
{
    TopRideConfig *cfg = &Gm_GetGameData()->topride_config;
    for (int s = 0; s < 4; s++)
    {
        if (s == slot)
            continue;
        if (TopRide_GetPlayerKind(s) != TR_PKIND_CPU || cfg->slots[s].handicap != AP_TR_CPU_LEVEL_5)
            return 0;
    }
    return 1;
}

static void RecordTopRideColor(int slot)
{
    int color = TopRide_GetColor(slot);
    if (color < 0 || color >= KIRBYCOLOR_NUM)
        return;

    if (!LATCH_BIT(ap_save->checks.tr_color_mask, color))
        return;
    OSReport("[APCheckDetect] Top Ride race finished as %s (colors = %s)\n",
             KirbyColor_Names[color], MaskBits(ap_save->checks.tr_color_mask, KIRBYCOLOR_NUM));
}

static void RecordTopRideItem(int item)
{
    if (item == TRITEM_PARTY_BALL)
        item = TRITEM_PARTY_BALL_ALT;
    if (!LATCH_BIT(ap_save->checks.tr_item_mask, item))
        return;
    OSReport("[APCheckDetect] Top Ride item %s used (%d of %d kinds)\n", TopRideItemKind_Names[item],
             Popcount64(ap_save->checks.tr_item_mask), Popcount64(AP_TR_ITEM_MASK_ALL));
}

static void RecordSteerWin(int course)
{
    if (course < 0 || course >= TOPRIDE_NUM)
        return;

    if (!LATCH_BIT(ap_save->checks.tr_steer_win_mask, course))
        return;
    OSReport("[APCheckDetect] Top Ride %s won on Steer Star (courses = %s)\n",
             TopRideCourse_Names[course], MaskBits(ap_save->checks.tr_steer_win_mask, TOPRIDE_NUM));
}

// Runs on the frame a human's finish lands. standing is the results order, and its 0 on
// that frame is the 1st place vanilla's own cells test.
static void JudgeTopRideRace(TopRideKirbyMgr *mgr, TopRideKirby *k, TopRideRun *run)
{
    int slot = k->player_slot;
    RecordTopRideColor(slot);

    if (k->standing != 0 || TopRideRivals(mgr, slot) == 0)
        return;

    int course = TopRide_GetSelectedCourse();
    if (course == TOPRIDE_FIRE && !run->burned)
        APCheckDetect_Observe(APCK_TR_FIRE_NO_BURN);
    if (course == TOPRIDE_SAND && !run->ant_doom)
        APCheckDetect_Observe(APCK_TR_SAND_NO_ANTDOOM);
    if (course == TOPRIDE_WATER && !run->falls)
        APCheckDetect_Observe(APCK_TR_WATER_NO_FALLS);
    if (course == TOPRIDE_LIGHT && !run->grinded)
        APCheckDetect_Observe(APCK_TR_LIGHT_NO_RAIL);
    if (RivalsAllLevel5(slot))
        APCheckDetect_Observe(APCK_TR_1ST_VS_3_LV5);
    if (TopRide_GetMachineKind(slot) == TR_MACHINE_STEER)
        RecordSteerWin(course);
    // With nothing spawning, the only hits left are rams and the course's own.
    if (run->hits == 0 && Gm_GetGameData()->topride_config.item_rule != TOPRIDE_ITEM_RULE_ZERO)
        APCheckDetect_Observe(APCK_TR_NO_HIT);
    if (run->hits >= AP_TR_HITS_NEED)
        APCheckDetect_Observe(APCK_TR_HIT_5_WIN);
    if (run->speed_downs >= AP_TR_SPEEDDOWN_NEED)
        APCheckDetect_Observe(APCK_TR_SPEEDDOWN_3_WIN);
}

// A human and any other finisher within AP_TR_PHOTO_FINISH_FRAMES. An unfinished
// kirby's finish_time is a projection, so both have to have crossed the line.
static void SampleTopRidePhoto(TopRideKirbyMgr *mgr, TopRideKirby *k)
{
    for (int s = 0; s < 4; s++)
    {
        TopRideKirby *o = mgr->kirbys[s];
        if (!o || o == k || !o->finished)
            continue;
        if (abs((int)k->finish_time - (int)o->finish_time) <= AP_TR_PHOTO_FINISH_FRAMES)
            APCheckDetect_Observe(APCK_TR_PHOTO);
    }
}

static void SampleTopRideHuman(TopRideKirbyMgr *mgr, TopRideKirby *k, TopRideRun *run, void *vt, int entered)
{
    TopRideMode mode = TopRide_GetMode();

    if (vt != TR_ITEMPOWER_VT_FREEZE_FAN)
        run->fan_victims = 0;
    if (entered && IsHitState(vt) && run->hits < 255)
        run->hits++;
    if (vt == TR_KSTATE_VT_BURN)
        run->burned = 1;
    if (vt == TR_KSTATE_VT_DOODLEBUG || vt == TR_KSTATE_VT_DOODLEBUG_OUT)
        run->ant_doom = 1;
    if (vt == TR_KSTATE_VT_WHIRLPOOL)
        run->falls = 1;
    if (vt == TR_KSTATE_VT_GRIND)
        run->grinded = 1;

    // TopRide_CheckPerCourseObjectives resets this once it has read it, so here, just
    // before it, it is whatever TopRide_KirbyApplyItem applied since the last frame.
    int item = k->active_item_kind;
    if (item < TRITEM_NUM)
    {
        if (item == TRITEM_SPEED_DOWN && run->speed_downs < 255)
            run->speed_downs++;
        RecordTopRideItem(item);
        for (int i = 0; i < TR_ABILITY_ITEM_NUM; i++)
        {
            if (item == topride_ability_items[i].item)
                run->ability_items |= 1 << i;
        }
        if (run->ability_items == TR_ABILITY_ITEMS_ALL)
            APCheckDetect_Observe(APCK_TR_ABILITY_ITEMS);
    }

    if (k->lap_progress > run->laps)
    {
        run->laps = k->lap_progress;
        OnTopRideLap(k, run, mode);
    }

    if (!k->finished)
        return;
    if (mode == TOPRIDEMODE_RACE)
        SampleTopRidePhoto(mgr, k);
    if (run->judged)
        return;
    run->judged = 1;
    if (mode == TOPRIDEMODE_TIME)
        JudgeTimeAttack(k, run);
    else if (mode == TOPRIDEMODE_RACE)
        JudgeTopRideRace(mgr, k, run);
}

// Replaces both bl TopRide_CheckPerCourseObjectives, one per session class: 0x8029cb74 in
// TopRide_GameModeNormalUpdate (0x8029c650) and 0x802acd4c in TopRide_GameModeTuningUpdate
// (0x802ac850). Each runs per occupied slot once TopRide_KirbyMgrUpdate has moved, lapped
// and ranked the kirbys.
static void APCheckDetect_TopRideKirby(TopRideKirbyRecord *rec)
{
    TopRideKirbyMgr *mgr = *stc_topride_kirbymgr;
    TopRideKirby *k = rec->kirby;
    if (tr_armed && mgr && mgr->round_state == 2 && k && k->player_slot < 4)
    {
        int slot = k->player_slot;
        TopRideRun *run = &tr_runs[slot];
        void *vt = TopRide_KirbyStateVtable(k);
        int entered = vt != run->prev_vt;
        run->prev_vt = vt;

        if (entered && vt == TR_KSTATE_VT_FREEZE)
            CreditFreeze(mgr, slot);
        if (TopRide_GetPlayerKind(slot) == TR_PKIND_HMN)
            SampleTopRideHuman(mgr, k, run, vt, entered);
    }

    TopRide_CheckPerCourseObjectives(rec);
}

void APCheckDetect_OnTopRideLoadEnd(void)
{
    memset(tr_runs, 0, sizeof(tr_runs));
    tr_armed = !Gm_IsAutoDemo();
    in_city_trial = 0;
}

void APCheckDetect_OnBoot(void)
{
    CODEPATCH_REPLACECALL(0x802022ec, APCheckDetect_EnemyDefeat);
    CODEPATCH_REPLACECALL(0x80105da0, APCheckDetect_YakumonoBreak);
    CODEPATCH_REPLACECALL(0x80010418, APCheckDetect_FreeRunLap);
    CODEPATCH_REPLACECALL(0x80010d68, APCheckDetect_RaceFinish);
    CODEPATCH_REPLACECALL(0x801db928, APCheckDetect_ItemCollect);
    CODEPATCH_REPLACECALL(0x801c68c0, APCheckDetect_GiveDamage);
    CODEPATCH_REPLACECALL(0x801c6990, APCheckDetect_GiveDamage);
    CODEPATCH_REPLACECALL(0x801d741c, APCheckDetect_RailFireHit);
    CODEPATCH_REPLACECALL(0x80196668, APCheckDetect_RailFireHit);
    CODEPATCH_HOOKAPPLY(0x801dacb4);
    CODEPATCH_REPLACECALL(0x801279a0, APCheckDetect_PredictRandom);
    CODEPATCH_REPLACECALL(0x8029cb74, APCheckDetect_TopRideKirby);
    CODEPATCH_REPLACECALL(0x802acd4c, APCheckDetect_TopRideKirby);
    OSReport("[APCheckDetect] Hooks installed\n");
}

int APCheckDetect_GetProgress(APCheckProgressKind which)
{
    switch (which)
    {
    case AP_PROGRESS_ALLUP_TOTAL: return ap_save->checks.allup_collect_total;
    case AP_PROGRESS_PURPLE_SR1:  return ap_save->checks.purple_sr1_wins;
    case AP_PROGRESS_RACE_COLORS: return ap_save->checks.race_color_mask;
    case AP_PROGRESS_TR_COLORS:   return ap_save->checks.tr_color_mask;
    case AP_PROGRESS_TR_STEER_WINS: return ap_save->checks.tr_steer_win_mask;
    case AP_PROGRESS_DRAG_WINS:   return ap_save->checks.drag_win_mask;
    case AP_PROGRESS_AR_COURSE_WINS: return ap_save->checks.ar_course_win_mask;
    case AP_PROGRESS_TR_ITEMS:    return (int)ap_save->checks.tr_item_mask;
    default:                      return 0;
    }
}

// A lowered counter does not un-record a check already recorded.
void APCheckDetect_DebugSetProgress(APCheckProgressKind which, int value)
{
    if (value < 0)
        value = 0;

    switch (which)
    {
    case AP_PROGRESS_ALLUP_TOTAL:
        ap_save->checks.allup_collect_total = (u16)value;
        break;
    case AP_PROGRESS_PURPLE_SR1:
        ap_save->checks.purple_sr1_wins = (u8)value;
        break;
    case AP_PROGRESS_RACE_COLORS:
        ap_save->checks.race_color_mask = (u8)value;
        break;
    case AP_PROGRESS_TR_COLORS:
        ap_save->checks.tr_color_mask = (u8)value;
        break;
    case AP_PROGRESS_TR_STEER_WINS:
        ap_save->checks.tr_steer_win_mask = (u8)value;
        break;
    case AP_PROGRESS_DRAG_WINS:
        ap_save->checks.drag_win_mask = (u8)value;
        break;
    case AP_PROGRESS_AR_COURSE_WINS:
        ap_save->checks.ar_course_win_mask = (u16)value;
        break;
    case AP_PROGRESS_TR_ITEMS:
        ap_save->checks.tr_item_mask = (u32)value;
        break;
    default:
        return;
    }

    // No card write: Hoshi_WriteSave stalls the frame and this fires per D-pad tick.
    OSReport("[APCheckDetect] Debug: progress %d set to %d\n", which, value);
}

void APCheckDetect_ReportProgress(void)
{
    const APCheckProgress *c = &ap_save->checks;
    OSReport("[APCheckDetect] Progress: %d/%d All Ups, %d/%d purple SR1 wins, AR colors %s, "
             "TR colors %s\n",
             c->allup_collect_total, AP_ALLUP_TOTAL_NEED, c->purple_sr1_wins, AP_PURPLE_SR1_NEED,
             MaskBits(c->race_color_mask, KIRBYCOLOR_NUM), MaskBits(c->tr_color_mask, KIRBYCOLOR_NUM));
    OSReport("[APCheckDetect] Progress: TR Steer wins %s, drag wins %s, AR course wins %s, "
             "TR items %s\n",
             MaskBits(c->tr_steer_win_mask, TOPRIDE_NUM), MaskBits(c->drag_win_mask, AP_DRAG_RACE_NUM),
             MaskBits(c->ar_course_win_mask, AIRRIDE_NUM), MaskBits(c->tr_item_mask, TRITEM_NUM));
}

void APCheckDetect_ResetProgress(void)
{
    ap_observed[0] = 0;
    ap_observed[1] = 0;
    memset(&ap_save->checks, 0, sizeof(ap_save->checks));
}

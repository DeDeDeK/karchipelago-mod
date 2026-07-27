#include "game.h"
#include "os.h"
#include "scene.h"
#include "stage.h"
#include "stadium.h"
#include "rider.h"
#include "item.h"

#include "main.h"
#include "ap_check_detect.h"

// Detection for the Archipelago checklist's objectives. All sampling lives
// here, in the two hooks below, because the checklist framework polls every
// predicate each frame in every scene - a predicate is only ever a read of
// state latched here.

// Objectives observed this boot, one bit per APCheckKind. Transient - the
// permanent record is ap_save->sent_checks, written by the framework the frame
// after a predicate first returns true. The two objectives that count across
// boots ("in total" / "3 times") read ap_save->checks instead.
static u64 ap_observed;

static void Observe(int ck)
{
    u64 bit = 1ULL << ck;
    if (ap_observed & bit)
        return;
    ap_observed |= bit;
    OSReport("[APCheckDetect] Objective %d achieved\n", ck);
}

int APCheckDetect_IsSet(int ck)
{
    switch (ck)
    {
    case APCK_ALLUPS_10:       return ap_save->checks.allup_collect_total >= AP_ALLUP_TOTAL_NEED;
    case APCK_SR1_PURPLE_3X:   return ap_save->checks.purple_sr1_wins >= AP_PURPLE_SR1_NEED;
    default:
        if (ck < 0 || ck >= APCK_NUM)
            return 0;
        return (ap_observed >> ck) & 1ULL;
    }
}

// City Trial's coral: yakumono descriptor 33, of which GrCity1 places 10.
// The count is read from the stage at runtime rather than hardcoded, matching
// how the vanilla "break all coral" cell resolves Sky Sands' total.
#define AP_CORAL_DESC_ID 33

// Standing on the flower means standing on one of the tallest structures in the
// city, on foot. Without the flower's own coordinates this approximates it as
// "on foot, this high, and staying there" - the dwell requirement is what keeps
// a dismount-and-fall through the same altitude from counting.
#define AP_FLOWER_MIN_Y      400.0f
#define AP_FLOWER_DWELL_FRAMES 30

// 0.10 seconds at 60fps.
#define AP_PHOTO_FINISH_FRAMES 6

#define AP_FEET_PER_METRE (1.0f / 0.3048f)

// Per-City-Trial-run item objectives. Every one is a delta against a baseline
// taken at the start of the run, so patches applied at round start and anything
// collected in an earlier run don't count.
typedef struct RunItemCheck
{
    u8 ck;
    u8 it_kind;
    u8 need;
} RunItemCheck;

static const RunItemCheck run_item_checks[] = {
    { APCK_HP_PATCHES_10,  ITKIND_HP,            10 },
    { APCK_FOOD_ICECREAM,  ITKIND_FOODICECREAM,   3 },
    { APCK_FOOD_RICEBALL,  ITKIND_FOODRICEBALL,   3 },
    { APCK_FOOD_CHICKEN,   ITKIND_FOODCHICKEN,    3 },
    { APCK_FOOD_CURRY,     ITKIND_FOODCURRY,      3 },
    { APCK_FOOD_RAMEN,     ITKIND_FOODRAMEN,      3 },
    { APCK_FOOD_OMELET,    ITKIND_FOODOMELET,     3 },
    { APCK_FOOD_HAMBURGER, ITKIND_FOODHAMBURGER,  3 },
    { APCK_FOOD_APPLE,     ITKIND_FOODAPPLE,      3 },
};

#define RUN_ITEM_NUM ((int)(sizeof(run_item_checks) / sizeof(run_item_checks[0])))

static int run_base[5][RUN_ITEM_NUM];
static int prev_allup[5];
static int needs_baseline[5];
static int flower_frames[5];

// Coral placed by the loaded stage, sampled once at load (0 outside City Trial).
static int coral_total;

// Per-frame proc on each human rider during a City Trial round.
static void APCheckDetect_PerFrame(GOBJ *rg)
{
    RiderData *rd = rg->userdata;
    int ply = rd->ply;
    PlayerStats *st = Ply_GetItemCollectArray(ply);

    // Baseline on the first frame after the intro, so the round's starting
    // patches are not read as a collection.
    if (needs_baseline[ply])
    {
        if (Gm_GetIntroState() != GMINTRO_END)
            return;
        needs_baseline[ply] = 0;
        for (int i = 0; i < RUN_ITEM_NUM; i++)
            run_base[ply][i] = st->item_collect[run_item_checks[i].it_kind];
        prev_allup[ply] = st->item_collect[ITKIND_ALLUP];
        flower_frames[ply] = 0;
        return;
    }

    for (int i = 0; i < RUN_ITEM_NUM; i++)
    {
        int got = st->item_collect[run_item_checks[i].it_kind] - run_base[ply][i];
        if (got >= (int)run_item_checks[i].need)
            Observe(run_item_checks[i].ck);
    }

    // All Ups count across the whole save, so the frame delta folds into a
    // persistent counter. Every pickup path bumps item_collect, including a
    // patch spawned by an Archipelago item - a received All Up counts.
    int allup = st->item_collect[ITKIND_ALLUP];
    if (allup > prev_allup[ply] && ap_save->checks.allup_collect_total < AP_ALLUP_TOTAL_NEED)
    {
        ap_save->checks.allup_collect_total += (u16)(allup - prev_allup[ply]);
        OSReport("[APCheckDetect] All Ups collected: %d/%d\n",
                 ap_save->checks.allup_collect_total, AP_ALLUP_TOTAL_NEED);
    }
    prev_allup[ply] = allup;

    // Coral: the whole stage's worth, broken by this player, this round.
    // yakumono_break is zeroed per game, so no baseline is needed.
    if (coral_total > 0 && st->yakumono_break[AP_CORAL_DESC_ID] >= coral_total)
        Observe(APCK_BREAK_ALL_CORAL);

    // Out of bounds: the engine's own definition. Negative clearance is what
    // makes Machine_CheckFallDeath respawn the player.
    if (calcDistanceFromOOB(&rd->pos) < 0.0f)
        Observe(APCK_OUT_OF_BOUNDS);

    if (!Rider_IsOnMachine(rd) && rd->pos.Y >= AP_FLOWER_MIN_Y)
    {
        if (++flower_frames[ply] >= AP_FLOWER_DWELL_FRAMES)
            Observe(APCK_CASTLE_FLOWER);
    }
    else
    {
        flower_frames[ply] = 0;
    }
}

void APCheckDetect_On3DLoadEnd(void)
{
    for (int i = 0; i < 5; i++)
    {
        needs_baseline[i] = 1;
        flower_frames[i] = 0;
    }
    coral_total = 0;

    // City Trial rounds only. "In one game" means one CT Trial run, and every
    // objective sampled per-frame is a city one.
    if (!Gm_IsInCity() || Gm_GetCityMode() != CITYMODE_TRIAL)
        return;

    coral_total = Gr_GetYakumonoSpawnTotal(AP_CORAL_DESC_ID);

    int attached = 0;
    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;
        GOBJ *r = Ply_GetRiderGObj(i);
        if (!r)
            continue;
        GObj_AddProc(r, APCheckDetect_PerFrame, RDPRI_HITCOLL + 1);
        attached++;
    }
    OSReport("[APCheckDetect] Sampling %d player(s) (coral total %d)\n",
             attached, coral_total);
}

// Did this slot's result get recorded? Stadium_ComputeRank* skip slots whose
// gate byte is nonzero, so their placement and time are stale.
static int SlotRecorded(const StadiumResults *r, int p)
{
    return Ply_GetPKind(p) != PKIND_NONE && r->xc00[p] == 0;
}

// Any two finishers within AP_PHOTO_FINISH_FRAMES of each other. CPU racers
// count: the latch loop copies all four slots unconditionally and the rankers
// treat CPUs as players, which is what makes these objectives solo-achievable.
static int PhotoFinish(const StadiumResults *r)
{
    for (int a = 0; a < 4; a++)
    {
        if (!SlotRecorded(r, a) || !r->ply_finished[a] || r->ply_race_time[a] == 0)
            continue;
        for (int b = a + 1; b < 4; b++)
        {
            if (!SlotRecorded(r, b) || !r->ply_finished[b] || r->ply_race_time[b] == 0)
                continue;
            int gap = r->ply_race_time[a] - r->ply_race_time[b];
            if (gap < 0)
                gap = -gap;
            if (gap <= AP_PHOTO_FINISH_FRAMES)
                return 1;
        }
    }
    return 0;
}

static void SampleStadium(const StadiumResults *r, StadiumKind st)
{
    if (st >= STKIND_DRAG1 && st <= STKIND_DRAG4)
    {
        if (PhotoFinish(r))
            Observe(APCK_DRAG1_PHOTO + (st - STKIND_DRAG1));
        return;
    }

    for (int p = 0; p < 4; p++)
    {
        if (Ply_GetPKind(p) != PKIND_HMN || !SlotRecorded(r, p))
            continue;

        if (st >= STKIND_SINGLERACE1 && st <= STKIND_SINGLERACE9)
        {
            // Stadium_ComputeRankByTime ranks players who never crossed the
            // line too, ordering them by distance - so placement alone would
            // hand 1st to whoever was leading when the round was abandoned.
            if (!r->ply_finished[p] || r->ply_placement[p] != 0)
                continue;
            Observe(APCK_SR1_FIRST + (st - STKIND_SINGLERACE1));
            if (st != STKIND_SINGLERACE1)
                continue;
            if (Ply_GetMachineKind(p) == VCKIND_BULK)
                Observe(APCK_SR1_BULK);
            // Ply_GetColor reads PlayerDesc.color, which is a KirbyColor only
            // for a Kirby rider - City Trial is Kirby-only, but the stadiums
            // are reachable from a Dedede match too.
            if (Gm_GetGameData()->ply_desc[p].rider_kind == RDKIND_KIRBY &&
                Ply_GetColor(p) == KIRBYCOLOR_PURPLE &&
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
                Observe(APCK_HIGHJUMP_1500);
        }
        else if (st == STKIND_AIRGLIDER)
        {
            if (r->ply_dist[p] * AP_FEET_PER_METRE > 2000.0f)
                Observe(APCK_AIRGLIDER_2000);
        }
        else if (st == STKIND_MELEE1 && r->ply_points[p] > 100)
        {
            Observe(APCK_MELEE1_100);
        }
        else if (st == STKIND_MELEE2 && r->ply_points[p] > 60)
        {
            Observe(APCK_MELEE2_60);
        }
    }
}

void APCheckDetect_On3DExit(void)
{
    GameData *gd = Gm_GetGameData();

    // Stadium_ExitMinor skips the results latch for a replay (and for the
    // title-screen demo, which can't reach here), leaving the previous round's
    // values in the block.
    if (gd->is_replay)
        return;

    MajorKind major = Scene_GetCurrentMajor();
    if (major == MJRKIND_CITY)
    {
        if (Gm_GetCityMode() == CITYMODE_STADIUM)
            SampleStadium(&gd->stadium_results, Gm_GetCurrentStadiumKind());
    }
    else if (major == MJRKIND_AIR)
    {
        if (Gm_GetAirRideMode() == AIRRIDEMODE_RACE && PhotoFinish(&gd->stadium_results))
            Observe(APCK_AIRRIDE_PHOTO);
    }
}

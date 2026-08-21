#include "game.h"
#include "os.h"
#include "scene.h"
#include "stage.h"
#include "stadium.h"
#include "rider.h"
#include "item.h"
#include "hurt.h"
#include "code_patch/code_patch.h"

#include "inline.h"

#include "main.h"
#include "ap_check_detect.h"
#include "gate_ap_star.h"

// Sampling for the Archipelago checklist's objectives. The framework polls every
// predicate each frame in every scene, so a predicate is only ever a read of state
// latched by the two hooks below.

// Objectives observed this boot, one bit per APCheckKind. Objectives that count
// across boots read ap_save->checks instead.
static u64 ap_observed;

void APCheckDetect_Observe(int ck)
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
    case APCK_ALLUPS_5:           return ap_save->checks.allup_collect_total >= AP_ALLUP_TOTAL_NEED;
    case APCK_SR1_PURPLE_3X:      return ap_save->checks.purple_sr1_wins >= AP_PURPLE_SR1_NEED;
    case APCK_AIRRIDE_ALL_COLORS: return ap_save->checks.race_color_mask == AP_RACE_COLOR_MASK_ALL;
    case APCK_ASSEMBLE_AP_STAR:   return GateApStar_WasAssembled();
    default:
        if (ck < 0 || ck >= APCK_NUM)
            return 0;
        return (ap_observed >> ck) & 1ULL;
    }
}

// Coral is yakumono descriptor 33.
#define AP_CORAL_DESC_ID 33

// Places in the city a rider has to reach on foot, and the radius that counts as
// having reached one.
typedef struct FootVisitCheck
{
    u8 ck;
    Vec3 pos;
    float radius;
} FootVisitCheck;

static const FootVisitCheck foot_visit_checks[] = {
    // The flower sits on a very small platform on top of Castle Hall, so its sphere
    // is tight against a stage whose out-of-bounds box spans 2600 units in X and Z.
    { APCK_CASTLE_FLOWER,   { 408.7f, 370.8f, -564.6f },  2.0f },
    { APCK_MODEL_CITY,      { -422.7f, 12.7f, -168.9f }, 10.0f },
    { APCK_VOLCANO_FLOWER,  { -107.0f, 205.1f, -847.3f },  5.0f },
    // The garden's top surface. Vanilla's own cell only asks the player to reach the
    // garden, so this sphere sits on the roof rather than anywhere on the structure.
    { APCK_SKY_GARDEN_TOP,  { -67.9f, 463.8f, -0.3f },    5.0f },
};

#define FOOT_VISIT_NUM ((int)(sizeof(foot_visit_checks) / sizeof(foot_visit_checks[0])))

// Fantasy Meadows' shortcut is an elevated arc peaking around (255, 135, 6); the
// normal racing line runs 40 to 60 units below it. The sphere sits on the arc's
// descent, wide enough to cover both the surface and the line a machine at speed
// flies, and still 26 units clear of the racing line. Sampled in every Air Ride
// mode, since the objective names none the way vanilla's TA and FR cells do.
static const Vec3 meadows_shortcut_pos = { 249.7f, 120.0f, 19.2f };

#define AP_MEADOWS_SHORTCUT_RADIUS 25.0f

static int WithinSphere(const Vec3 *p, const Vec3 *centre, float radius)
{
    float dx = p->X - centre->X;
    float dy = p->Y - centre->Y;
    float dz = p->Z - centre->Z;
    return dx * dx + dy * dy + dz * dz <= radius * radius;
}

// A climb into the city's ceiling stops at Y 1040.3, well under the stage's
// out-of-bounds lid at 1500. The threshold sits below the ceiling so the contact
// frame is not required, and far above the sky garden at 464 - the highest place
// reachable without flying.
#define AP_MAX_ALTITUDE_Y 1000.0f

// 0.10 seconds at 60fps.
#define AP_PHOTO_FINISH_FRAMES 6

#define AP_FEET_PER_METRE (1.0f / 0.3048f)

// Targets of the Air Ride NEBULA BELT objectives. Each is quoted in its cell label and
// in the matching AP location name, so changing one is a two-repo edit.
#define AP_NEBULA_FEET_NEED   5500.0f
// 02:30:00 at 60fps.
#define AP_NEBULA_2LAP_FRAMES 9000
// 10 seconds at 60fps.
#define AP_NEBULA_AIR_FRAMES  600

// Per-City-Trial-run item objectives, counted as a delta against a baseline taken
// at the start of the run.
typedef struct RunItemCheck
{
    u8 ck;
    u8 it_kind;
    u8 need;
} RunItemCheck;

// ItemKind 0/1/2 are the three box colors, and a break bumps item_collect the same
// way a pickup does, so the box counts ride the same per-run delta as the rest.
static const RunItemCheck run_item_checks[] = {
    { APCK_HP_PATCHES_10,  ITKIND_HP,            10 },
    { APCK_BOX_BLUE_20,    ITKIND_BOXBLUE,       20 },
    { APCK_BOX_GREEN_10,   ITKIND_BOXGREEN,      10 },
    { APCK_BOX_RED_10,     ITKIND_BOXRED,        10 },
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

// Coral placed by the loaded stage, sampled once at load (0 outside City Trial).
static int coral_total;

// Is a City Trial Trial round loaded? The three-legendary poll needs it, and that
// poll cannot ride the per-rider sampler: assembly ends in
// Rider_RespawnFullRecreate, which tears the rider's machine down under it.
static int in_city_trial;

// Kirbys KO'd by a human King Dedede in the current Destruction Derby game.
static int dedede_kirby_kos;

#define AP_DEDEDE_KIRBY_KO_NEED 10

// Enemies a human defeated mid-Mic-blast in the current KIRBY MELEE round, and
// whether such a round is what is loaded. The melee stadiums are the only City
// Trial stages that spawn the AI enemy pool at all.
static int mic_enemy_kos;
static int in_kirby_melee;

// Is Nebula Belt the loaded Air Ride course? Latched at load like in_kirby_melee,
// because the objectives keyed off it are sampled once the round is already over.
static int in_nebula;

#define AP_MIC_ENEMY_KO_NEED 10

// Is this player's rider singing? The Mic's damage lands over the blast animation
// and its recovery, so both states count.
static int IsMidMicBlast(int ply)
{
    GOBJ *rg = Ply_GetRiderGObj(ply);
    if (!rg)
        return 0;

    RiderData *rd = rg->userdata;
    return rd->copy_kind == COPYKIND_MIC &&
           (rd->state_idx == RIDERSTATE_MIC_SING || rd->state_idx == RIDERSTATE_MIC_END);
}

// Per-frame proc on each human rider during a City Trial round.
static void APCheckDetect_PerFrame(GOBJ *rg)
{
    RiderData *rd = rg->userdata;
    int ply = rd->ply;
    PlayerStats *st = Ply_GetItemCollectArray(ply);

    // Baseline after the intro, so the round's starting patches are not read as
    // a collection.
    if (needs_baseline[ply])
    {
        if (Gm_GetIntroState() != GMINTRO_END)
            return;
        needs_baseline[ply] = 0;
        for (int i = 0; i < RUN_ITEM_NUM; i++)
            run_base[ply][i] = st->item_collect[run_item_checks[i].it_kind];
        prev_allup[ply] = st->item_collect[ITKIND_ALLUP];
        return;
    }

    for (int i = 0; i < RUN_ITEM_NUM; i++)
    {
        int got = st->item_collect[run_item_checks[i].it_kind] - run_base[ply][i];
        if (got >= (int)run_item_checks[i].need)
            APCheckDetect_Observe(run_item_checks[i].ck);
    }

    // All Ups count across the whole save. Every pickup path bumps item_collect,
    // including a patch spawned by an Archipelago item.
    int allup = st->item_collect[ITKIND_ALLUP];
    if (allup > prev_allup[ply] && ap_save->checks.allup_collect_total < AP_ALLUP_TOTAL_NEED)
    {
        ap_save->checks.allup_collect_total += (u16)(allup - prev_allup[ply]);
        OSReport("[APCheckDetect] All Ups collected: %d/%d\n",
                 ap_save->checks.allup_collect_total, AP_ALLUP_TOTAL_NEED);
    }
    prev_allup[ply] = allup;

    // yakumono_break is zeroed per game, so no baseline is needed.
    if (coral_total > 0 && st->yakumono_break[AP_CORAL_DESC_ID] >= coral_total)
        APCheckDetect_Observe(APCK_BREAK_ALL_CORAL);

    // Only the copy-wheel grant paths set this mask, so a Mic panel picked up off
    // the ground does not count - the same wheel-only demand vanilla's Bomb and
    // Sleep cells make.
    if (st->copy_chance_mask & COPY_CHANCE_BIT(COPYKIND_MIC))
        APCheckDetect_Observe(APCK_MIC_COPY_CHANCE);

    // Negative clearance is the engine's own out-of-bounds definition - what makes
    // Machine_CheckFallDeath respawn the player.
    if (calcDistanceFromOOB(&rd->pos) < 0.0f)
        APCheckDetect_Observe(APCK_OUT_OF_BOUNDS);

    if (rd->pos.Y >= AP_MAX_ALTITUDE_Y)
        APCheckDetect_Observe(APCK_MAX_ALTITUDE);

    if (!Rider_IsOnMachine(rd))
    {
        for (int i = 0; i < FOOT_VISIT_NUM; i++)
        {
            const FootVisitCheck *fv = &foot_visit_checks[i];
            if (WithinSphere(&rd->pos, &fv->pos, fv->radius))
                APCheckDetect_Observe(fv->ck);
        }
    }
}

// Per-frame proc on each human rider during an Air Ride round on Fantasy Meadows.
static void APCheckDetect_PerFrameMeadows(GOBJ *rg)
{
    RiderData *rd = rg->userdata;

    if (WithinSphere(&rd->pos, &meadows_shortcut_pos, AP_MEADOWS_SHORTCUT_RADIUS))
        APCheckDetect_Observe(APCK_MEADOWS_SHORTCUT);
}

static int AttachSamplers(void *proc)
{
    int attached = 0;
    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;
        GOBJ *r = Ply_GetRiderGObj(i);
        if (!r)
            continue;
        GObj_AddProc(r, proc, RDPRI_HITCOLL + 1);
        attached++;
    }
    return attached;
}

void APCheckDetect_On3DLoadEnd(void)
{
    for (int i = 0; i < 5; i++)
        needs_baseline[i] = 1;
    coral_total = 0;
    dedede_kirby_kos = 0;
    mic_enemy_kos = 0;
    in_city_trial = 0;

    StadiumKind st = Gm_GetCurrentStadiumKind();
    in_kirby_melee = Scene_GetCurrentMajor() == MJRKIND_CITY &&
                     Gm_GetCityMode() == CITYMODE_STADIUM &&
                     (st == STKIND_MELEE1 || st == STKIND_MELEE2);

    // The loaded terrain, not GameData.stage_kind - that field is only the menu's
    // course selection and holds a stale value outside a race, while GrPlants1 is
    // Fantasy Meadows and GrSpace2 is Nebula Belt in every Air Ride mode.
    in_nebula = Scene_GetCurrentMajor() == MJRKIND_AIR && Gr_GetCurrentGrKind() == GR_SPACE2;

    if (Scene_GetCurrentMajor() == MJRKIND_AIR)
    {
        if (Gr_GetCurrentGrKind() == GR_PLANTS1)
            OSReport("[APCheckDetect] Sampling %d player(s) on Fantasy Meadows\n",
                     AttachSamplers(APCheckDetect_PerFrameMeadows));
        return;
    }

    // City Trial rounds only: "in one game" means one CT Trial run.
    if (!Gm_IsInCity() || Gm_GetCityMode() != CITYMODE_TRIAL)
        return;

    in_city_trial = 1;
    coral_total = Gr_GetYakumonoSpawnTotal(AP_CORAL_DESC_ID);

    OSReport("[APCheckDetect] Sampling %d player(s) (coral total %d)\n",
             AttachSamplers(APCheckDetect_PerFrame), coral_total);
}

void APCheckDetect_OnFrameStart(void)
{
    if (!in_city_trial || (ap_observed & (1ULL << APCK_ASSEMBLE_ALL_LEGENDARY)))
        return;

    for (int ply = 0; ply < 5; ply++)
    {
        if (Ply_GetPKind(ply) != PKIND_HMN)
            continue;
        PlayerStats *st = Ply_GetItemCollectArray(ply);
        // flags_84d is per-round state, zeroed with the rest of PlayerStats on
        // scene load, so this is the "in one game" scope vanilla's two-machine
        // cell has.
        if (st != NULL &&
            (st->flags_84d & PLYSTATS_DRAGOON_ASSEMBLED) &&
            (st->flags_84d & PLYSTATS_HYDRA_ASSEMBLED) &&
            GateApStar_AssembledThisRound(ply))
        {
            APCheckDetect_Observe(APCK_ASSEMBLE_ALL_LEGENDARY);
            return;
        }
    }
}

// Replaces the one bl Ply_AddDeath, the engine's unified KO recorder, inside
// Machine_GiveDamage. Who was KO'd is only available here: the per-player KO tally
// the Destruction Derby cells read records the killer alone, and a stadium CPU can
// be Meta Knight or King Dedede once those are unlocked, so a rival is not always
// a Kirby.
static void APCheckDetect_AddDeath(int victim, DmgLog *dmg_log, int is_bike, MachineKind machine_kind)
{
    Ply_AddDeath(victim, dmg_log, is_bike, machine_kind);

    if (!Gm_IsDestructionDerby())
        return;

    int killer = dmg_log->attacker_ply;
    if (victim < 0 || victim >= 4 || killer < 0 || killer >= 4 || killer == victim)
        return;
    if (Ply_GetPKind(killer) != PKIND_HMN)
        return;
    if (Ply_GetRiderKind(killer) != RDKIND_DEDEDE || Ply_GetRiderKind(victim) != RDKIND_KIRBY)
        return;

    dedede_kirby_kos++;
    if (dedede_kirby_kos <= AP_DEDEDE_KIRBY_KO_NEED)
        OSReport("[APCheckDetect] Kirbys KO'd as King Dedede: %d/%d\n",
                 dedede_kirby_kos, AP_DEDEDE_KIRBY_KO_NEED);
    if (dedede_kirby_kos >= AP_DEDEDE_KIRBY_KO_NEED)
        APCheckDetect_Observe(APCK_DD_DEDEDE_KO_KIRBY);
}

// Replaces the one bl Ply_RecordEnemyDefeat, the enemy-side counterpart of the KO
// recorder above. The Mic has no attack-method index of its own to read back out of
// PlayerStats.enemy_defeat_by_method the way the Tornado cell does, so the blast is
// identified from the crediting rider's live state instead.
static void APCheckDetect_EnemyDefeat(int ply, void *attacker_log, GOBJ *enemy)
{
    Ply_RecordEnemyDefeat(ply, attacker_log, enemy);

    if (!in_kirby_melee)
        return;
    if (ply < 0 || ply >= 4 || Ply_GetPKind(ply) != PKIND_HMN)
        return;
    if (!IsMidMicBlast(ply))
        return;

    mic_enemy_kos++;
    if (mic_enemy_kos <= AP_MIC_ENEMY_KO_NEED)
        OSReport("[APCheckDetect] Enemies defeated as Mic Kirby: %d/%d\n",
                 mic_enemy_kos, AP_MIC_ENEMY_KO_NEED);
    if (mic_enemy_kos >= AP_MIC_ENEMY_KO_NEED)
        APCheckDetect_Observe(APCK_MIC_ENEMY_KOS);
}

// Stadium_ComputeRank* skip slots whose gate byte is nonzero, leaving their
// placement and time stale.
static int SlotRecorded(const StadiumResults *r, int p)
{
    return Ply_GetPKind(p) != PKIND_NONE && r->xc00[p] == 0;
}

// Racers other than ply that the rankers counted. Single Race is reachable straight
// from the Stadium menu with no CPUs configured, where 1st place is free.
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

// A human and any other finisher within AP_PHOTO_FINISH_FRAMES of each other. The
// other side may be a CPU - the rankers treat them as players, which keeps these
// objectives solo-achievable - but the human has to be in the photo finish, so two
// CPUs trading places while the player trails do not award it.
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
            int gap = r->ply_race_time[a] - r->ply_race_time[b];
            if (gap < 0)
                gap = -gap;
            if (gap <= AP_PHOTO_FINISH_FRAMES)
                return 1;
        }
    }
    return 0;
}

// One bit per KirbyColor a human has finished an Air Ride race as. Like the Purple
// SINGLE RACE objective this needs the rider-kind test: Ply_GetColor reads
// PlayerDesc.color, a KirbyColor only for a Kirby rider.
static void RecordRaceColor(const GameData *gd, int p)
{
    if (gd->ply_desc[p].rider_kind != RDKIND_KIRBY)
        return;

    int color = Ply_GetColor(p);
    if (color < 0 || color >= KIRBYCOLOR_NUM)
        return;

    u8 bit = (u8)(1 << color);
    if (ap_save->checks.race_color_mask & bit)
        return;
    ap_save->checks.race_color_mask |= bit;
    OSReport("[APCheckDetect] Air Ride race finished as %s (colors = %s)\n",
             KirbyColor_Names[color],
             MaskBits(ap_save->checks.race_color_mask, KIRBYCOLOR_NUM));
}

// The two "finish 1st as <character>" objectives, on any course, plus the color mask and
// everything Nebula Belt asks for. The mask is the only thing here latched across boots.
static void SampleAirRide(const StadiumResults *r)
{
    GameData *gd = Gm_GetGameData();

    for (int p = 0; p < 4; p++)
    {
        if (Ply_GetPKind(p) != PKIND_HMN || !SlotRecorded(r, p))
            continue;

        // Crossing the line is the whole demand for a color - placement does not matter.
        if (r->ply_finished[p])
            RecordRaceColor(gd, p);

        // Stadium_ComputeRankByTime ranks players who never crossed the line too,
        // and a race can be started with no CPUs at all, where 1st place is free.
        int won = r->ply_finished[p] && r->ply_placement[p] == 0 && OpponentCount(r, p) > 0;

        if (won)
        {
            RiderKind rk = gd->ply_desc[p].rider_kind;
            if (rk == RDKIND_METAKNIGHT)
                APCheckDetect_Observe(APCK_AIRRIDE_1ST_METAKNIGHT);
            else if (rk == RDKIND_DEDEDE)
                APCheckDetect_Observe(APCK_AIRRIDE_1ST_DEDEDE);
        }

        if (!in_nebula)
            continue;

        if (won)
        {
            APCheckDetect_Observe(APCK_NEBULA_1ST);
            if (Ply_GetMachineKindAbs(p) == VCKIND_WHEELIESCOOTER)
                APCheckDetect_Observe(APCK_NEBULA_1ST_SCOOTER);
        }

        // Both gates AirRide_CheckRaceDistanceObjectives runs behind, so the demand
        // is the one vanilla's eight per-course distance cells make: a timed race,
        // set to 2 minutes.
        if (Gm_GetCityKind() == AIRRIDE_RULE_TIME &&
            Gm_GetRaceTimeLimitSeconds() == 120 &&
            Gm_GetPlayerRaceDistance(p) * AP_FEET_PER_METRE >= AP_NEBULA_FEET_NEED)
            APCheckDetect_Observe(APCK_NEBULA_DIST_2MIN);

        // AirRide_CheckRaceLapObjectives keys off the configured lap total rather
        // than laps completed, so a 3-lap race cannot pay out the 2-lap time.
        if (Gm_GetCityKind() == AIRRIDE_RULE_LAPS && Gm_GetRaceLapTotal() == 2 &&
            r->ply_race_time[p] != 0 && r->ply_race_time[p] <= AP_NEBULA_2LAP_FRAMES)
            APCheckDetect_Observe(APCK_NEBULA_2LAP_TIME);

        // airborne_time is the longest single airborne stretch, and PlayerStats is
        // only zeroed on the next 3D scene load, so it still reads this race's run.
        // The three flight machines are the only ones that hold a glide that long.
        MachineKind mk = Ply_GetMachineKindAbs(p);
        if ((mk == VCKIND_DRAGOON || mk == VCKIND_FLIGHT || mk == VCKIND_WINGED) &&
            Ply_GetItemCollectArray(p)->airborne_time > AP_NEBULA_AIR_FRAMES)
            APCheckDetect_Observe(APCK_NEBULA_AIRBORNE);
    }
}

static void SampleStadium(const StadiumResults *r, StadiumKind st)
{
    if (st >= STKIND_DRAG1 && st <= STKIND_DRAG4)
    {
        if (PhotoFinish(r))
            APCheckDetect_Observe(APCK_DRAG_PHOTO);
        return;
    }

    for (int p = 0; p < 4; p++)
    {
        if (Ply_GetPKind(p) != PKIND_HMN || !SlotRecorded(r, p))
            continue;

        if (st >= STKIND_SINGLERACE1 && st <= STKIND_SINGLERACE9)
        {
            // Stadium_ComputeRankByTime ranks players who never crossed the line
            // too, ordering them by distance, so placement alone is not a win, and
            // a stadium entered from the Stadium menu can be started with no CPUs,
            // where 1st place is free the moment the player crosses the line.
            if (!r->ply_finished[p] || r->ply_placement[p] != 0 || OpponentCount(r, p) == 0)
                continue;
            APCheckDetect_Observe(APCK_SR1_FIRST + (st - STKIND_SINGLERACE1));
            if (st != STKIND_SINGLERACE1)
                continue;
            if (Ply_GetMachineKindAbs(p) == VCKIND_BULK)
                APCheckDetect_Observe(APCK_SR1_BULK);
            // Ply_GetColor reads PlayerDesc.color, a KirbyColor only for a Kirby
            // rider - the stadiums are reachable from a Dedede match too.
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
                APCheckDetect_Observe(APCK_HIGHJUMP_1500);
        }
        else if (st == STKIND_AIRGLIDER)
        {
            if (r->ply_dist[p] * AP_FEET_PER_METRE > 2000.0f)
                APCheckDetect_Observe(APCK_AIRGLIDER_2000);
        }
        else if (st == STKIND_MELEE1 && r->ply_points[p] > 100)
        {
            APCheckDetect_Observe(APCK_MELEE1_100);
        }
        else if (st == STKIND_MELEE2 && r->ply_points[p] > 60)
        {
            APCheckDetect_Observe(APCK_MELEE2_60);
        }
        else if (st == STKIND_DESTRUCTION3 && r->ply_points[p] >= 10)
        {
            // ply_points is GameData.destruction_derby_ko_num here, the same field
            // the vanilla DD cells count against.
            APCheckDetect_Observe(APCK_DD3_KO_10);
        }
    }
}

void APCheckDetect_On3DExit(void)
{
    GameData *gd = Gm_GetGameData();

    // Stadium_ExitMinor skips the results latch for a replay, leaving the previous
    // round's values in the block.
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
        if (Gm_GetAirRideMode() != AIRRIDEMODE_RACE)
            return;
        if (PhotoFinish(&gd->stadium_results))
            APCheckDetect_Observe(APCK_AIRRIDE_PHOTO);
        SampleAirRide(&gd->stadium_results);
    }
}

void APCheckDetect_OnBoot(void)
{
    CODEPATCH_REPLACECALL(0x801e1f74, APCheckDetect_AddDeath);
    CODEPATCH_REPLACECALL(0x802022ec, APCheckDetect_EnemyDefeat);
    OSReport("[APCheckDetect] Hooks installed\n");
}

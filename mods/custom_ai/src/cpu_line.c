#include <float.h>

#include "game.h"
#include "rider.h"
#include "machine.h"
#include "stage.h"
#include "enemy.h"
#include "hsd.h"
#include "scene.h"
#include "os.h"
#include "inline.h"
#include "code_patch/code_patch.h"

#include "cpu_ai.h"
#include "cpu_sense.h"
#include "cpu_line.h"

// Cross-sections every LINE_SPACING units, more widely spaced on a lap too long for
// LINE_SAMPLE_MAX of them.
#define LINE_SPACING 8.0f
#define LINE_SAMPLE_MAX 1536
#define LINE_RANGE_MAX 160
#define LINE_SUCC_MAX 4
#define LINE_ALIGN_STEP 0.05f
// The line keeps this far off each edge, as a fraction of the road clamped to the range.
#define LINE_EDGE_UNITS 3.0f
#define LINE_EDGE_MIN 0.1f
#define LINE_EDGE_MAX 0.45f
#define LINE_ITERATIONS 300
// Samples searched around a cursor; past LINE_LOST_DIST the whole line is searched.
#define LINE_SEARCH 24
#define LINE_LOST_DIST 40.0f
// Corner braking: a corner this many frames ahead at the current speed, slower than the
// speed by this margin, starts a ChargeHold.
#define LINE_BRAKE_FRAMES 24.0f
#define LINE_BRAKE_MARGIN 0.15f
#define LINE_GRIP 1.3f
#define LINE_BIKE_TURN_RATE 0.03f
// Swerve and Formula barely turn unless charging, so the corner speed means nothing for them.
#define LINE_TURN_RATE_MIN 0.01f
#define LINE_CURV_MIN 0.0001f
#define LINE_CURV_SPREAD 4
// A branch is preferred when it saves this many units over the lap's own way.
#define LINE_SHORTCUT_GAIN 20.0f
#define LINE_BRANCH_STEPS 24
#define LINE_WEIGHT_MIN 0.5f

typedef struct LineRange
{
    s16 succ[LINE_SUCC_MAX];
    u8 succ_num;
    u8 on_loop;
    float len;
    float s_start; // lap position of the range's start, on the loop
    Vec3 dir_start;
    Vec3 dir_end;
} LineRange;

int cpu_racing_line = CPU_LINE_RACERS;

char *cpu_racing_line_names[CPU_LINE_NUM] = {
    [CPU_LINE_OFF] = "Off",
    [CPU_LINE_RACERS] = "Racers",
    [CPU_LINE_ALL] = "All CPUs",
};

static LineRange stc_ranges[LINE_RANGE_MAX];
static int stc_range_num;
static Vec3 *stc_l;
static Vec3 *stc_r;
static float *stc_a;
static float *stc_curv;
static int stc_num;
static int stc_ready;
static int stc_loop;
static float stc_lap;
static int stc_cursor[PLY_NUM];

static void CpuLine_Point(int i, Vec3 *out)
{
    Vec3 d;

    VECSubtract(&stc_r[i], &stc_l[i], &d);
    VECScale(&d, &d, stc_a[i]);
    VECAdd(&stc_l[i], &d, out);
}

static int CpuLine_Wrap(int i)
{
    if (stc_loop)
        return ((i % stc_num) + stc_num) % stc_num;
    if (i < 0)
        return 0;
    return i >= stc_num ? stc_num - 1 : i;
}

static void CpuLine_RangeEnds(int r, LineRange *lr)
{
    HSD_Spline *c = Spline_GetCenter(r);
    Vec3 start, end, p;

    lr->len = c->total_length;
    splArcLengthPoint(&start, c, 0);
    splArcLengthPoint(&end, c, 1.0f);
    splArcLengthPoint(&p, c, LINE_ALIGN_STEP);
    VECSubtract(&p, &start, &lr->dir_start);
    VECNormalize(&lr->dir_start, &lr->dir_start);
    splArcLengthPoint(&p, c, 1.0f - LINE_ALIGN_STEP);
    VECSubtract(&end, &p, &lr->dir_end);
    VECNormalize(&lr->dir_end, &lr->dir_end);
}

// Each range's continuations: the enabled ranges leaving its end point that are driven start
// to end, as races drive every range.
static int CpuLine_BuildGraph(void)
{
    int exits[SPLINE_JUNCTION_EXIT_MAX];
    int flags[SPLINE_JUNCTION_EXIT_MAX];
    LineRange *a;
    int n, i, j;

    stc_range_num = Spline_GetCount();
    if (stc_range_num <= 0 || stc_range_num > LINE_RANGE_MAX)
        return 0;

    for (i = 0; i < stc_range_num; i++)
        CpuLine_RangeEnds(i, &stc_ranges[i]);

    for (i = 0; i < stc_range_num; i++)
    {
        a = &stc_ranges[i];
        a->succ_num = 0;
        a->on_loop = 0;
        n = Spline_GetJunctionExits(i, 1, exits, flags);
        for (j = 0; j < n && a->succ_num < LINE_SUCC_MAX; j++)
        {
            if (flags[j] || exits[j] == i || exits[j] < 0 || exits[j] >= stc_range_num)
                continue;
            a->succ[a->succ_num++] = exits[j];
        }
    }
    return 1;
}

// The continuation that turns least.
static int CpuLine_Straightest(int r)
{
    LineRange *a = &stc_ranges[r];
    int best = -1;
    float best_dot = 0;
    float dot;
    int i;

    for (i = 0; i < a->succ_num; i++)
    {
        dot = VECDotProduct(&a->dir_end, &stc_ranges[a->succ[i]].dir_start);
        if (best < 0 || dot > best_dot)
        {
            best = a->succ[i];
            best_dot = dot;
        }
    }
    return best;
}

// Walks the straightest continuations from start, marking the lap. Returns the range count.
static int CpuLine_WalkLap(int start, s16 *order)
{
    float s = 0;
    int num = 0;
    int r = start;

    stc_loop = 0;
    while (r >= 0 && num < stc_range_num)
    {
        stc_ranges[r].on_loop = 1;
        stc_ranges[r].s_start = s;
        s += stc_ranges[r].len;
        order[num++] = r;
        r = CpuLine_Straightest(r);
        if (r == start)
        {
            stc_loop = 1;
            break;
        }
        if (r >= 0 && stc_ranges[r].on_loop)
            break;
    }
    stc_lap = s;
    return num;
}

// Every range takes at least one sample, so the spacing leaves one slot per range spare.
static int CpuLine_Sample(s16 *order, int order_num)
{
    float fit = stc_lap / (LINE_SAMPLE_MAX - order_num);
    float spacing = fit > LINE_SPACING ? fit : LINE_SPACING;
    HSD_Spline *fwd, *bwd;
    float t;
    int r, n, i, j;

    stc_l = HSD_MemAlloc(sizeof(Vec3) * LINE_SAMPLE_MAX);
    stc_r = HSD_MemAlloc(sizeof(Vec3) * LINE_SAMPLE_MAX);
    stc_a = HSD_MemAlloc(sizeof(float) * LINE_SAMPLE_MAX);
    stc_curv = HSD_MemAlloc(sizeof(float) * LINE_SAMPLE_MAX);
    if (!stc_l || !stc_r || !stc_a || !stc_curv)
        return 0;

    stc_num = 0;
    for (i = 0; i < order_num; i++)
    {
        r = order[i];
        fwd = Spline_GetForward(r);
        bwd = Spline_GetBackward(r);
        n = (int)(stc_ranges[r].len / spacing);
        if (n < 1)
            n = 1;
        for (j = 0; j < n && stc_num < LINE_SAMPLE_MAX; j++)
        {
            t = (float)j / n;
            splArcLengthPoint(&stc_l[stc_num], fwd, t);
            splArcLengthPoint(&stc_r[stc_num], bwd, t);
            stc_a[stc_num] = 0.5f;
            stc_num++;
        }
    }
    return stc_num >= 3;
}

// One pass pulling each point to the midpoint of its neighbours, kept on its cross-section
// and off the edges: the line straightens until it touches the inside of each corner.
static void CpuLine_Relax(void)
{
    Vec3 prev, next, mid, d, rel;
    float width2, margin, a;
    int lo = stc_loop ? 0 : 1;
    int hi = stc_loop ? stc_num : stc_num - 1;
    int i;

    for (i = lo; i < hi; i++)
    {
        CpuLine_Point(CpuLine_Wrap(i - 1), &prev);
        CpuLine_Point(CpuLine_Wrap(i + 1), &next);
        VECAdd(&prev, &next, &mid);
        VECScale(&mid, &mid, 0.5f);
        VECSubtract(&stc_r[i], &stc_l[i], &d);
        width2 = VECDotProduct(&d, &d);
        if (width2 <= 0)
            continue;
        VECSubtract(&mid, &stc_l[i], &rel);
        a = VECDotProduct(&rel, &d) / width2;
        margin = LINE_EDGE_UNITS / sqrtf(width2);
        if (margin < LINE_EDGE_MIN)
            margin = LINE_EDGE_MIN;
        if (margin > LINE_EDGE_MAX)
            margin = LINE_EDGE_MAX;
        if (a < margin)
            a = margin;
        if (a > 1.0f - margin)
            a = 1.0f - margin;
        stc_a[i] = a;
    }
}

// Turn per unit at each sample, then the sharpest of the next few so a corner reads sharp
// on its approach.
static void CpuLine_Curvature(void)
{
    Vec3 p0, p1, p2, d1, d2;
    float l1, l2, cosv;
    int i, k;

    for (i = 0; i < stc_num; i++)
    {
        CpuLine_Point(CpuLine_Wrap(i - 1), &p0);
        CpuLine_Point(i, &p1);
        CpuLine_Point(CpuLine_Wrap(i + 1), &p2);
        VECSubtract(&p1, &p0, &d1);
        VECSubtract(&p2, &p1, &d2);
        l1 = VECMag(&d1);
        l2 = VECMag(&d2);
        stc_curv[i] = 0;
        if (l1 <= 0 || l2 <= 0)
            continue;
        cosv = VECDotProduct(&d1, &d2) / (l1 * l2);
        if (cosv > 1.0f)
            cosv = 1.0f;
        if (cosv < -1.0f)
            cosv = -1.0f;
        stc_curv[i] = (float)acos(cosv) / (0.5f * (l1 + l2));
    }
    for (i = 0; i < stc_num; i++)
    {
        for (k = 1; k <= LINE_CURV_SPREAD; k++)
        {
            if (stc_curv[CpuLine_Wrap(i + k)] > stc_curv[i])
                stc_curv[i] = stc_curv[CpuLine_Wrap(i + k)];
        }
    }
}

static int CpuLine_Build(int start_range)
{
    s16 order[LINE_RANGE_MAX];
    int order_num, i;

    if (!CpuLine_BuildGraph() || start_range < 0 || start_range >= stc_range_num)
        return 0;
    order_num = CpuLine_WalkLap(start_range, order);
    if (!CpuLine_Sample(order, order_num))
        return 0;
    for (i = 0; i < LINE_ITERATIONS; i++)
        CpuLine_Relax();
    CpuLine_Curvature();
    return 1;
}

// Air Ride courses only, from the first CPU's range, when some CPU will follow the line.
void CpuLine_On3DLoadEnd(void)
{
    RiderData *rd;
    RiderData *start = NULL;
    int used = cpu_racing_line == CPU_LINE_ALL;
    int i;

    if (cpu_racing_line == CPU_LINE_OFF || Scene_GetCurrentMajor() != MJRKIND_AIR || Gm_IsAutoDemo())
        return;

    for (i = 0; i < PLY_NUM; i++)
    {
        rd = CpuAI_GetCpuRider(i);
        if (!rd || rd->cpu->target_primary < 0)
            continue;
        if (!start)
            start = rd;
        if (CpuAI_GetPreset(i) == CPU_AI_RACER)
            used = 1;
    }
    if (!start || !used)
        return;

    stc_ready = CpuLine_Build(start->cpu->target_primary);
    if (stc_ready)
        OSReport("[CustomAI] Racing line built: %d samples over %d ranges, lap %.0f%s\n", stc_num, stc_range_num,
                 stc_lap, stc_loop ? "" : " (open)");
}

// The scene's heap, and the line's buffers with it, are gone.
void CpuLine_On3DLoadStart(void)
{
    int i;

    stc_ready = 0;
    stc_num = 0;
    stc_l = stc_r = NULL;
    stc_a = stc_curv = NULL;
    for (i = 0; i < PLY_NUM; i++)
        stc_cursor[i] = -1;
}

int CpuLine_IsReady(void)
{
    return stc_ready;
}

// The sample nearest pos, searched around the rider's cursor and from scratch when lost.
static int CpuLine_Nearest(int ply, Vec3 *pos, int ahead_only)
{
    int cur = stc_cursor[ply];
    int best = -1;
    float best_dist = 0;
    float dist;
    Vec3 p;
    int lo, hi, i, j;

    if (cur >= 0)
    {
        lo = ahead_only ? 0 : -LINE_SEARCH;
        hi = LINE_SEARCH;
        for (i = lo; i <= hi; i++)
        {
            j = CpuLine_Wrap(cur + i);
            CpuLine_Point(j, &p);
            dist = VECSquareDistance(&p, pos);
            if (best < 0 || dist < best_dist)
            {
                best = j;
                best_dist = dist;
            }
        }
        if (best_dist < LINE_LOST_DIST * LINE_LOST_DIST)
            return best;
    }

    best = -1;
    for (j = 0; j < stc_num; j++)
    {
        CpuLine_Point(j, &p);
        dist = VECSquareDistance(&p, pos);
        if (best < 0 || dist < best_dist)
        {
            best = j;
            best_dist = dist;
        }
    }
    return best;
}

// How strongly a CPU follows the line: Racers fully, others by skill under All CPUs. Off the
// lap, on a branch, nothing pulls it back toward the lap's road.
static float CpuLine_Weight(RiderData *rd)
{
    int cur = rd->cpu->target_primary;

    if (!stc_ready || rd->cpu->ai_state != CPUSTATE_CRUISE_PLUS || cur < 0 || cur >= stc_range_num ||
        !stc_ranges[cur].on_loop)
        return 0;
    if (CpuAI_GetPreset(rd->ply) == CPU_AI_RACER && cpu_racing_line != CPU_LINE_OFF)
        return 1.0f;
    if (cpu_racing_line == CPU_LINE_ALL)
        return LINE_WEIGHT_MIN + (1.0f - LINE_WEIGHT_MIN) * Rider_CPUDifficultyScale(rd);
    return 0;
}

// u along a cross-section from right (0) to left (1), moved into the nearest free gap.
static float CpuLine_SnapToGap(CpuRouteGap *gap, float u)
{
    float best = u;
    float best_d = -1.0f;
    float d, v;
    int i;

    if (gap->num <= 0)
        return u;
    for (i = 0; i < gap->num; i++)
    {
        if (u >= gap->lo[i] && u <= gap->hi[i])
            return u;
        v = u < gap->lo[i] ? gap->lo[i] : gap->hi[i];
        d = _fabs(v - u);
        if (best_d < 0 || d < best_d)
        {
            best = v;
            best_d = d;
        }
    }
    return best;
}

// Rider_CPUDecideCruisePlus after the route's waypoints are placed: moves waypoints 1-3 onto
// the line, within the free gaps vanilla found. The route is then classified from the moved
// points, so steering and corner checks follow them.
static void CpuLine_PlaceRoute(RiderData *rd, CpuRoute *route)
{
    float w = CpuLine_Weight(rd);
    Vec3 p, across, rel, target;
    float width2, u;
    int k, j;

    if (w <= 0)
        return;

    stc_cursor[rd->ply] = CpuLine_Nearest(rd->ply, &rd->pos, 0);
    for (k = 1; k < 4; k++)
    {
        j = CpuLine_Nearest(rd->ply, &route->mid[k], 1);
        if (j < 0)
            return;
        CpuLine_Point(j, &p);
        VECSubtract(&route->left[k], &route->right[k], &across);
        width2 = VECDotProduct(&across, &across);
        if (width2 <= 0)
            continue;
        VECSubtract(&p, &route->right[k], &rel);
        u = VECDotProduct(&rel, &across) / width2;
        if (u < 0)
            u = 0;
        if (u > 1.0f)
            u = 1.0f;
        u = CpuLine_SnapToGap(&route->gap[k - 1], u);
        VECScale(&across, &target, u);
        VECAdd(&route->right[k], &target, &target);
        route->point[k].X += (target.X - route->point[k].X) * w;
        route->point[k].Y += (target.Y - route->point[k].Y) * w;
        route->point[k].Z += (target.Z - route->point[k].Z) * w;
    }
}

// Yaw per frame the machine can hold, radians.
static float CpuLine_TurnRate(MachineData *md)
{
    if (md->is_bike)
        return LINE_BIKE_TURN_RATE;
    return md->attr->handling.turn_rate_top;
}

// Brakes (ChargeHold) before a corner the current speed cannot take, instead of on the
// ninth frame of turning into it.
void CpuLine_Brake(RiderData *rd)
{
    CpuData *cpu = rd->cpu;
    GOBJ *m = rd->machine_gobj;
    float speed, omega, limit;
    int cur, steps, i;

    if (CpuLine_Weight(rd) <= 0 || !m || CpuAI_IsUrgent(cpu->maneuver) || cpu->maneuver == CPUMAN_CHARGE_HOLD ||
        !rd->round_started || !MachineGObj_CPUCanBrake(m) || !MachineGObj_CPUCanChargeHold(m))
        return;
    cur = stc_cursor[rd->ply];
    if (cur < 0)
        return;

    speed = CpuSense_Self(rd->ply)->speed;
    omega = CpuLine_TurnRate(m->userdata);
    if (omega < LINE_TURN_RATE_MIN)
        return;
    omega *= LINE_GRIP;
    steps = (int)(speed * LINE_BRAKE_FRAMES / (stc_lap / stc_num));
    for (i = 1; i <= steps; i++)
    {
        limit = omega / (stc_curv[CpuLine_Wrap(cur + i)] > LINE_CURV_MIN ? stc_curv[CpuLine_Wrap(cur + i)] : LINE_CURV_MIN);
        if (limit < speed * (1.0f - LINE_BRAKE_MARGIN))
        {
            cpu->maneuver = CPUMAN_CHARGE_HOLD;
            return;
        }
    }
}

// Units a branch starting at exit adds over the lap's own way to where it rejoins the lap,
// negative for a shortcut. FLT_MAX when it never rejoins, or rejoins behind the junction,
// which would read as a shortcut of nearly a lap.
static float CpuLine_BranchExtra(int exit, float s_junction)
{
    float len = 0;
    float gain;
    int r = exit;
    int i;

    for (i = 0; i < LINE_BRANCH_STEPS && r >= 0; i++)
    {
        if (stc_ranges[r].on_loop)
        {
            gain = stc_ranges[r].s_start - s_junction;
            if (gain < 0)
                gain += stc_lap;
            return gain > stc_lap * 0.5f ? FLT_MAX : len - gain;
        }
        len += stc_ranges[r].len;
        r = CpuLine_Straightest(r);
    }
    return FLT_MAX;
}

// Rider_CPURankJunctionExits' (0x80264510) Rider_CPUGetPreferredFork call (0x8026454c).
// Vanilla's per-course picks stand; otherwise a CPU on the line takes a branch that saves
// distance.
static int CpuLine_PreferredFork(RiderData *rd, int *ranges, int count, int *out)
{
    int cur = rd->cpu->target_primary;
    float s_junction, extra, base, best_extra;
    int best = -1;
    int i;

    if (Rider_CPUGetPreferredFork(rd, ranges, count, out))
        return 1;
    if (CpuLine_Weight(rd) <= 0 || !stc_loop)
        return 0;

    s_junction = stc_ranges[cur].s_start + stc_ranges[cur].len;
    base = CpuLine_BranchExtra(CpuLine_Straightest(cur), s_junction);
    best_extra = base;
    for (i = 0; i < count; i++)
    {
        if (ranges[i] < 0 || ranges[i] >= stc_range_num || !grRangeSpline_IsEnabled(*stc_grobj, ranges[i]))
            continue;
        extra = CpuLine_BranchExtra(ranges[i], s_junction);
        if (extra < best_extra)
        {
            best = ranges[i];
            best_extra = extra;
        }
    }
    if (best < 0 || base - best_extra < LINE_SHORTCUT_GAIN)
        return 0;
    *out = best;
    return 1;
}

// The line's points from the rider's cursor on, for the debug view.
int CpuLine_GetPoints(int ply, Vec3 *out, int max)
{
    int cur = ply >= 0 && ply < PLY_NUM ? stc_cursor[ply] : 0;
    int n = 0;

    if (!stc_ready)
        return 0;
    if (cur < 0)
        cur = 0;
    while (n < max && n < stc_num)
    {
        CpuLine_Point(CpuLine_Wrap(cur + n), &out[n]);
        n++;
    }
    return n;
}

// Rider_CPUDecideCruisePlus (0x80271b24) `mr r3,r28` before classifying the route: r30 is
// the RiderData, r28 the route and nothing volatile is live.
CODEPATCH_HOOKCREATE(0x80271e58,
    "mr 3, 30\n\t"
    "mr 4, 28\n\t",
    CpuLine_PlaceRoute,
    "",
    0)

void CpuLine_InstallHooks(void)
{
    CODEPATCH_HOOKAPPLY(0x80271e58);
    CODEPATCH_REPLACECALL(0x8026454c, CpuLine_PreferredFork);
}

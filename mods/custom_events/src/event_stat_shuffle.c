#include <stdio.h>
#include <string.h>

#include "game.h"
#include "os.h"
#include "hsd.h"
#include "item.h"
#include "machine.h"
#include "rider.h"
#include "inline.h"

#include "event_stat_shuffle.h"

typedef struct ShuffleSlot
{
    float own[PATCHKIND_NUM];      // the player's stats before the shuffle
    float borrowed[PATCHKIND_NUM]; // the stats the shuffle handed them
} ShuffleSlot;

static ShuffleSlot slot[PLY_NUM];
static u8 pending_mask; // slots still holding borrowed stats
static int shuffled_num;
static int on_foot_num; // restored without a machine, so no patch cap applied

static int TakesPart(int ply)
{
    return Ply_GetPKind(ply) != PKIND_NONE;
}

int StatShuffle_CanStart(void)
{
    int n = 0;
    for (int ply = 0; ply < PLY_NUM; ply++)
        n += TakesPart(ply);
    return n >= 2;
}

// The ridden machine's stats are the source the game copies down to the rider and the
// player slot each frame; on foot it is the rider's.
static void GetLiveStats(int ply, float *out)
{
    GOBJ *rg = Ply_GetRiderGObj(ply);
    if (!rg)
    {
        Ply_GetAllStats(ply, out);
        return;
    }

    RiderData *rd = rg->userdata;
    const float *src = rd->machine_gobj ? ((MachineData *)rd->machine_gobj->userdata)->stats.values
                                        : rd->stats.values;
    memcpy(out, src, sizeof(float) * PATCHKIND_NUM);
}

// Like a patch pickup: a higher HP stat raises current HP, a lower one clamps it.
static void SetLiveStats(int ply, float *stats)
{
    GOBJ *rg = Ply_GetRiderGObj(ply);
    if (rg)
    {
        RiderData *rd = rg->userdata;
        memcpy(rd->stats.values, stats, sizeof(rd->stats.values));
        if (rd->machine_gobj)
        {
            MachineData *md = rd->machine_gobj->userdata;
            memcpy(md->stats.values, stats, sizeof(md->stats.values));
            Machine_UpdateAppearance(md);
            if (!md->suppress_attr_recalc)
                Machine_AdjustAttributes(md);
        }
    }
    Ply_SetAllStats(ply, stats);
}

// A uniformly random derangement: Fisher-Yates, redrawn while anyone draws their own
// index. At least a third of the draws are kept.
static void Derange(int *perm, int n)
{
    int fixed;
    do
    {
        for (int i = 0; i < n; i++)
            perm[i] = i;
        RandomShuffle(perm, n, sizeof(perm[0]));

        fixed = 0;
        for (int i = 0; i < n; i++)
            fixed |= perm[i] == i;
    } while (fixed);
}

void StatShuffle_Start(void)
{
    int plys[PLY_NUM];
    int n = 0;
    for (int ply = 0; ply < PLY_NUM; ply++)
    {
        if (TakesPart(ply))
            plys[n++] = ply;
    }

    pending_mask = 0;
    shuffled_num = 0;
    on_foot_num = 0;
    // Derange never finishes below two.
    if (n < 2)
        return;

    for (int i = 0; i < n; i++)
        GetLiveStats(plys[i], slot[plys[i]].own);

    int perm[PLY_NUM];
    Derange(perm, n);

    char map[PLY_NUM * 7 + 1] = "";
    char *p = map;
    for (int i = 0; i < n; i++)
    {
        ShuffleSlot *s = &slot[plys[i]];
        memcpy(s->borrowed, slot[plys[perm[i]]].own, sizeof(s->borrowed));
        SetLiveStats(plys[i], s->borrowed);
        pending_mask |= 1 << plys[i];
        p += sprintf(p, " P%d<-P%d", plys[i] + 1, plys[perm[i]] + 1);
    }
    shuffled_num = n;

    OSReport("[StatShuffle] Shuffled stats among %d players:%s\n", n, map);
}

// own + (current - borrowed): the player keeps what they gained or lost meanwhile.
static void RestoredStats(int ply, const float *cur, float *out)
{
    const ShuffleSlot *s = &slot[ply];
    float sum[PATCHKIND_NUM];
    for (int i = 0; i < PATCHKIND_NUM; i++)
        sum[i] = s->own[i] + cur[i] - s->borrowed[i];
    Machine_SetStatBlockClamped(out, sum);
}

// The gains replay through Machine_GivePatch, so a patch cap installed on it still
// holds; stat values are whole numbers, so the deltas are exact.
static void RestoreRiding(int ply, RiderData *rd)
{
    MachineData *md = rd->machine_gobj->userdata;
    const ShuffleSlot *s = &slot[ply];

    float cur[PATCHKIND_NUM];
    memcpy(cur, md->stats.values, sizeof(cur));
    memcpy(md->stats.values, s->own, sizeof(md->stats.values));
    for (int i = 0; i < PATCHKIND_NUM; i++)
        Machine_GivePatch(md, i, (int)(cur[i] - s->borrowed[i]));

    memcpy(rd->stats.values, md->stats.values, sizeof(rd->stats.values));
    Ply_SetAllStats(ply, md->stats.values);
}

static void RestoreOnFoot(int ply)
{
    float cur[PATCHKIND_NUM];
    float restored[PATCHKIND_NUM];
    GetLiveStats(ply, cur);
    RestoredStats(ply, cur, restored);
    SetLiveStats(ply, restored);
}

// A player on foot waits for a machine until `force`.
static void RestorePending(int force)
{
    if (!pending_mask)
        return;

    for (int ply = 0; ply < PLY_NUM; ply++)
    {
        if (!(pending_mask & (1 << ply)))
            continue;

        GOBJ *rg = Ply_GetRiderGObj(ply);
        RiderData *rd = rg ? rg->userdata : NULL;
        if (rd && rd->machine_gobj)
        {
            RestoreRiding(ply, rd);
        }
        else if (force)
        {
            RestoreOnFoot(ply);
            on_foot_num++;
        }
        else
        {
            continue;
        }
        pending_mask &= ~(1 << ply);
    }

    if (!pending_mask)
        OSReport("[StatShuffle] Restored own stats to %d players (%d on foot)\n", shuffled_num, on_foot_num);
}

void StatShuffle_End(void)
{
    RestorePending(0);
}

void StatShuffle_End2(void)
{
    RestorePending(1);
}

void StatShuffle_Abort(void)
{
    for (int ply = 0; ply < PLY_NUM; ply++)
    {
        if (!(pending_mask & (1 << ply)))
            continue;

        float cur[PATCHKIND_NUM];
        float restored[PATCHKIND_NUM];
        Ply_GetAllStats(ply, cur);
        RestoredStats(ply, cur, restored);
        Ply_SetAllStats(ply, restored);
    }
    pending_mask = 0;
}

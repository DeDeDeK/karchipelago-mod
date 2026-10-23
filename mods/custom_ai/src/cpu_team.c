#include "game.h"
#include "rider.h"
#include "os.h"
#include "inline.h"

#include "cpu_ai.h"
#include "cpu_strategist.h"
#include "cpu_team.h"

#define TEAM_CLAIM_MAX (PLY_NUM * 2)
// The other CPUs divide a claimed item's score by this.
#define TEAM_CLAIMED_SCORE_DIV 3

int cpu_teamwork = 1;

typedef struct CpuClaim
{
    void *key;
    u8 ply;
    float dist;
} CpuClaim;

static CpuClaim stc_claims[TEAM_CLAIM_MAX];
static int stc_claim_num;

static void CpuTeam_Claim(void *key, int ply, Vec3 *from, Vec3 *to)
{
    if (!key || stc_claim_num >= TEAM_CLAIM_MAX)
        return;
    stc_claims[stc_claim_num++] = (CpuClaim){.key = key, .ply = ply, .dist = VECDistance(from, to)};
}

// Rebuilt each tick from every city CPU's item target and its machine or goal target.
void CpuTeam_OnTick(void)
{
    RiderData *rd;
    CpuData *cpu;
    Vec3 pos;
    void *target;
    int i;

    stc_claim_num = 0;
    if (!cpu_teamwork || !Gm_IsInCity())
        return;

    for (i = 0; i < PLY_NUM; i++)
    {
        rd = CpuAI_GetCpuRider(i);
        if (!rd || !CpuAI_IsCityNavigate(rd))
            continue;
        cpu = rd->cpu;
        CpuTeam_Claim(cpu->item_target, i, &rd->pos, &cpu->item_target_pos);
        if (Strategist_GetTarget(i, &target, &pos))
            CpuTeam_Claim(target, i, &rd->pos, &pos);
        else
            CpuTeam_Claim(cpu->city_object, i, &rd->pos, &cpu->city_object_pos);
    }
}

// Another CPU is closer to key and already going for it.
int CpuTeam_IsClaimed(int ply, void *key, float dist)
{
    int i;

    for (i = 0; i < stc_claim_num; i++)
    {
        if (stc_claims[i].key == key && stc_claims[i].ply != ply && stc_claims[i].dist < dist)
            return 1;
    }
    return 0;
}

// Item scores for Rider_CPUScanItems: an item another CPU will reach first is worth less.
int CpuTeam_ItemScore(RiderData *rd, ItemData *it, int score)
{
    if (score <= 0 || !stc_claim_num || !CpuTeam_IsClaimed(rd->ply, it->item_gobj, VECDistance(&rd->pos, &it->pos)))
        return score;
    score /= TEAM_CLAIMED_SCORE_DIV;
    return score > 0 ? score : 1;
}

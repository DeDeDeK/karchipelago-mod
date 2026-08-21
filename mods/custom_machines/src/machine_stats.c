// Per-machine counters wide enough for the appended MachineKinds.
//
// PlayerStats' three int[0x1a] arrays are indexed by absolute MachineKind with no
// bounds check, so a custom machine's slot lands on the bike half of the range and
// past the eighth writes into the KO-by-cause counters, the vehicle-bust mask and
// the item tally. All three are relocated here, widened, and reached through five
// call replacements; the vanilla arrays are left to whatever the engine puts in them.

#include "os.h"
#include "obj.h"
#include "game.h"
#include "hurt.h"
#include "machine.h"
#include "code_patch/code_patch.h"

#include "custom_machines.h"

// machine_change_count, kills_by_machine, deaths_by_machine, in that order.
#define STAT_CHANGE 0
#define STAT_KILL   1
#define STAT_DEATH  2
#define STAT_NUM    3

#define STAT_PLY_NUM 5

static int stc_stats[STAT_PLY_NUM][STAT_NUM][CUSTOM_VCKIND_NUM];

// A vanilla kind that no vehicle-bust table entry names as the busted machine, and
// that City Trial never puts on the field. Ply_AddDeath indexes with whatever kind
// it is handed, so a custom machine's counting is done here and the engine call is
// sent here instead of past the end of its arrays.
#define STAT_SCAPEGOAT_KIND VCKIND_WHEELVSDEDEDE

// The absolute kind a machine counts under, appended kinds included. Vanilla stars
// keep their class slot, bikes sit at 19 and up, and a custom machine - a star at
// class slot 19 and up - takes its own MachineKind past those.
static int AbsoluteKind(int is_bike, int class_slot)
{
    if (is_bike)
        return class_slot + VCSTAR_NUM;
    if (class_slot >= VCSTAR_NUM)
        return VCKIND_NUM + (class_slot - VCSTAR_NUM);
    return class_slot;
}

static void Bump(int ply, int stat, int kind)
{
    if (ply >= 0 && ply < STAT_PLY_NUM && kind >= 0 && kind < CUSTOM_VCKIND_NUM)
        stc_stats[ply][stat][kind]++;
}

static int Total(int ply, int stat)
{
    int sum = 0;

    if (ply < 0 || ply >= STAT_PLY_NUM)
        return 0;
    for (int i = 0; i < CUSTOM_VCKIND_NUM; i++)
        sum += stc_stats[ply][stat][i];
    return sum;
}

// Replaces the bl at 0x801ba190 in AS_GetOnStar, whose r4 is the machine GObj the
// rider just mounted. The engine's own counter is left unwritten; its only reader
// is replaced below.
static void CountMachineChange(int ply, GOBJ *machine_gobj)
{
    MachineData *md = machine_gobj->userdata;

    Bump(ply, STAT_CHANGE, AbsoluteKind(md->is_bike, md->kind));
}

// Replaces the bl at 0x801e1f74 in Machine_GiveDamage. The counting is done here,
// with the widened kind; the engine still runs for everything else it does on a KO
// - the KO-by-cause counters, the vehicle-bust mask, the King Dedede frame - but a
// kind it has no bucket for is swapped out first.
static void AddDeath(int ply, DmgLog *dmg_log, int is_bike, MachineKind machine_kind)
{
    int kind = AbsoluteKind(is_bike, machine_kind);
    int attacker = dmg_log->attacker_ply;

    if (ply != attacker)
    {
        Bump(ply, STAT_DEATH, kind);
        Bump(attacker, STAT_KILL, kind);
    }

    if (kind >= VCKIND_NUM)
    {
        is_bike = 0;
        machine_kind = STAT_SCAPEGOAT_KIND;
    }
    Ply_AddDeath(ply, dmg_log, is_bike, machine_kind);
}

// Replaces the bl at 0x8004e6a8 in CityTrial_CheckFreeRunObjectives, which unlocks
// a checklist cell at ten machine changes.
static int GetMachineChangeCount(int ply)
{
    return Total(ply, STAT_CHANGE);
}

// Replaces the bl at 0x80012470 in Game_Think, which publishes the count as the
// Destruction Derby score.
static int GetKONum(int ply)
{
    return Total(ply, STAT_KILL);
}

// The engine clears its own arrays in Player_InitAll, which runs just after this.
void CustomMachineStats_On3DLoadStart(void)
{
    for (int p = 0; p < STAT_PLY_NUM; p++)
    {
        for (int s = 0; s < STAT_NUM; s++)
        {
            for (int i = 0; i < CUSTOM_VCKIND_NUM; i++)
                stc_stats[p][s][i] = 0;
        }
    }
}

void CustomMachineStats_OnBoot(void)
{
    CODEPATCH_REPLACECALL(0x801ba190, CountMachineChange);   // bl Ply_IncrementGetOnMachineNum
    CODEPATCH_REPLACECALL(0x801e1f74, AddDeath);             // bl Ply_AddDeath
    CODEPATCH_REPLACECALL(0x8004e6a8, GetMachineChangeCount); // bl Ply_GetMachineChangeCount
    CODEPATCH_REPLACECALL(0x80012470, GetKONum);             // bl Ply_GetKONum
    OSReport("[MachineStats] Per-machine counters widened to %d kinds\n", CUSTOM_VCKIND_NUM);
}

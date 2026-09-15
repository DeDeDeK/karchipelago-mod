// Per-machine counters wide enough for the appended MachineKinds. PlayerStats'
// machine_change_count and kills_by_machine are int[0x1a] indexed by the engine's
// absolute-kind fold with no bounds check, so a custom machine's appended slot counts
// under another kind or, far enough out, writes into the KO-by-cause counters, the
// vehicle-bust mask and the item tally. Both are relocated here and widened; the
// vanilla arrays are left to whatever the engine puts in them.

#include "os.h"
#include "obj.h"
#include "game.h"
#include "hurt.h"
#include "machine.h"
#include "code_patch/code_patch.h"

#include "custom_machines.h"

#define STAT_CHANGE 0
#define STAT_KILL   1
#define STAT_NUM    2

static int stc_stats[PLY_NUM][STAT_NUM][CUSTOM_VCKIND_NUM];

// A vanilla kind that no vehicle-bust table entry names as the busted machine, and
// that City Trial never puts on the field. Ply_AddDeath indexes with whatever kind
// it is handed, so a custom machine's counting is done here and the engine call is
// sent here instead of past the end of its arrays.
#define STAT_SCAPEGOAT_KIND VCKIND_WHEELVSDEDEDE

#define DEATH_HANDLER_MAX 4

static CustomMachineDeathHandler stc_death_handlers[DEATH_HANDLER_MAX];

static void Bump(int ply, int stat, int kind)
{
    if (ply >= 0 && ply < PLY_NUM && kind >= 0 && kind < CUSTOM_VCKIND_NUM)
        stc_stats[ply][stat][kind]++;
}

static int Total(int ply, int stat)
{
    int sum = 0;

    if (ply < 0 || ply >= PLY_NUM)
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

    Bump(ply, STAT_CHANGE, CustomMachines_KindFromClassIndex(md->is_bike, md->kind));
}

void CustomMachineStats_AddDeathHandler(CustomMachineDeathHandler handler)
{
    if (handler == NULL)
        return;
    for (int i = 0; i < DEATH_HANDLER_MAX; i++)
    {
        if (stc_death_handlers[i] == handler)
            return;
    }
    for (int i = 0; i < DEATH_HANDLER_MAX; i++)
    {
        if (stc_death_handlers[i] == NULL)
        {
            stc_death_handlers[i] = handler;
            return;
        }
    }
    OSReport("[MachineStats] Death handler list full\n");
}

// Replaces the bl at 0x801e1f74 in Machine_GiveDamage. The counting is done here,
// with the widened kind; the engine still runs for everything else it does on a KO
// - the KO-by-cause counters, the vehicle-bust mask, the King Dedede frame - but a
// kind it has no bucket for is swapped out first. This is the game's only call to
// the KO recorder, so every consumer that has to see KOs is told from here.
static void AddDeath(int ply, DmgLog *dmg_log, int is_bike, int class_slot)
{
    int kind = CustomMachines_KindFromClassIndex(is_bike, class_slot);
    int attacker = dmg_log->attacker_ply;

    if (ply != attacker)
        Bump(attacker, STAT_KILL, kind);

    if (kind >= VCKIND_NUM)
    {
        is_bike = MachineKind_IsBike(STAT_SCAPEGOAT_KIND);
        class_slot = MachineKind_ClassIndex(STAT_SCAPEGOAT_KIND);
    }
    Ply_AddDeath(ply, dmg_log, is_bike, class_slot);

    for (int i = 0; i < DEATH_HANDLER_MAX; i++)
    {
        if (stc_death_handlers[i] != NULL)
            stc_death_handlers[i](ply, dmg_log, kind);
    }
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
    for (int p = 0; p < PLY_NUM; p++)
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

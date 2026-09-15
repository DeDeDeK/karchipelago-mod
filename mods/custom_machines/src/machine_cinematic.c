// The legendary assembly cutscene, driven by a machine's own archive, by standing in at
// the bl sites where the engine's cinematic decides which archive preloads, which loads,
// which frees, and which machine the rider mounts. One run at a time, which is the
// engine's own limit: GameData.legendary_assembly_gobj holds a single controller.

#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "game.h"
#include "rider.h"
#include "stage.h"
#include "preload.h"
#include "machine.h"
#include "code_patch/code_patch.h"

#include "custom_machines.h"

// Where the registry's bits start in stc_assembled, past Dragoon's 0 and Hydra's 1.
#define ASSEMBLED_CUSTOM_BIT 2

static HSD_Archive *stc_arc;
static CustomMachineEntry *stc_running;

// Cutscenes that ran in the loaded scene. The engine frees an archive when its run ends
// while the preload table still hands the freed block back, so a second run loads a
// joint out of it.
static u16 stc_assembled;
_Static_assert(ASSEMBLED_CUSTOM_BIT + CUSTOM_MACHINE_MAX <= 16, "stc_assembled holds a bit per cutscene");

// Replaces the bl at 0x80283914 in LegendaryMachine_CreateAssembly, which returns the
// vsData public of VsDragoon.dat or VsHydra.dat. A machine's archive carries one of
// the same shape.
static LegendaryAssemblyData *LoadArchive(int machine_index)
{
    LegendaryAssemblyData *vsdata;

    if (stc_running == NULL)
        return LegendaryMachine_LoadAssemblyArchive(machine_index);

    lbLoadArchive(&stc_arc, stc_running->cine_file, &vsdata, stc_running->cine_symbol, 0);
    return vsdata;
}

// Replaces the bl at 0x80283c98, in phase 3 of LegendaryMachine_AssemblyThink
// (0x802839b8). The latch clears here rather than at the mount, so every seam
// downstream of the load still sees our run.
static void FreeArchive(int machine_index)
{
    if (stc_running == NULL)
    {
        LegendaryMachine_FreeAssemblyArchive(machine_index);
        return;
    }

    stc_running = NULL;
    Archive_Free(0, stc_arc);
    stc_arc = NULL;
}

// Replaces the bl at 0x80283b70 in LegendaryMachine_AssemblyThink (0x802839b8).
// Vanilla's Enter poses the rider and stages the (is_bike, class slot) pair the
// substate's motion script feeds to Rider_RespawnFullRecreate 150 frames later;
// overwriting that pair is the whole of pointing the mount at a different machine.
static void EnterAssembly(int ply, int machine_index)
{
    Ply_EnterLegendaryAssembly(ply, machine_index);

    CustomMachineEntry *e = stc_running;
    GOBJ *rg = Ply_GetRiderGObj(ply);
    if (e == NULL || rg == NULL)
        return;

    RiderData *rd = rg->userdata;
    rd->respawn_is_bike = e->is_bike;
    rd->respawn_class_slot = e->class_slot;
    rd->starting_machine_idx = (MachineKind)e->machine_kind;
}

// Replaces the bl LegendaryMachine_PreloadAssemblyArchives at 0x80262be8, the tail of
// Preload_AllCityFiles (0x80262ba4). A run loads its archive synchronously mid-round, so
// each machine's is queued with the vanilla pair, under the same arguments.
static void PreloadArchives(GroundKind gr_kind)
{
    LegendaryMachine_PreloadAssemblyArchives(gr_kind);
    if (gr_kind != GR_CITY1)
        return;

    for (int i = 0; i < CustomMachines_GetCount(); i++)
    {
        CustomMachineEntry *e = CustomMachines_GetEntry(i);
        if (e->cine_machine_index >= 0)
            Preload_CreateEntry(5, e->cine_file, 6, 6, 0, 1, 5, 0x20, 0);
    }
}

// lbLoadArchive panics on a public it cannot find, so each cutscene archive is loaded
// whole once here, where the boot arena takes it back.
static int CutsceneLoads(CustomMachineEntry *e)
{
    if (DVDConvertPathToEntrynum(e->cine_file) == -1)
        return 0;

    void *mark = HSD_ArenaMark();
    HSD_Archive *arc = Archive_LoadFile(e->cine_file);
    int ok = arc != NULL && Archive_GetPublicAddress(arc, e->cine_symbol) != NULL;
    HSD_ArenaRelease(mark);
    return ok;
}

void CustomMachineCinematic_On3DLoadStart(void)
{
    // The archive came off a per-scene heap the teardown has already reclaimed, so
    // there is nothing to free - only the handle to forget.
    stc_running = NULL;
    stc_arc = NULL;
    stc_assembled = 0;
}

int CustomMachineCinematic_Start(int machine_kind, int ply)
{
    CustomMachineEntry *e = CustomMachines_FindByKind(machine_kind);
    GOBJ *mg;
    GOBJ *rg;
    MachineData *md;
    LegendaryAssemblyParams params;
    int machine_index;
    int bit;

    if (e != NULL)
    {
        machine_index = e->cine_machine_index;
        bit = ASSEMBLED_CUSTOM_BIT + CustomMachines_Index(e);
    }
    else if (machine_kind == VCKIND_DRAGOON || machine_kind == VCKIND_HYDRA)
    {
        machine_index = machine_kind == VCKIND_HYDRA;
        bit = machine_index;
    }
    else
        return 0;

    if (machine_index < 0 || ply < 0 || ply >= PLY_NUM || (stc_assembled & (1 << bit)))
        return 0;

    // The cutscene stages its models on the open City Trial map and drives that
    // scene's sky and area lights, so a stadium or an Air Ride race dereferences a
    // null jobj or trips the area-light assert. The title demo runs a real City
    // Trial round, where a cutscene would take over the attract loop.
    if (!Gm_IsInCity() || Gm_IsAutoDemo())
        return 0;

    mg = Ply_GetMachineGObj(ply);
    rg = Ply_GetRiderGObj(ply);
    if (mg == NULL || rg == NULL || Gm_IsLegendaryAssembling() || stc_running != NULL)
        return 0;

    // Rider_EnterLegendaryAssembly (0x8019248c) is Kirby-only, and the mount rides
    // on the state it enters, so anyone else would get the whole shot and no machine.
    if (((RiderData *)rg->userdata)->kind != RDKIND_KIRBY)
        return 0;

    md = mg->userdata;
    params.machine_index = machine_index;
    params.ply = (u8)ply;
    params.pos = md->pos;
    params.forward = md->forward;
    params.up = md->up;

    stc_running = e;
    stc_assembled |= (u16)(1 << bit);
    if (e != NULL)
        Machine_ResetColAnims(md);

    LegendaryMachine_StartAssembly(&params);
    OSReport("[MachineCinematic] Player %d assembling the %s\n", ply + 1,
             e != NULL ? e->name : (machine_index == 0 ? "Dragoon" : "Hydra"));
    return 1;
}

void CustomMachineCinematic_OnBoot(void)
{
    int n = 0;

    for (int i = 0; i < CustomMachines_GetCount(); i++)
    {
        CustomMachineEntry *e = CustomMachines_GetEntry(i);
        if (e->cine_machine_index < 0)
            continue;

        if (!CutsceneLoads(e))
        {
            OSReport("[MachineCinematic] '%s' has no '%s' in %s, no cutscene\n", e->name,
                     e->cine_symbol, e->cine_file);
            e->cine_machine_index = -1;
            continue;
        }
        n++;
    }
    if (n == 0)
        return;

    CODEPATCH_REPLACECALL(0x80283914, LoadArchive);     // bl LegendaryMachine_LoadAssemblyArchive
    CODEPATCH_REPLACECALL(0x80283c98, FreeArchive);     // bl LegendaryMachine_FreeAssemblyArchive
    CODEPATCH_REPLACECALL(0x80283b70, EnterAssembly);   // bl Ply_EnterLegendaryAssembly
    CODEPATCH_REPLACECALL(0x80262be8, PreloadArchives); // bl LegendaryMachine_PreloadAssemblyArchives
    OSReport("[MachineCinematic] %d machine(s) with a cutscene, hooks installed\n", n);
}

// The legendary assembly cutscene, driven by a machine's own archives.
//
// Every machine-specific decision the engine's cinematic takes is at a bl, so a
// machine reaches the whole of it by standing in at three: which archive loads,
// which frees, and which machine the rider mounts. One run at a time, which is the
// engine's own limit - GameData+0xa8c holds a single controller GObj. Dragoon and
// Hydra are accepted here too and fall through to the engine's own archives.

#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "game.h"
#include "rider.h"
#include "machine.h"
#include "code_patch/code_patch.h"

#include "custom_machines.h"

static HSD_Archive *stc_glow_arc;
static HSD_Archive *stc_parts_arc;
static LegendaryAssemblyData stc_vsdata;
static CustomMachineEntry *stc_running;

// Vanilla legendaries assembled through this file in the loaded scene: bit 0 =
// Dragoon, bit 1 = Hydra.
static u8 stc_vanilla_assembled;

int CustomMachineCinematic_IsRunning(void)
{
    return stc_running != NULL || Gm_IsLegendaryAssembling();
}

// Replaces the bl at 0x80283914 in LegendaryMachine_CreateAssembly. Vanilla picks
// VsDragoon.dat or VsHydra.dat off the machine index and returns the archive's
// vsData: a glow-model triple, a parts-model triple and a pointer to the camera
// animation descriptor. A machine's two archives carry one half each, so the
// three-pointer block is assembled here.
static void *LoadArchive(int machine_index)
{
    CustomMachineEntry *e = stc_running;
    if (e == NULL)
        return LegendaryMachine_LoadAssemblyArchive(machine_index);

    void *glow = NULL;
    void *cam = NULL;
    void *parts = NULL;
    lbLoadArchive(&stc_glow_arc, e->cine_glow_file, &glow, e->cine_glow_symbol,
                  &cam, e->cine_cam_symbol, 0);
    lbLoadArchive(&stc_parts_arc, e->cine_parts_file, &parts, e->cine_parts_symbol, 0);
    if (glow == NULL || cam == NULL || parts == NULL)
    {
        OSReport("[MachineCinematic] '%s' archive load failed (glow %d cam %d parts %d)\n",
                 e->name, glow != NULL, cam != NULL, parts != NULL);
        stc_running = NULL;
        return LegendaryMachine_LoadAssemblyArchive(machine_index);
    }

    stc_vsdata.glow = (void **)glow;
    stc_vsdata.parts = (void **)parts;
    stc_vsdata.cam_anim = (void **)cam;
    return &stc_vsdata;
}

// Replaces the bl at 0x80283c98 in phase 3. The latch clears here rather than at
// the mount, so every seam downstream of the load still sees our run.
static void FreeArchive(int machine_index)
{
    if (stc_running == NULL)
    {
        LegendaryMachine_FreeAssemblyArchive(machine_index);
        return;
    }

    stc_running = NULL;
    if (stc_glow_arc != NULL)
    {
        Archive_Free(0, stc_glow_arc);
        stc_glow_arc = NULL;
    }
    if (stc_parts_arc != NULL)
    {
        Archive_Free(0, stc_parts_arc);
        stc_parts_arc = NULL;
    }
}

// Replaces the bl at 0x80283b70. Vanilla's Enter poses the rider and stages the
// (is_bike, class slot) pair the substate's motion script feeds to
// Rider_RespawnFullRecreate 150 frames later; overwriting that pair is the whole
// of pointing the mount at a different machine.
static void EnterAssembly(int ply, int machine_index)
{
    Ply_EnterLegendaryAssembly(ply, machine_index);

    CustomMachineEntry *e = stc_running;
    GOBJ *rg = Ply_GetRiderGObj(ply);
    if (e == NULL || rg == NULL)
        return;

    RiderData *rd = rg->userdata;
    rd->x944 = 0;
    rd->x948 = e->star_slot;
    rd->starting_machine_idx = (MachineKind)e->machine_kind;
}

void CustomMachineCinematic_On3DLoadStart(void)
{
    // The archives came off a per-scene heap the teardown has already reclaimed,
    // so there is nothing to free - only the handles to forget.
    stc_running = NULL;
    stc_glow_arc = NULL;
    stc_parts_arc = NULL;
    stc_vanilla_assembled = 0;
}

int CustomMachineCinematic_Start(int machine_kind, int ply)
{
    CustomMachineEntry *e = CustomMachines_FindByKind(machine_kind);
    GOBJ *mg;
    GOBJ *rg;
    MachineData *md;
    LegendaryAssemblyParams params;
    int machine_index;

    if (e != NULL)
        machine_index = e->cine_machine_index;
    else if (machine_kind == VCKIND_DRAGOON)
        machine_index = 0;
    else if (machine_kind == VCKIND_HYDRA)
        machine_index = 1;
    else
        return 0;

    if (machine_index < 0 || ply < 0 || ply >= 5)
        return 0;

    // The cutscene stages its models on the open City Trial map and drives that
    // scene's sky and area lights, so a stadium or an Air Ride race dereferences a
    // null jobj or trips the area-light assert.
    if (!Gm_IsInCity())
        return 0;

    // A machine's own archives are reloaded per run; the vanilla pair's are freed
    // when one ends and a second would load a joint out of the freed archive.
    if (e == NULL && (stc_vanilla_assembled & (1 << machine_index)))
        return 0;

    mg = Ply_GetMachineGObj(ply);
    rg = Ply_GetRiderGObj(ply);
    if (mg == NULL || rg == NULL || Gm_IsLegendaryAssembling() || stc_running != NULL)
        return 0;

    // Rider_EnterLegendaryAssembly (0x8019248c) is Kirby-only, and the mount rides
    // on the state it enters, so anyone else would get the whole shot and no
    // machine. They take the plain mount instead.
    if (((RiderData *)rg->userdata)->kind != RDKIND_KIRBY)
        return 0;

    md = mg->userdata;
    params.machine_index = machine_index;
    params.ply = (u8)ply;
    params.pos = md->pos;
    params.forward = md->forward;
    params.up = md->up;

    stc_running = e;
    if (e != NULL)
        Machine_ResetColAnims(md);
    else
        stc_vanilla_assembled |= (u8)(1 << machine_index);

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

        CustomMachinePreload_Add(e->cine_glow_file);
        CustomMachinePreload_Add(e->cine_parts_file);
        n++;
    }
    if (n == 0)
        return;

    CODEPATCH_REPLACECALL(0x80283914, LoadArchive);    // bl LegendaryMachine_LoadAssemblyArchive
    CODEPATCH_REPLACECALL(0x80283c98, FreeArchive);    // bl LegendaryMachine_FreeAssemblyArchive
    CODEPATCH_REPLACECALL(0x80283b70, EnterAssembly);  // bl Ply_EnterLegendaryAssembly
    OSReport("[MachineCinematic] %d machine(s) with a cutscene, hooks installed\n", n);
}

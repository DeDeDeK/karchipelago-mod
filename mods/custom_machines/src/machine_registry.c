// Widens both machine classes - the star class past its 19 slots and the bike class
// past its 7 - so registered machines load archives of their own.

#include "os.h"
#include "hsd.h"
#include "machine.h"
#include "code_patch/code_patch.h"

#include "custom_machines.h"

// Each class's {filename, symbol} pairs, vanilla then appended.
static char *stc_names[2][CUSTOM_VCSTAR_NUM * 2];

// Replaces stc_vcDataLookup. The bike row is as wide as the star row so both are
// indexed the same way.
static vcData *stc_vc_lookup[2][CUSTOM_VCSTAR_NUM];

// The lis / addi pair forming each star handler table inside the one function that
// reads it: Machine_Star_Init (0x801e7f3c), then Machine_Star_Think (0x801eacbc).
static const u32 stc_star_handler_sites[2][2] = {
    { 0x801e80dc, 0x801e80e0 },
    { 0x801eb524, 0x801eb528 },
};

// [Init, Think][class slot]. A custom star slot stays NULL until a consumer installs its
// own. The bike class dispatches through no per-kind table, so its handlers run from
// hooks on the epilogues of Machine_Wheel_Init and Machine_Wheel_Think.
static CustomMachineHandler stc_star_handlers[2][CUSTOM_VCSTAR_NUM];
static CustomMachineHandler stc_bike_handlers[2][CUSTOM_VCWHEEL_NUM];

// In registry order. Machine_AnimThink (0x801c618c) is shared by both classes and
// dispatches through nothing indexed by kind, so these run from a replacement of its
// last call.
static CustomMachineHandler stc_anim_handlers[CUSTOM_MACHINE_MAX];

int CustomMachineRegistry_SetHandler(CustomMachineHandlerSlot slot, int machine_kind,
                                     CustomMachineHandler fn)
{
    CustomMachineEntry *e = CustomMachines_FindByKind(machine_kind);
    if (e == NULL)
        return 0;

    if (slot == CUSTOM_MACHINE_HANDLER_ANIM)
        stc_anim_handlers[CustomMachines_Index(e)] = fn;
    else if (e->is_bike)
        stc_bike_handlers[slot][e->class_slot] = fn;
    else
        stc_star_handlers[slot][e->class_slot] = fn;
    return 1;
}

// Replaces the bl Machine_ColAnimThink at 0x801c6274, Machine_AnimThink's last call, which
// runs once per machine per frame after the ColAnim overlays and before the draw.
static void AnimThinkTail(MachineData *md)
{
    Machine_ColAnimThink(md);

    CustomMachineEntry *e = CustomMachines_FindByClassSlot(md->is_bike, md->kind);
    if (e != NULL && stc_anim_handlers[CustomMachines_Index(e)] != NULL)
        stc_anim_handlers[CustomMachines_Index(e)](md);
}

static void RunBikeHandler(CustomMachineHandlerSlot slot, MachineData *md)
{
    if (md->kind < CUSTOM_VCWHEEL_NUM && stc_bike_handlers[slot][md->kind] != NULL)
        stc_bike_handlers[slot][md->kind](md);
}

static void WheelInitTail(MachineData *md)
{
    RunBikeHandler(CUSTOM_MACHINE_HANDLER_INIT, md);
}

static void WheelThinkTail(MachineData *md)
{
    RunBikeHandler(CUSTOM_MACHINE_HANDLER_THINK, md);
}

// r31 holds the machine through Machine_Wheel_Init's epilogue, r29 through
// Machine_Wheel_Think's.
CODEPATCH_HOOKCREATE(0x801f3c70, "mr 3, 31\n\t", WheelInitTail, "", 0)
CODEPATCH_HOOKCREATE(0x801f597c, "mr 3, 29\n\t", WheelThinkTail, "", 0)

// Machine_CheckStuck (0x801d2b04) leaves Dragoon, Hydra and Wing Meta Knight out of City
// Trial's stuck check by class slot alone - 8, 4 and 17, whatever the class - and an
// appended bike can sit at 8 or 17. An appended slot is compared as one matching none.
static int StuckCheckSlot(MachineData *md)
{
    return md->kind >= (md->is_bike ? VCWHEEL_NUM : VCSTAR_NUM) ? 0xFF : md->kind;
}

// The first compare, with r29 the machine and r0 the slot just loaded. All three
// compares run on what this leaves in r0.
CODEPATCH_HOOKCREATE(0x801d2ba8, "mr 3, 29\n\t", StuckCheckSlot, "mr 0, 3\n\t", 0)

// Replaces vcData_InitLookup (0x801c6c68), the scene-entry reset.
static void InitLookup(void)
{
    for (int is_bike = 0; is_bike < 2; is_bike++)
    {
        for (int i = 0; i < CUSTOM_VCSTAR_NUM; i++)
            stc_vc_lookup[is_bike][i] = NULL;
        stc_vcDataKindStar[is_bike] = NULL;
    }
}

// A machine's animation bank names its own generators from CUSTOM_MACHINE_GENERATOR_BASE
// up, and they are installed past every other machine's, so each loaded copy takes the
// ids discovery resolved. Written whole rather than offset, so a copy that already took
// them is left as it is.
static void InstallParticleIds(int is_bike, int class_index)
{
    vcData *vc = stc_vc_lookup[is_bike][class_index];
    CustomMachineEntry *e = CustomMachines_FindByClassSlot(is_bike, class_index);

    if (e == NULL || vc == NULL || vc->anim == NULL)
        return;

    int num;
    int *slots = CustomMachines_ParticleSlots(is_bike, vc->anim, &num);
    for (int i = 0; i < num; i++)
        slots[i] = e->particle[i];
}

// Replaces Vehile_LoadFile (0x801c6d74). lbLoadArchive resolves each archive's
// public symbol straight into the destination pointer; both halves are skipped
// once their slot is filled, as in vanilla.
static void LoadFile(int is_bike, int class_index)
{
    if (is_bike < 0 || is_bike > 1 || class_index < 0 || class_index >= CUSTOM_VCSTAR_NUM)
        return;

    if (stc_vcDataKindStar[is_bike] == NULL)
    {
        char **pair = &stc_vcClassNameTable[is_bike * 2];
        lbLoadArchive(0, pair[0], &stc_vcDataKindStar[is_bike], pair[1], 0);
        CustomMachineAudio_OnClassLoad(is_bike);
    }

    if (stc_vc_lookup[is_bike][class_index] == NULL)
    {
        char **pair = &stc_vcNameTable[is_bike][class_index * 2];
        lbLoadArchive(0, pair[0], &stc_vc_lookup[is_bike][class_index], pair[1], 0);
        InstallParticleIds(is_bike, class_index);
    }
}

// Replaces MachineDesc_SetKindAndIsBikeFromMachineKind (0x801c857c), the
// MachineKind -> (is_bike, class slot) split shared by CityMachineSpawn_Create
// and 13 other sites.
static void SplitKind(MachineKind kind, int *out_is_bike, u8 *out_class_index)
{
    *out_class_index = (u8)CustomMachines_ClassIndexFromKind(kind, out_is_bike);
}

// Replaces Machine_EncodeVehicleKind (0x801c85a8), the reverse: (is_bike, class slot) ->
// absolute kind, behind Ply_GetVehicleKind, Free Run's machine counts and the checklist
// and finish-line compares against a vanilla kind. Vanilla adds 19 to a bike's slot only,
// which reads an appended star slot as a bike and an appended bike slot as another kind.
static int EncodeKind(int is_bike, int class_index)
{
    return CustomMachines_KindFromClassIndex(is_bike, (u8)class_index);
}

// Queues every registered machine's archive alongside the ones Machine_PreloadAll
// walks its 26-entry enable table for. Hooked at the branch out of that loop, so
// it runs once and only on the City Trial path that preloads every machine.
static void PreloadCustomMachines(void)
{
    for (int i = 0; i < CustomMachines_GetCount(); i++)
        Machine_PreloadArchive(CustomMachines_GetEntry(i)->path);
}

CODEPATCH_HOOKCREATE(0x801c8d8c,
    "",
    PreloadCustomMachines,
    "",
    0
)

// Repoint Machine_StoreVcDataPtr's (0x801c4f98) inline `stc_vcDataLookup[is_bike][kind]`
// read at stc_vc_lookup. Patching the arithmetic rather than hooking keeps the
// caller-saved registers the surrounding code still needs (r0, r4, r5) untouched.
static void PatchLookupBase(void)
{
    CustomMachines_RepointTable(0x801c4fd0, 0x801c4fe8, stc_vc_lookup);
    CustomMachines_SetImmediate(0x801c5034, CUSTOM_VCSTAR_NUM * 4); // mulli r7, r7, N
}

void CustomMachineRegistry_OnBoot(void)
{
    for (int is_bike = 0; is_bike < 2; is_bike++)
    {
        char **vanilla = stc_vcNameTable[is_bike];
        int n = is_bike ? VCWHEEL_NUM : VCSTAR_NUM;

        for (int i = 0; i < n * 2; i++)
            stc_names[is_bike][i] = vanilla[i];
    }
    for (int i = 0; i < CustomMachines_GetCount(); i++)
    {
        CustomMachineEntry *e = CustomMachines_GetEntry(i);
        stc_names[e->is_bike][e->class_slot * 2 + 0] = e->path;
        stc_names[e->is_bike][e->class_slot * 2 + 1] = e->symbol;
    }
    stc_vcNameTable[0] = stc_names[0];
    stc_vcNameTable[1] = stc_names[1];

    const MachineStarProc *vanilla_handlers[2] = {
        stc_machine_star_init_handler,
        stc_machine_star_think_handler,
    };
    for (int t = 0; t < 2; t++)
    {
        for (int i = 0; i < VCSTAR_NUM; i++)
            stc_star_handlers[t][i] = vanilla_handlers[t][i];

        CustomMachines_RepointTable(stc_star_handler_sites[t][0], stc_star_handler_sites[t][1],
                                    stc_star_handlers[t]);
    }

    PatchLookupBase();
    CODEPATCH_REPLACEFUNC(vcData_InitLookup, InitLookup);
    CODEPATCH_REPLACEFUNC(Vehile_LoadFile, LoadFile);
    CODEPATCH_REPLACEFUNC(MachineDesc_SetKindAndIsBikeFromMachineKind, SplitKind);
    CODEPATCH_REPLACEFUNC(Machine_EncodeVehicleKind, EncodeKind);
    CODEPATCH_HOOKAPPLY(0x801c8d8c); // Machine_PreloadAll tail
    CODEPATCH_REPLACECALL(0x801c6274, AnimThinkTail); // bl Machine_ColAnimThink

    if (CustomMachines_GetClassCount(1) > 0)
    {
        CODEPATCH_HOOKAPPLY(0x801f3c70); // Machine_Wheel_Init epilogue
        CODEPATCH_HOOKAPPLY(0x801f597c); // Machine_Wheel_Think epilogue
        CODEPATCH_HOOKAPPLY(0x801d2ba8); // Machine_CheckStuck slot compare
    }

    OSReport("[MachineRegistry] Classes widened for %d star(s) and %d bike(s)\n",
             CustomMachines_GetClassCount(0), CustomMachines_GetClassCount(1));
}

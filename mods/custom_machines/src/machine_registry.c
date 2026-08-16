// Widens the engine's star machine class from 19 slots to 19 + CUSTOM_MACHINE_MAX
// so registered machines can load archives of their own.
//
// Four engine tables are hard-sized at 19 star slots. The name table is reached
// through a pointer (stc_vcNameTable[is_bike]), so it is simply repointed at a
// wider copy. The loaded-archive table stc_vcDataLookup is a fixed array whose
// bike row starts right after its star row, so it is relocated wholesale into
// stc_vc_lookup; every read of the original goes through this file. The two
// machine-specific handler tables are relocated by rewriting the lis/addi pair
// inside their one reader.

#include "os.h"
#include "hsd.h"
#include "machine.h"
#include "code_patch/code_patch.h"

#include "custom_machines.h"

static char *stc_star_names[CUSTOM_VCSTAR_NUM * 2];

// Replaces stc_vcDataLookup. The bike row is as wide as the star row so both are
// indexed the same way; only slots 0-6 of it are ever filled.
static vcData *stc_vc_lookup[2][CUSTOM_VCSTAR_NUM];

// Each holds the machine-specific handler that Machine_Star_Init and
// Machine_Star_Think end by calling: the vanilla table, then the lis / addi pair
// that forms it inside the one function that reads it. Only Hydra, Formula, Wagon
// and Turbo have handlers; the other 15 slots are NULL, as is any custom slot
// whose donor is one of them.
static const u32 stc_handler_tables[2][3] = {
    { 0x804b15c0, 0x801e80dc, 0x801e80e0 }, // Machine_Star_Init
    { 0x804b160c, 0x801eb524, 0x801eb528 }, // Machine_Star_Think
};

static void (*stc_handlers[2][CUSTOM_VCSTAR_NUM])(MachineData *);

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
    }

    if (stc_vc_lookup[is_bike][class_index] == NULL)
    {
        char **pair = &stc_vcNameTable[is_bike][class_index * 2];
        lbLoadArchive(0, pair[0], &stc_vc_lookup[is_bike][class_index], pair[1], 0);
    }
}

// Replaces MachineDesc_SetKindAndIsBikeFromMachineKind (0x801c857c), the
// MachineKind -> (is_bike, class slot) split shared by CityMachineSpawn_Create
// and 13 other sites. Vanilla splits at 19; registered machines are star slots
// 19 and up, whose MachineKinds sit past VCKIND_NUM.
static void SplitKind(MachineKind kind, int *out_is_bike, u8 *out_class_index)
{
    CustomMachineEntry *e = CustomMachines_FindByKind(kind);
    if (e != NULL)
    {
        *out_is_bike = 0;
        *out_class_index = (u8)e->star_slot;
        return;
    }
    *out_is_bike = MachineKind_IsBike(kind);
    *out_class_index = (u8)MachineKind_ClassIndex(kind);
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

// Repoint Machine_StoreVcDataPtr's inline `stc_vcDataLookup[is_bike][kind]` read
// at stc_vc_lookup: the lis/addi pair that forms the base, plus the class-stride
// multiply. Patching the arithmetic rather than hooking keeps the caller-saved
// registers the surrounding code still needs (r0, r4, r5) untouched.
static void PatchLookupBase(void)
{
    CustomMachines_RepointTable(0x801c4fd0, 0x801c4fe8, stc_vc_lookup);
    CODEPATCH_REPLACEINSTRUCTION(0x801c5034, 0x1CE70000 | (CUSTOM_VCSTAR_NUM * 4)); // mulli r7, r7, N
}

void CustomMachineRegistry_OnBoot(void)
{
    // The vanilla pairs are copied rather than restated so the widened table
    // cannot drift from the DOL's.
    char **vanilla = stc_vcNameTable[0];
    for (int i = 0; i < VCSTAR_NUM * 2; i++)
        stc_star_names[i] = vanilla[i];

    for (int i = 0; i < CustomMachines_GetCount(); i++)
    {
        CustomMachineEntry *e = CustomMachines_GetEntry(i);
        stc_star_names[e->star_slot * 2 + 0] = e->path;
        stc_star_names[e->star_slot * 2 + 1] = e->symbol;
    }
    stc_vcNameTable[0] = stc_star_names;

    for (int t = 0; t < 2; t++)
    {
        void (**handlers)(MachineData *) = (void (**)(MachineData *))stc_handler_tables[t][0];

        for (int i = 0; i < VCSTAR_NUM; i++)
            stc_handlers[t][i] = handlers[i];

        for (int i = 0; i < CustomMachines_GetCount(); i++)
        {
            CustomMachineEntry *e = CustomMachines_GetEntry(i);
            // A star's MachineKind is its class slot, so this also rejects bikes.
            if (e->clone_kind >= 0 && e->clone_kind < VCSTAR_NUM)
                stc_handlers[t][e->star_slot] = handlers[e->clone_kind];
        }

        CustomMachines_RepointTable(stc_handler_tables[t][1], stc_handler_tables[t][2],
                                    stc_handlers[t]);
    }

    PatchLookupBase();
    CODEPATCH_REPLACEFUNC(vcData_InitLookup, InitLookup);
    CODEPATCH_REPLACEFUNC(Vehile_LoadFile, LoadFile);
    CODEPATCH_REPLACEFUNC(MachineDesc_SetKindAndIsBikeFromMachineKind, SplitKind);
    CODEPATCH_HOOKAPPLY(0x801c8d8c); // Machine_PreloadAll tail

    OSReport("[CustomMachines] Star class widened to %d slots for %d machine(s)\n",
             CUSTOM_VCSTAR_NUM, CustomMachines_GetCount());
}

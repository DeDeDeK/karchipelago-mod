// The vehicle particle bank generators a machine brings. Every Ptcl_LoadEfPtclVehicle
// points psGeneratorDesc[PTCL_BANK_VEHICLE] into the archive it just loaded, so each load
// the table is copied into one wide enough for every registered machine's generators past
// the bank's own, and the count raised to match. Ptcl_Alloc (0x8043294c) bounds an id by
// that count alone, emits nothing for a NULL entry, and hands a generator node the
// descriptor's program pointer once, at creation, which the registry's copies satisfy by
// outliving every scene.

#include "os.h"
#include "particle.h"
#include "code_patch/code_patch.h"

#include "custom_machines.h"

#define TRAIL_TABLE_MAX (CUSTOM_MACHINE_GENERATOR_BASE + CUSTOM_MACHINE_MAX * CUSTOM_MACHINE_GENERATOR_MAX)

// The bank's own entries are refilled per load; the machines' stay put.
static u8 *stc_table[TRAIL_TABLE_MAX];
static u32 stc_count;

static void InstallGenerators(void)
{
    u8 **descs = psGeneratorDesc[PTCL_BANK_VEHICLE];
    u32 count = psGeneratorCount[PTCL_BANK_VEHICLE];

    if (descs == NULL || descs == stc_table)
        return;

    // Machines number their generators from the base, so a bank holding more would
    // have its own ids taken over.
    if (count > CUSTOM_MACHINE_GENERATOR_BASE)
    {
        static int reported;

        if (!reported)
        {
            reported = 1;
            OSReport("[TrailBank] Vehicle bank holds %d generators, past the %d machines number theirs from - machine generators are off\n",
                     count, CUSTOM_MACHINE_GENERATOR_BASE);
        }
        return;
    }

    for (u32 i = 0; i < CUSTOM_MACHINE_GENERATOR_BASE; i++)
        stc_table[i] = i < count ? descs[i] : NULL;

    psGeneratorDesc[PTCL_BANK_VEHICLE] = stc_table;
    psGeneratorCount[PTCL_BANK_VEHICLE] = stc_count;
}

// Tail of Ptcl_LoadEfPtclVehicle, where every install path meets with the bank's
// descriptor table already in place.
CODEPATCH_HOOKCREATE(0x802354bc,
    "",
    InstallGenerators,
    "",
    0
)

void CustomMachineTrailBank_OnBoot(void)
{
    int installed = 0;

    stc_count = CUSTOM_MACHINE_GENERATOR_BASE;
    for (int i = 0; i < CustomMachines_GetCount(); i++)
    {
        CustomMachineEntry *e = CustomMachines_GetEntry(i);

        for (int k = 0; k < e->generator_count; k++)
        {
            struct PtclDesc *desc = (struct PtclDesc *)e->generator[k];

            if (e->generator_size[k] == 0)
                continue;

            // A copy is installed past psRelocDataBanks, so it takes that pass's rewrite.
            desc->flags = (desc->flags & ~PTCL_FLAGS_RELOC_MASK) | PTCL_FLAGS_RELOCATED;
            stc_table[e->generator_base + k] = (u8 *)desc;
            installed++;
        }
        if ((u32)(e->generator_base + e->generator_count) > stc_count)
            stc_count = e->generator_base + e->generator_count;
    }

    if (installed == 0)
        return;

    CODEPATCH_HOOKAPPLY(0x802354bc); // Ptcl_LoadEfPtclVehicle tail
    OSReport("[TrailBank] %d generator(s) installed at vehicle bank ids %d-%d\n",
             installed, CUSTOM_MACHINE_GENERATOR_BASE, stc_count - 1);
}

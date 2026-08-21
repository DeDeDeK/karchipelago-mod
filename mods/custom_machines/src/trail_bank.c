// Private copies of shared vehicle particle bank generators, so a machine can tint
// its exhaust without tinting every machine emitting the same index. Generators 3
// and 8 of EfPtclVehicle.dat's 52 go unreferenced by any vanilla machine. The copy
// is made at bank load because Ptcl_Alloc (0x8043294c) hands a generator node the
// descriptor's program pointer once, at creation, and the node keeps it for life.

#include <string.h>

#include "os.h"
#include "particle.h"
#include "code_patch/code_patch.h"

#include "custom_machines.h"

#define PTCL_BANK_VEHICLE 0
#define TRAIL_CLONE_MAX 4

// Descriptors sit back to back in the bank with no length in front of them, so a
// clone takes a fixed span instead of a measured one. This clears the largest
// generator in the vehicle bank with room over; the slack past a shorter one is
// still inside the loaded archive, and the program it copies ends at its own 0xff
// wherever that falls.
#define TRAIL_CLONE_SIZE 256

static u8 stc_clone[TRAIL_CLONE_MAX][TRAIL_CLONE_SIZE];
static u8 stc_src[TRAIL_CLONE_MAX];
static u8 stc_dst[TRAIL_CLONE_MAX];
static int stc_count;

static void InstallClones(void)
{
    u8 **descs = psGeneratorDesc[PTCL_BANK_VEHICLE];
    u32 count = psGeneratorCount[PTCL_BANK_VEHICLE];

    if (descs == NULL)
        return;

    for (int i = 0; i < stc_count; i++)
    {
        if (stc_src[i] >= count || stc_dst[i] >= count || descs[stc_src[i]] == NULL)
            continue;
        memcpy(stc_clone[i], descs[stc_src[i]], TRAIL_CLONE_SIZE);
        descs[stc_dst[i]] = stc_clone[i];
    }
}

// Tail of Ptcl_LoadEfPtclVehicle, at the mr that both install paths reach with
// the bank's descriptor table already in place.
CODEPATCH_HOOKCREATE(0x802354bc,
    "",
    InstallClones,
    "",
    0
)

void CustomMachineTrail_OnBoot(void)
{
    for (int i = 0; i < CustomMachines_GetCount(); i++)
    {
        CustomMachineEntry *e = CustomMachines_GetEntry(i);

        for (int k = 0; k < e->trail_clone_count && stc_count < TRAIL_CLONE_MAX; k++)
        {
            stc_src[stc_count] = e->trail_clone_src[k];
            stc_dst[stc_count] = e->trail_clone_dst[k];
            stc_count++;
        }
    }

    if (stc_count == 0)
        return;

    CODEPATCH_HOOKAPPLY(0x802354bc); // Ptcl_LoadEfPtclVehicle tail
    OSReport("[TrailBank] %d generator clone(s), hooks installed\n", stc_count);
}

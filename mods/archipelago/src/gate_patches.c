#include "game.h"
#include "os.h"

#include "main.h"
#include "gate_patches.h"
#include "inline.h"
#include "textbox_api.h"
#include "ap_announce.h"

// No OnBoot: the central spawn-table filter chain reads the predicate below.
int GatePatches_IsItemLocked(u8 it_kind)
{
    int pk = Item_KindToPatchKind(it_kind);
    return pk >= 0 && !(ap_save->patch_unlocked_mask & (1 << pk));
}

int GatePatches_UnlockPatch(PatchKind kind)
{
    if (kind >= PATCHKIND_NUM)
        return 0;

    ap_save->patch_unlocked_mask |= (1 << kind);
    OSReport("[GatePatches] Patch %d (%s) unlocked (mask = %s)\n",
             kind, PatchKind_Names[kind], MaskBits(ap_save->patch_unlocked_mask, 16));
    APAnnounce_Grant("Unlocked Patch: ", PatchKind_Names[kind], tb_api->PatchColors[kind], NULL);
    return 1;
}

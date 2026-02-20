#include "ability_item.h"
#include "main.h"
#include "textbox.h"

// give copy ability to every human rider
int Ability_GiveItem(CopyKind copy_kind) {
    for (int i = 0; i < 5; i++) {
        if (Ply_GetPKind(i) == PKIND_HMN) {
            GOBJ *rg = Ply_GetRiderGObj(i);
            RiderData *rd = rg->userdata;
            OSReport("Giving ability %d to player %d...\n", copy_kind, rd->ply);
            TextBox_Enqueue("Giving ability %d to player %d...\n", copy_kind, rd->ply);
            Rider_GiveAbility(rd, copy_kind);
            return 1;
        }
    }
    return 0;
}
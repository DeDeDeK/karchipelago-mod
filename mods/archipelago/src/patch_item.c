#include "game.h"
#include "patch_item.h"
#include "textbox.h"
#include "item.h"
#include "machine.h"
#include "os.h"

// Helper function to spawn a patch item near a position
// Based on PowerUp_SpawnFromSky assembly analysis
GOBJ *Patch_SpawnItem(ItemKind item_kind, Vec3 *pos) {
    SpawnItem spawn_desc = {0};

    // Set item kind
    spawn_desc.kind = item_kind;

    spawn_desc.lifetime = 0;

    // Set position (required)
    if (pos) {
        spawn_desc.pos = *pos;
    }

    // Set defaults based on PowerUp_SpawnFromSky assembly:
    spawn_desc.x8 = 1;          // r5 = 1 in the assembly
    spawn_desc.x30 = 1.0f;      // Scale factor (guessed)

    // These values come from the switch statement in PowerUp_SpawnFromSky
    // Using case 0 (r31 == 0) as default:
    spawn_desc.x4c = 2;         // Set at 0x800ece94
    spawn_desc.x50 = 0;         // Set at 0x800ece90

    // Velocity is NULL in PowerUp_SpawnFromSky (items fall with gravity)
    // spawn_desc.vel is already zeroed

    // Create the item
    GOBJ *item_gobj = Item_Create(&spawn_desc);

    if (item_gobj) {
        OSReport("Spawned item kind %d at (%.1f, %.1f, %.1f)\n",
                 item_kind, pos->X, pos->Y, pos->Z);
        Patch_DebugSpawnItem(&spawn_desc);
    } else {
        OSReport("Failed to spawn item kind %d\n", item_kind);
    }

    return item_gobj;
}

// Debug helper: Dump SpawnItem structure to console
void Patch_DebugSpawnItem(SpawnItem *s) {
    OSReport("=== SpawnItem Debug ===\n");
    OSReport("x0:   %d\n", s->x0);
    OSReport("kind: %d\n", s->kind);
    OSReport("x8:   %d\n", s->x8);
    OSReport("pos:  (%.1f, %.1f, %.1f)\n", s->pos.X, s->pos.Y, s->pos.Z);
    OSReport("x18:  (%.1f, %.1f, %.1f)\n", s->x18.X, s->x18.Y, s->x18.Z);
    OSReport("vel:  (%.1f, %.1f, %.1f)\n", s->vel.X, s->vel.Y, s->vel.Z);
    OSReport("x30:  %.2f\n", s->x30);
    OSReport("lifetime: %d\n", s->lifetime);
    OSReport("x38:  %d\n", s->x38);
    OSReport("x3c:  %d\n", s->x3c);
    OSReport("x40:  %d\n", s->x40);
    OSReport("x44:  %d\n", s->x44);
    OSReport("lifetime_var: %d\n", s->lifetime_var);
    OSReport("x4c:  %d\n", s->x4c);
    OSReport("x50:  %d\n", s->x50);
    OSReport("x54:  %d\n", s->x54);
    OSReport("flags: 0x%X\n", s->flags);
    OSReport("======================\n");
}

// give PatchKind to every human rider on a machine
int Patch_GiveItem(PatchKind kind, int num) {
    for (int i = 0; i < 4; i++) {
        if (Ply_GetPKind(i) == PKIND_HMN) {
            GOBJ *mg = Ply_GetMachineGObj(i);
            if (mg) {
                // TODO: find stat num locations
                MachineData *md = mg->userdata;
                Machine_GivePatch(md, kind, num);
                OSReport("Giving %d patches of kind %d to player %d...\n", num, kind, i);
                TextBox_AddMessage("Giving %d patches of kind %d to player %d...\n", num, kind, i);
            }
        }
    }
    return 1;
}

// give num of AllUp to every human rider on a machine
int Patch_AllUp_GiveItem(int num) {
    for (int i = 0; i < 4; i++) {
        if (Ply_GetPKind(i) == PKIND_HMN) {
            GOBJ *mg = Ply_GetMachineGObj(i);
            if (mg) {
                // TODO: find stat num locations
                MachineData *md = mg->userdata;
                Machine_GiveAllUp(md, num);
                OSReport("Giving %d all ups to player %d...\n", num, i);
                TextBox_AddMessage("Giving %d all ups to player %d...\n", num, i);
            }
        }
    }
    return 1;
}
#include "game.h"
#include "obj.h"
#include "weapon.h"

#include "custom_events.h"
#include "hazard_weapon.h"

#define HAZARD_HIDE_MAX 128

WeaponData *HazardWeapon_Create(WeaponKind kind, Vec3 *pos, GOBJ *owner)
{
    if (!wp_kind_data[kind])
        return NULL;

    WeaponDesc desc = {
        .kind = kind,
        .owner_gobj = owner,
        .pos = *pos,
        .forward = CustomEvents_RandomHeading(),
        .up = {0.0f, 1.0f, 0.0f},
        .scale = 1.0f,
        .type_flag = 1,
        .charge = 1.0f,
    };
    GOBJ *g = Weapon_Create(&desc);
    return g ? g->userdata : NULL;
}

// The caps count by kind, so each ownerless weapon wears WPKIND_BOMB for the call.
void HazardWeapon_EnforceMax(WeaponKind kind, void (*enforce)(void))
{
    WeaponData *hidden[HAZARD_HIDE_MAX];
    int num = 0;
    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_WEAPON]; g && num < HAZARD_HIDE_MAX; g = g->next)
    {
        WeaponData *wp = g->userdata;
        if (wp->kind == kind && !wp->owner_gobj)
        {
            wp->kind = WPKIND_BOMB;
            hidden[num++] = wp;
        }
    }

    enforce();

    for (int i = 0; i < num; i++)
        hidden[i]->kind = kind;
}

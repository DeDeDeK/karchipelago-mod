#ifndef GATE_TOPRIDE_ITEMS_H
#define GATE_TOPRIDE_ITEMS_H

// Bitmask indices for Top Ride items in the ItemMgr enabled mask (+0x24).
// Mystery (a2dIT21 "?") is NOT in the bitmask — it's always available as the
// roulette item. The 22 gateable items map to bits 0-21.
typedef enum TopRideItemKind
{
    TRITEM_HAMMER,          // 0  a2dIT1e AC_hammer
    TRITEM_GROW,            // 1  a2dIT01 AC_macron
    TRITEM_SPEEDUP,         // 2  a2dIT02 AC_speedUp
    TRITEM_SPEEDDOWN,       // 3  a2dIT03 AC_speedDown
    TRITEM_BOOST_SAW,       // 4  a2dIT04 AC_BoostUp_Missile (charge saw attack)
    TRITEM_CHARGEBOOST,     // 5  a2dIT0c AC_chargeUp
    TRITEM_INVINCIBLE,      // 6  a2dIT0d AC_muteki
    TRITEM_BUZZSAW,         // 7  a2dIT0a AC_Sdrill_kusudama
    TRITEM_SPEAR,           // 8  a2dIT05 AC_FrontSpeer
    TRITEM_FREEZE,          // 9  a2dIT1b AC_ice
    TRITEM_MISSILE,         // 10 a2dIT07 AC_BoostUp_Missile (projectile missile)
    TRITEM_FIRE,            // 11 a2dIT06 AC_AfterFlame
    TRITEM_NEEDLE,          // 12 a2dIT0b AC_Sdrill_kusudama
    TRITEM_BOMB,            // 13 a2dIT08 AC_bomb
    TRITEM_LANDMINE,        // 14 a2dIT10 AC_landbomb
    TRITEM_SENSORBOMB,      // 15 a2dIT11 AC_lanthanum
    TRITEM_MIKE,            // 16 a2dIT16 AC_mike
    TRITEM_CRACKER,         // 17 a2dIT12 AC_clakko
    TRITEM_METAKNIGHT,      // 18 a2dIT13 AC_meta
    TRITEM_SMOKESCREEN,     // 19 a2dIT17 AC_kemuron
    TRITEM_DIZZY,           // 20 a2dIT18 AC_piyo
    TRITEM_BACKWARD,        // 21 a2dIT20 AC_usiro
    TRITEM_NUM,
} TopRideItemKind;

void GateTopRideItems_OnBoot();
void GateTopRideItems_ApplyMask();
int GateTopRideItems_UnlockItem(TopRideItemKind kind);

#endif

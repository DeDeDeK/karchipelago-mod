#ifndef GATE_TOPRIDE_ITEMS_H
#define GATE_TOPRIDE_ITEMS_H

typedef enum TopRideItemKind
{
    TRITEM_MYSTERY,         // 0  a2dIT21 AC_hatena
    TRITEM_HAMMER,          // 1  a2dIT1e AC_hammer
    TRITEM_GROW,            // 2  a2dIT01 AC_macron
    TRITEM_SPEEDUP,         // 3  a2dIT02 AC_speedUp
    TRITEM_SPEEDDOWN,       // 4  a2dIT03 AC_speedDown
    TRITEM_MISSILE,         // 5  a2dIT04 AC_BoostUp_Missile
    TRITEM_CHARGEBOOST,     // 6  a2dIT0c AC_chargeUp
    TRITEM_INVINCIBLE,      // 7  a2dIT0d AC_muteki
    TRITEM_BUZZSAW,         // 8  a2dIT0a AC_Sdrill_kusudama
    TRITEM_SPEAR,           // 9  a2dIT05 AC_FrontSpeer
    TRITEM_FREEZE,          // 10 a2dIT1b AC_ice
    TRITEM_MISSILE_ALT,     // 11 a2dIT07 AC_BoostUp_Missile
    TRITEM_FIRE,            // 12 a2dIT06 AC_AfterFlame
    TRITEM_NEEDLE,          // 13 a2dIT0b AC_Sdrill_kusudama
    TRITEM_BOMB,            // 14 a2dIT08 AC_bomb
    TRITEM_LANDMINE,        // 15 a2dIT10 AC_landbomb
    TRITEM_SENSORBOMB,      // 16 a2dIT11 AC_lanthanum
    TRITEM_MIKE,            // 17 a2dIT16 AC_mike
    TRITEM_CRACKER,         // 18 a2dIT12 AC_clakko
    TRITEM_METAKNIGHT,      // 19 a2dIT13 AC_meta
    TRITEM_SMOKESCREEN,     // 20 a2dIT17 AC_kemuron
    TRITEM_DIZZY,           // 21 a2dIT18 AC_piyo
    TRITEM_BACKWARD,        // 22 a2dIT20 AC_usiro
    TRITEM_NUM,
} TopRideItemKind;

void GateTopRideItems_OnBoot();
void GateTopRideItems_ApplyMask();
int GateTopRideItems_UnlockItem(TopRideItemKind kind);

#endif

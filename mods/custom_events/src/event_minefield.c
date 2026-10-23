#include "game.h"
#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "weapon.h"
#include "code_patch/code_patch.h"

#include "custom_events.h"
#include "event_minefield.h"
#include "hazard_weapon.h"
#include "road_spot.h"

#define MINEFIELD_COUNT        40
#define MINEFIELD_TRIES        400
#define MINEFIELD_MIN_GAP      60.0f // twice the sensor radius
#define MINEFIELD_PLAYER_GAP   60.0f
#define MINEFIELD_EDGE_MARGIN  0.15f // share of the road width kept clear at each edge
#define MINEFIELD_DROP_HEIGHT  3.0f
#define MINEFIELD_FUSE_SPREAD  300 // frames the self-detonations are spread over

static int Minefield_IsClear(Vec3 *pos, Vec3 *placed, int num)
{
    for (int i = 0; i < num; i++)
    {
        if (VECSquareDistance(pos, &placed[i]) < MINEFIELD_MIN_GAP * MINEFIELD_MIN_GAP)
            return 0;
    }
    return CustomEvents_NearestRider(pos, MINEFIELD_PLAYER_GAP, 0) < 0;
}

// An ownerless sensor bomb dropped onto the floor, where SensorBomb_Land embeds it.
static int Minefield_Spawn(Vec3 *floor)
{
    Vec3 pos = {floor->X, floor->Y + MINEFIELD_DROP_HEIGHT, floor->Z};
    WeaponData *wp = HazardWeapon_Create(WPKIND_SENSORBOMB, &pos, NULL);
    if (!wp)
        return 0;

    // HELD would snap the bomb to its missing owner's hand on the first proc.
    Weapon_StateChange(wp, SENSOR_BOMB_STATE_ARMED_FLYING, 0.0f, 1.0f, 1);
    wp->kind_scratch.sensor_bomb.fuse -= HSD_Randi(MINEFIELD_FUSE_SPREAD);
    return 1;
}

void Minefield_Start(void)
{
    Vec3 placed[MINEFIELD_COUNT];
    int num = 0;
    for (int i = 0; i < MINEFIELD_TRIES && num < MINEFIELD_COUNT; i++)
    {
        Vec3 floor;
        if (!RoadSpot_PickFlatFloor(&floor, MINEFIELD_EDGE_MARGIN) ||
            !Minefield_IsClear(&floor, placed, num))
            continue;

        if (!CustomEvents_HeapOk())
        {
            OSReport("[Minefield] Stopped at the heap reserve\n");
            break;
        }
        if (!Minefield_Spawn(&floor))
            break;
        placed[num++] = floor;
    }

    OSReport("[Minefield] Laid %d mines\n", num);
}

// SensorBomb_EnforceMaxActive counts every owner's sensor bombs against a cap of 2,
// so a player's throw would fade all but two mines.
static void Minefield_EnforceMaxActive(void)
{
    HazardWeapon_EnforceMax(WPKIND_SENSORBOMB, SensorBomb_EnforceMaxActive);
}

// The bl SensorBomb_EnforceMaxActive in SensorBomb_TryThrow (0x80228f08).
void Minefield_InstallHooks(void)
{
    CODEPATCH_REPLACECALL(0x80228fa8, Minefield_EnforceMaxActive);
}

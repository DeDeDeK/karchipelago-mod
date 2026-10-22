#include "game.h"
#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "item.h"
#include "stage.h"
#include "inline.h"

#include "custom_events.h"
#include "event_treasure_rain.h"
#include "road_spot.h"
#include "item_offer.h"

#define TREASURE_EDGE_MARGIN  0.1f   // share of the road width kept clear at each edge
#define TREASURE_DROP_HEIGHT  150.0f // above the floor a drop lands on
#define TREASURE_INTERVAL     15     // frames between drops
#define TREASURE_MAX_LIVE     30     // rain items alive at once
#define TREASURE_HEADROOM     12     // items left room for under the live-item cap, for box contents and rider drops
#define TREASURE_SPAWN_TYPE   0x5452 // an ItemSpawnType no vanilla source uses, tagging the rain's items
#define TREASURE_SKY_ITEM_MAX 65     // the sky spawner's field cap; vanilla's is 45-60
#define TREASURE_SKY_TIME_MIN 10     // frames between sky spawns; vanilla's are 20-50
#define TREASURE_SKY_TIME_MAX 20

typedef struct TreasureDesc
{
    ItemKind kind;
    int weight;
} TreasureDesc;

static const TreasureDesc treasures[] = {
    {ITKIND_ACCEL, 10},
    {ITKIND_TOPSPEED, 10},
    {ITKIND_OFFENSE, 10},
    {ITKIND_DEFENSE, 10},
    {ITKIND_TURN, 10},
    {ITKIND_GLIDE, 10},
    {ITKIND_CHARGE, 10},
    {ITKIND_WEIGHT, 10},
    {ITKIND_HP, 10},
    {ITKIND_ALLUP, 12},
    {ITKIND_SPEEDMAX, 4},
    {ITKIND_OFFENSEMAX, 4},
    {ITKIND_DEFENSEMAX, 4},
    {ITKIND_CHARGEMAX, 4},
    {ITKIND_FOODMAXIMTOMATO, 5},
    {ITKIND_CANDY, 3},
};

static int weights[GetElementsIn(treasures)]; // this run's, 0 for a kind the round does not offer
static int weight_total;
static int spawn_timer;
static int drop_num;
static int boosted;              // the sky spawner runs on the event's fall desc
static ItemFallDesc saved_fall;  // the override it replaced
static int saved_override;

static int TreasureRain_CountLive(void)
{
    int num = 0;
    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_ITEM]; g; g = g->next)
        num += ((ItemData *)g->userdata)->spawn_type == TREASURE_SPAWN_TYPE;
    return num;
}

// Falls under item gravity and lands on the floor the creation's raycast finds.
static int TreasureRain_Drop(Vec3 *pos)
{
    Vec3 up = {0.0f, 1.0f, 0.0f};
    Vec3 forward = CustomEvents_RandomHeading();
    ItemKind kind = treasures[Gm_Roll(weights, GetElementsIn(treasures))].kind;

    ItemDesc desc;
    Item_InitDesc(&desc, kind, 1.0f, TREASURE_SPAWN_TYPE, pos, &up, &forward,
                  -1, -1, 1, ITEM_COLL_POINT, -1, -1);
    return CityItem_Create(&desc) != NULL;
}

// While GRBOXGENE_EVENT_FALLDESC is set the sky spawner reads cur_event_fall_desc in place
// of the match-progress table, the override CityEvent_ModifyItemFallDesc gives the
// vanilla events the stage lists.
static void TreasureRain_BoostSky(void)
{
    grBoxGeneInfo *info = *stc_grBoxGeneInfo;
    if (!info)
        return;

    saved_fall = info->cur_event_fall_desc;
    saved_override = info->event_active_flags & GRBOXGENE_EVENT_FALLDESC;
    boosted = 1;

    // Bound by the field's spawn slots, as the vanilla override is.
    int slots = info->item_area_pos_num + info->item_pos_num;
    info->cur_event_fall_desc = (ItemFallDesc){
        .item_max = TREASURE_SKY_ITEM_MAX < slots ? TREASURE_SKY_ITEM_MAX : slots,
        .spawn_time_min = TREASURE_SKY_TIME_MIN,
        .spawn_time_max = TREASURE_SKY_TIME_MAX,
    };
    info->event_active_flags |= GRBOXGENE_EVENT_FALLDESC;
    info->spawn_timer = 0;
}

void TreasureRain_Start(void)
{
    spawn_timer = 0;
    drop_num = 0;
    boosted = 0;

    weight_total = 0;
    int offered = 0;
    for (int i = 0; i < GetElementsIn(treasures); i++)
    {
        weights[i] = ItemOffer_IsOffered(treasures[i].kind) ? treasures[i].weight : 0;
        weight_total += weights[i];
        offered += weights[i] > 0;
    }

    TreasureRain_BoostSky();
    OSReport("[TreasureRain] Raining %d of %d kinds, sky item cap %s\n", offered,
             (int)GetElementsIn(treasures), boosted ? "raised" : "unchanged");
}

void TreasureRain_Active(void)
{
    if (weight_total == 0 || ++spawn_timer < TREASURE_INTERVAL)
        return;
    spawn_timer = 0;

    if (TreasureRain_CountLive() >= TREASURE_MAX_LIVE || !CityItem_CanSpawnNMore(TREASURE_HEADROOM) ||
        !CustomEvents_HeapOk())
        return;

    Vec3 floor, drop;
    if (RoadSpot_PickOpenSky(&floor, &drop, TREASURE_EDGE_MARGIN, TREASURE_DROP_HEIGHT))
        drop_num += TreasureRain_Drop(&drop);
}

// The sky spawner goes back to its own pace as the rain stops.
void TreasureRain_End(void)
{
    grBoxGeneInfo *info = *stc_grBoxGeneInfo;
    if (!boosted || !info)
        return;

    info->cur_event_fall_desc = saved_fall;
    info->event_active_flags = (info->event_active_flags & ~GRBOXGENE_EVENT_FALLDESC) | saved_override;
    boosted = 0;
}

void TreasureRain_End2(void)
{
    OSReport("[TreasureRain] Dropped %d items\n", drop_num);
}

#include <math.h>
#include <string.h>

#include "game.h"
#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "item.h"
#include "collision.h"
#include "inline.h"

#include "custom_events.h"
#include "event_jumping_items.h"

#define JUMP_SLOT_MAX     128 // above the 101 live items CityItem_Create allows
#define JUMP_DELAY_MIN    30  // frames an item rests between hops
#define JUMP_DELAY_MAX    120
#define JUMP_SPEED_MIN    0.4f
#define JUMP_SPEED_MAX    1.8f
#define JUMP_PITCH_MIN    40.0f // degrees above the horizon
#define JUMP_PITCH_MAX    88.0f
#define JUMP_AIRBORNE_MAX 48 // every airborne hop holds a CollData
#define JUMP_AIM_TRIES    4
#define JUMP_PROBE_UP     40.0f
#define JUMP_PROBE_DOWN   120.0f

typedef struct JumpSlot
{
    GOBJ *item;
    int delay; // frames until the hop
} JumpSlot;

// The resting items, rebuilt every frame in item-list order.
static JumpSlot slots[JUMP_SLOT_MAX];
static int slot_num;
static int hop_count;

// Boxes rest in the same state as every other family.
static int JumpingItems_IsResting(ItemData *id)
{
    return id->state == ITSTATE_POWERUP_REST && id->is_airborne == 0;
}

static int JumpingItems_CountAirborne(void)
{
    int num = 0;
    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_ITEM]; g; g = g->next)
    {
        if (((ItemData *)g->userdata)->is_airborne)
            num++;
    }
    return num;
}

// The item list keeps its order between frames, so the search starts after the
// previous match.
static int JumpingItems_TakeDelay(JumpSlot *prev, int num, GOBJ *item, int *hint)
{
    for (int n = 0; n < num; n++)
    {
        int i = (*hint + n) % num;
        if (prev[i].item == item)
        {
            *hint = i + 1;
            return prev[i].delay;
        }
    }
    return -1;
}

// A heading whose landing spot, predicted for flat ground, has floor under it that
// is neither the sea nor the barrier, so a long hop cannot strand an item. Writes
// the launch velocity, or returns 0 when no try finds one.
static int JumpingItems_Aim(ItemData *id, Vec3 *up, float speed, float pitch, Vec3 *vel)
{
    float v_up = speed * sinf(pitch);
    float v_side = speed * cosf(pitch);
    // The flight back to launch height, ignoring the fall-speed cap.
    float reach = v_side * 2.0f * v_up / CityItem_GetGravity(id);

    for (int i = 0; i < JUMP_AIM_TRIES; i++)
    {
        Vec3 side = CustomEvents_RandomHeading();
        float d = VECDotProduct(&side, up);
        side.X -= up->X * d;
        side.Y -= up->Y * d;
        side.Z -= up->Z * d;
        VECNormalize(&side, &side);

        Vec3 land = {
            id->pos.X + side.X * reach,
            id->pos.Y + side.Y * reach,
            id->pos.Z + side.Z * reach,
        };
        Vec3 from = {land.X + up->X * JUMP_PROBE_UP, land.Y + up->Y * JUMP_PROBE_UP, land.Z + up->Z * JUMP_PROBE_UP};
        Vec3 to = {land.X - up->X * JUMP_PROBE_DOWN, land.Y - up->Y * JUMP_PROBE_DOWN, land.Z - up->Z * JUMP_PROBE_DOWN};
        Vec3 hit;
        int tri = Raycast_Ground(&from, &to, &hit);
        if (tri < 0)
            continue;

        int ground = grGetGroundTypeFromTriangleID(tri);
        if (ground == GRCOLL_GROUNDTYPE_SEA || ground == GRCOLL_GROUNDTYPE_BARRIER)
            continue;

        vel->X = side.X * v_side + up->X * v_up;
        vel->Y = side.Y * v_side + up->Y * v_up;
        vel->Z = side.Z * v_side + up->Z * v_up;
        return 1;
    }
    return 0;
}

// A resting item's fall entry with a launch velocity, which is how CityItem_Throw
// throws a new one.
static void JumpingItems_Launch(GOBJ *g)
{
    ItemData *id = g->userdata;

    // A resting item stops refreshing its gravity.
    CityItem_UpdateGravity(id);
    Vec3 up = {-id->fall_dir.X, -id->fall_dir.Y, -id->fall_dir.Z};

    float speed = RandomInRangef(JUMP_SPEED_MIN, JUMP_SPEED_MAX);
    float pitch = MTXDegToRad(RandomInRangef(JUMP_PITCH_MIN, JUMP_PITCH_MAX));
    Vec3 vel;
    if (!JumpingItems_Aim(id, &up, speed, pitch, &vel))
        VECScale(&up, &vel, speed);

    ItemGObj_EnterFamilyFall(g);
    id->vel = vel;

    // Full collision for the flight, as a thrown item has; the landing frees it.
    if (!id->coll_data)
    {
        CityItem_AllocCollData(id, ITEM_COLL_MPCOLL);
        CityItem_InitCollData(id);
    }
    id->coll_kind = ITEM_COLL_MPCOLL;
}

void JumpingItems_Start(void)
{
    slot_num = 0;
    hop_count = 0;
}

void JumpingItems_Active(void)
{
    JumpSlot prev[JUMP_SLOT_MAX];
    int prev_num = slot_num;
    memcpy(prev, slots, prev_num * sizeof(JumpSlot));
    slot_num = 0;

    int airborne = JumpingItems_CountAirborne();
    int hint = 0;
    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_ITEM]; g && slot_num < JUMP_SLOT_MAX; g = g->next)
    {
        if (!JumpingItems_IsResting(g->userdata))
            continue;

        // An item that just landed, or was just spawned, waits a fresh delay.
        int delay = JumpingItems_TakeDelay(prev, prev_num, g, &hint);
        if (delay < 0)
            delay = RandomInRange(JUMP_DELAY_MIN, JUMP_DELAY_MAX);

        if (delay == 0 && airborne < JUMP_AIRBORNE_MAX)
        {
            JumpingItems_Launch(g);
            airborne++;
            hop_count++;
            continue;
        }

        slots[slot_num++] = (JumpSlot){g, delay > 0 ? delay - 1 : 0};
    }
}

void JumpingItems_End2(void)
{
    OSReport("[JumpingItems] Launched %d hops\n", hop_count);
}

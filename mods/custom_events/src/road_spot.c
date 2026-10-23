#include "hsd.h"
#include "stage.h"
#include "enemy.h"
#include "collision.h"
#include "inline.h"

#include "road_spot.h"

#define ROAD_SPOT_RAY_UP        4.0f
#define ROAD_SPOT_RAY_DOWN      12.0f
#define ROAD_SPOT_SKY_CLEARANCE 4.0f // the open-sky test stops this far above the floor
#define ROAD_SPOT_SKY_TRIES     8
#define ROAD_SPOT_MIN_NORMAL_Y  0.8f

int RoadSpot_PickFloor(Vec3 *out, float edge_margin)
{
    int pair = HSD_Randi(Spline_GetCount());
    float u = HSD_Randf();
    Vec3 left, right;
    splGetSplinePoint(&left, Spline_GetForward(pair), u);
    splGetSplinePoint(&right, Spline_GetBackward(pair), u);

    float t = RandomInRangef(edge_margin, 1.0f - edge_margin);
    float x = lerp(left.X, right.X, t);
    float y = lerp(left.Y, right.Y, t);
    float z = lerp(left.Z, right.Z, t);

    // The range splines run about 2 units above the floor.
    Vec3 from = {x, y + ROAD_SPOT_RAY_UP, z};
    Vec3 to = {x, y - ROAD_SPOT_RAY_DOWN, z};
    return Raycast_Ground(&from, &to, out);
}

// Ground types 26-30 are the three rails, the sea and the barrier, and whatever is placed
// on a moving floor is left floating when it moves away.
static int RoadSpot_IsFlat(GrCollTri *t)
{
    if ((t->flags & GRCOLL_FLAG_MOVING) || t->normal.Y < ROAD_SPOT_MIN_NORMAL_Y)
        return 0;

    int ground = (t->kind & GRCOLL_KIND_GROUNDTYPE_MASK) >> GRCOLL_KIND_GROUNDTYPE_SHIFT;
    return ground < GRCOLL_GROUNDTYPE_GRIND_RAIL || ground > GRCOLL_GROUNDTYPE_BARRIER;
}

int RoadSpot_PickFlatFloor(Vec3 *out, float edge_margin)
{
    int id = RoadSpot_PickFloor(out, edge_margin);
    return id >= 0 && RoadSpot_IsFlat(&Gr_GetCollTris()[id]);
}

int RoadSpot_PickOpenSky(Vec3 *floor, Vec3 *drop, float edge_margin, float height)
{
    for (int i = 0; i < ROAD_SPOT_SKY_TRIES; i++)
    {
        int id = RoadSpot_PickFloor(floor, edge_margin);
        if (id < 0 || (Gr_GetCollTris()[id].flags & GRCOLL_FLAG_MOVING))
            continue;

        *drop = (Vec3){floor->X, floor->Y + height, floor->Z};
        Vec3 to = {floor->X, floor->Y + ROAD_SPOT_SKY_CLEARANCE, floor->Z};
        Vec3 hit;
        if (Raycast_Any(drop, &to, &hit) < 0)
            return 1;
    }
    return 0;
}

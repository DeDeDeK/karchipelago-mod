#ifndef ROAD_SPOT_H
#define ROAD_SPOT_H

#include "structs.h"

// A random point between the two road edges of a random CPU range pair, keeping
// edge_margin of the road width clear at each edge, dropped onto the floor under it.
// Returns the floor triangle's index, or -1 when no floor is there.
int RoadSpot_PickFloor(Vec3 *out, float edge_margin);

// A RoadSpot_PickFloor floor that holds still, is fairly flat and is not a rail, the sea
// or the barrier. Returns 0 when the pick is none of those.
int RoadSpot_PickFlatFloor(Vec3 *out, float edge_margin);

// A road floor that is not moving and has open sky up to height above it, and the drop
// point height above it. Returns 0 when 8 picks find none.
int RoadSpot_PickOpenSky(Vec3 *floor, Vec3 *drop, float edge_margin, float height);

#endif // ROAD_SPOT_H

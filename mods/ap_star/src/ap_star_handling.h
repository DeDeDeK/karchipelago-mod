#ifndef AP_STAR_HANDLING_H
#define AP_STAR_HANDLING_H

#include "structs.h"

// Six handling profiles for the Archipelago Star, one per surviving pod. Firing a
// pod - or growing the ring back - re-derives the machine on the next profile
// down the ladder, so how the star drives is readable off how many pods it has
// left. Gated by ap_star_settings.handling_enabled.

#define AP_STAR_PROFILE_NUM 6

// Drop the profile blocks. They are rebuilt from the star's own vcData the first
// time a machine needs one, since a scene load moves the archive.
void ApStarHandling_On3DLoadEnd(void);

// Profile for a pod count. Six pods is 0, one pod is 5; an empty ring answers -1,
// which holds whatever the machine already had until the ring grows back.
int ApStarHandling_ProfileForPods(int pods);

// Re-derive `md` on `profile`. No-op before the star's attributes are readable.
void ApStarHandling_Apply(MachineData *md, int profile);

#endif // AP_STAR_HANDLING_H

#ifndef CANNON_EVENT_H
#define CANNON_EVENT_H

// Cannon event — work-in-progress investigation toward spawning a fully-
// functional cannon yakumono inside City Trial.
//
// The cannon (YAKUKIND_CANNON, desc_id 48) is normally spawned only by
// Machine Passage (gr_kind 5) and grDataSingleRace4_CreateYakumono, both via
// GrYakuCannon_Create at 0x800fed20. City Trial's per-grkind hook never calls
// it. This module experiments with two angles:
//
//   1. Spawn path — hijack one slot of grdata->yakumono->data_array[] to
//      point at a zeroed cannon param block, call GrYakuCannon_Create with
//      that index, then restore the pointer. Confirms framework plumbing is
//      stage-agnostic; resulting ydata is the "ghost" baseline.
//
//   2. Load path — load GrMachine2.dat / GrMachine2Model.dat from CT and
//      bring the cannon's mesh + joints + yakumono param across. Currently
//      blocked: the model archive (1.6MB) busts CT's heap budget.
//
// Run gated by build-time CANNON_SPAWN_ENABLED / CANNON_LOAD_ENABLED flags.

void CannonEvent_On3DLoadEnd(void);

// Trigger an experimental render of grModelMachine2[N] in CT.
//   set_index = 0 → grmodel[0] (main stage tree, ~122 joints)
//   set_index = 1 → grmodel[1] (mesh-bearing tree — likely contains cannons)
//   set_index = 2 → grmodel[2] (lights/cameras region — probably won't render)
void CannonEvent_TryRender(int set_index);

#endif // CANNON_EVENT_H

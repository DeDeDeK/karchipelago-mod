#ifndef CANNON_EVENT_H
#define CANNON_EVENT_H

// Scaffolding toward a working cannon yakumono (YAKUKIND_CANNON, desc_id 48) in
// City Trial: a ghost spawn through a hijacked yakumono data_array slot, and a
// cross-load of GrMachine2's archives for the mesh. Gated by the build-time
// CANNON_SPAWN_ENABLED / CANNON_LOAD_ENABLED flags; runs diagnostic dumps only.

void CannonEvent_On3DLoadEnd(void);

// Experimental render of grModelMachine2[set_index] (0..2) in City Trial.
void CannonEvent_TryRender(int set_index);

#endif // CANNON_EVENT_H

#ifndef ENVIRONMENT_DESTRUCTION_COLL_POOL_H
#define ENVIRONMENT_DESTRUCTION_COLL_POOL_H

#include "structs.h"
#include "stage.h"

// Reserve the mod's collision room inside the stage's own arrays. Hooks
// grColl_Alloc, so it must run once at boot and before any stage loads.
void EnvPool_OnBoot(void);

// Claim the reserved slots on a freshly loaded stage.
int EnvPool_Install(void);

// Re-seat the mod's records in the query list. Cheap; call before a carve.
void EnvPool_Ensure(void);

// Forget every triangle handed out. The slots belong to the stage and go with it.
void EnvPool_Reset(void);

// Start a new group. Each group is one broadphase record with its own box, so
// keeping a carve's triangles in one group keeps that box tight and every other
// query rejects it on three float compares.
void EnvPool_BeginGroup(void);

// One unused triangle, already inside the current group with its vertex pointers
// wired to its own three slots (returned through vtx_out so the caller can set
// prev alongside pos). NULL when the group is full or the pool was never installed.
// `lining` marks it as a face of the hole rather than a remnant of a real wall,
// which is the difference between a surface that stands in for material and one
// the carve invented.
GrCollTri *EnvPool_Alloc(int lining, GrCollVtx **vtx_out);

// Fold a written triangle's bounds into its group's broadphase box.
void EnvPool_NoteBounds(Vec3 *center, Vec3 *half);

// Record a stage triangle this group's carve retired, so that recycling the
// group can hand its collision back rather than leaving a hole nothing fills.
// Only stage triangles belong here - a pool triangle's own group reclaims it
// wholesale, and restoring one whose group has since been zeroed would make the
// narrowphase read its NULL vertex pointers.
void EnvPool_NoteCut(int index);

// 1 when this triangle is one of the pool's. Generated collision is carved like
// any other, since it is what stands at a wall the mod has already cut; this
// says which triangles cost a restore slot and which are the pool's to reclaim.
int EnvPool_Owns(const GrCollTri *t);

// 1 when this triangle is a face of a hole the mod lined, as opposed to a
// remnant of a wall that was really there.
//
// The two are not interchangeable. A remnant is the surviving part of a stage
// triangle, in its plane and with its classification, and it stands in for that
// wall completely - a wall cut once is made of remnants from then on, so
// treating them as anything less than material is what stops a hole ever being
// widened or a second bite ever being taken. A lining face is a surface the
// carve invented to cover a cut rim, standing where material was removed;
// reading one as material is what lets the mod carve its own holes forever.
int EnvPool_IsLining(const GrCollTri *t);

int EnvPool_Installed(void);

// Replacements and cuts the current group can still take.
int EnvPool_Room(void);
int EnvPool_CutRoom(void);

// Triangles unspent across every group, for the debug line.
int EnvPool_Free(void);

#endif // ENVIRONMENT_DESTRUCTION_COLL_POOL_H

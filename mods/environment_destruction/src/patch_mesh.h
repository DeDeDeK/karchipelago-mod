#ifndef ENVIRONMENT_DESTRUCTION_PATCH_MESH_H
#define ENVIRONMENT_DESTRUCTION_PATCH_MESH_H

#include "structs.h"

#include "carve.h"

// Attribute set a patch mesh carries. It mirrors the source POBJ's so the
// material it shares finds the inputs it expects; POS is always present.
#define ENV_ATTR_NRM  (1 << 0)
#define ENV_ATTR_TEX0 (1 << 1)
#define ENV_ATTR_CLR0 (1 << 2)

// A patch is the extra mesh piece one source DOBJ grows to hold the triangles a
// carve generates: the re-cut remains of the surface triangles it clipped, and
// the lining of the hole. Its vertices are in the source joint's local space and
// its DOBJ hangs off that same joint, so it inherits the joint's matrix and (for
// the skin patch) its material.
typedef struct EnvPatch EnvPatch;

// Reserve the mesh arena. Must run from the mod's OnBoot: HSD_MemAlloc only
// survives past a scene change when it is called there.
void EnvPatch_OnBoot(void);

// Drop every patch and rewind the arena. The stage archive is reloaded whole, so
// the DOBJs we appended to its joints are gone with it.
void EnvPatch_Reset(void);

// Find or create the patch that shadows `src` on `joint`. flat_fill selects the
// untextured lining material instead of sharing the source's.
EnvPatch *EnvPatch_Get(JOBJ *joint, DOBJ *src, int attrs, int flat_fill);

// Append one triangle. Returns 0 when the arena is exhausted.
int EnvPatch_AddTri(EnvPatch *patch, EnvVtx *a, EnvVtx *b, EnvVtx *c);

// 1 when `patch` still has room for `tris` more triangles, counting chunks the
// arena could still hand it. Checked before a carve commits to destroying the
// source primitive it is replacing.
int EnvPatch_CanFit(EnvPatch *patch, int tris);

// Snapshot / restore the write position of every patch. A carve marks before it
// emits and rolls back if any triangle fails to land, so a source primitive is
// only destroyed once its whole replacement is in memory. Rollback keeps patches
// created since the mark - it only rewinds what they hold.
void EnvPatch_Mark(void);
void EnvPatch_Rollback(void);

// Push every touched display list to memory. Call once after a carve, before the
// next frame's draw.
void EnvPatch_Flush(void);

// The patch whose own DOBJ is `d`, or NULL when `d` is stage geometry. Carving
// generated geometry emits straight back into the patch it came out of, so a
// wall worked over repeatedly keeps one patch instead of a chain of them.
EnvPatch *EnvPatch_ForDobj(DOBJ *d);

// Whether `p` holds the untextured lining material. A carved triangle's remnants
// belong in a patch shaded the way it was.
int EnvPatch_IsFlat(const EnvPatch *p);

// Where a walker carving `p` in place has to stop: the chunk being appended to
// and how much of it was written before the carve started. Everything past that
// is this carve's own output, and reading it back would re-cut fresh geometry
// and misparse vertex records as primitive headers.
void EnvPatch_WalkLimit(const EnvPatch *p, POBJ **pobj_out, int *used_out);

int EnvPatch_ArenaUsed(void);
int EnvPatch_ArenaSize(void);

#endif // ENVIRONMENT_DESTRUCTION_PATCH_MESH_H

# Yakumono

"Yakumono" is the game's framework for interactive stage objects: props bound to the stage that
usually carry hit collision and may animate, break, drop items or spawn other objects. Shipped
kinds cover destructible scenery (City Trial's houses, trees, rocks, walls and coral, Machine
Passage's fans, Frozen Hillside's icicles), hazards and machinery (laser gates, push-out walls,
rising cubes, gondolas, the Machine Passage cannon), trigger volumes (catch zone, recovery zone,
down-force zone) and fixed set pieces (Whispy Woods, the Lighthouse). A modder needs this to
break, move, count or add props, or to hook damage to them.

Yakumono are a different system from the enemy and event actors (p_link 12) and from items
(p_link 13).

## Objects and descriptors

Every yakumono is a GObj of entity class 15 on `GAMEPLINK_YAKUMONO` (p_link 8) whose user data
is a 0x190-byte `YakumonoData`; `grYakuCheckGObjYakumono` (0x800f7a50) is the canonical test.
All are created by one factory, `GrYaku_Create(kind, data_idx)` (0x800f446c), which returns the
new GObj so the kind's creator can finish it. `kind` (a `YakuKind`, 70 of them) indexes
`stc_yaku_descs` (0x804a5be8), the game's `grYakuFuncTable`; `data_idx` indexes the stage's
`YakumonoTable.data_array` (`GrData+0x40`) of per-instance parameter blocks.

Kinds 16-69 each have a 0x14-byte `YakuDesc`: a state table, a `coll_func` (the break handler),
an optional init or check pointer, `adhere_update_func` and `get_point_func`. Kinds 0-15 are
eight generic base kinds in even/odd pairs, the odd one the "ctrl" variant (bit 7 of
`YakumonoData.flags`); their two descriptors share one block. A kind's descriptor and state
table sit in its source file's data next to that file's assert strings (`gryakucannon.c`,
`gryakubreakcoll.c`, ...), which is what identifies most kinds.

## Spawning

`grInitYakumono` (0x800f425c) runs during stage load and takes two paths, both when both apply:

1. **Per-ground hook.** A 28-entry table at 0x804a322c, indexed by the physical `GroundKind`,
   holds an optional init hook at each entry's `+0x04`. The hook creates the stage's props by
   calling per-kind creators with hardcoded kinds and `data_idx` values.
2. **Generic entries.** It then walks `YakumonoTable.entries` (`{kind, param, common_group}`,
   kind below 16), creating each through the 16-entry common-kind table at 0x804a5ba8
   (`stc_yaku_common_create`), whose wrappers call `GrYaku_Create_Generic` (0x800f4b20) with
   parameters from `Yakumono.dat`. Which object a base kind becomes comes from that data, not
   the code. Entry kind 12 is the ground copy panel, which adds a collision zone; Nebula Belt
   and Celestial Valley get their copy panels this way. Finally `GrYakuCommon_SelectRandomGroup`
   (0x800f3f9c) keeps a random subset of the entries sharing a common-group id and disables the
   rest.

Event-time kinds (the Pillar event's pillars, the recovery zones, the UFO) are created later by
`GrYaku_CreateSpawn` (0x800f48cc) from `YakumonoTable.spawn_data_array`, for which the stage's
collision allocation reserves room once per entry; spawning an entry more often than the table
lists it asserts.

City Trial ships no entries, only the hook `grDataCity1_CreateYakumono` (0x8010f268), which
makes exactly 31 creator calls (`data_idx` 0-30). Its `data_array` has 33 slots, so slots 31 and
32 are spare: a mod can point them at its own parameter block and create a yakumono without
disturbing a vanilla one. `GrObj.yaku_num` (+0x6fc) counts yakumono GObjs; `GrObj.yaku` (+0x710)
is sized from the entry count and is therefore NULL in City Trial, so code that enumerates live
props must walk the p_link 8 list instead.

## Lifecycle

`GrYaku_Create` creates the GObj (no custom GX link), allocates the `YakumonoData`, bumps
`yaku_num`, and `YakumonoGObj_InitData` (0x800f4d50) seeds it: scale 1.0, `forward` (0,0,1),
`up` (0,1,0), state -1, every callback NULL, and the descriptor's state table. Then an init
pipeline runs, where four fields of the parameter block decide what the framework builds:

| Param field | Consumer | Non-zero | Zero |
|---|---|---|---|
| +0x04 | `GrYaku_AllocJObj` (0x800f7308), `GrYaku_AttachAnim` (0x800f6394) | builds a model JObj and its animation | `model_jobj` stays NULL |
| +0x0c | `GrYaku_AttachModel` (0x800f6274) | attaches and positions the model | only sets `ydata->pos` |
| +0x14 | `GrYaku_InitAudio` (0x800f77dc) | allocates the audio track and emitter | no audio |

These gate whether the framework builds its own model, not whether the prop is visible: the
cannon and every break family leave them zero and draw from the stage's own scene graph.
`GrYaku_InitHurtData` (0x800f8484) always creates a stage-object HurtData with 2 attack regions.
`GrYaku_DestroyCallback` (0x800f4f0c), the user-data destructor, frees the HurtData, effect
group and model and unlinks the prop.

| Pri | Proc | Address | Work |
|---:|---|---|---|
| 1 | `YakumonoGObj_Think` | 0x800f5284 | Advance the HurtData, the state machine and animation events, then `anim_callback` |
| 4 | `YakumonoGObj_Proc4` | 0x800f52e8 | `phys_callback`; rebuild the matrix when flag bit 7 is set |
| 5 | `YakumonoGObj_Proc5` | 0x800f5340 | `envcoll_callback` |
| 6 | `YakumonoGObj_Proc6` | 0x800f5374 | `post_envcoll_callback` |
| 7 | `YakumonoGObj_Proc7` | 0x800f53a8 | `trigger_callback`, then place the hurtboxes at `ydata->scale` |
| 9 | `YakumonoGObj_Proc9_HitColl` | 0x800f53fc | Hit collision with the prop as victim |
| 10 | `YakumonoGObj_Proc10` | 0x800f5454 | On a hit, add to `ydata->dmg` (capped at 9999) and call `on_damage_callback` |

`Gr_StateChange` (0x800f5548) moves a prop to a state: it installs the state entry's four words
as the anim, phys, envcoll and post-envcoll callbacks, clears `on_damage_callback`,
`off_damage_callback` and `trigger_callback`, and plays the state's animation. A kind's creator
installs any others after it.

## City Trial props

| data_idx | Kind | Object | Instance model |
|---|---:|---|---|
| 0 | 17 | catch zone | trigger volume |
| 1 | 18 | trigger zone | trigger volume |
| 2-17 | 46 | gondola cars | zone-backed, one GObj per car |
| 18-19 | 61 | small animated props | classic: own model JObj |
| 20 | 68 | Lighthouse | zone-backed, multi-part |
| 21 | 69 | Whispy Woods (Forest event) | single-prop break shape |
| 22-23 | 32 | forest pitfall | single prop, multi-stage crack |
| 24 | 37 | volcano-base hole covers | strong break family |
| 25 | 36 | volcano rock walls | strong break family |
| 26 | 38 | houses (30 placed) | strong break family |
| 27 | 33 | coral (10) | weak break family |
| 28 | 34 | trees (53) | weak break family |
| 29 | 35 | volcano and high-plains rocks (41) | weak break family |
| 30 | 29 | star pole | single prop |

Events add the Pillar event's huge pillars (40), the Restoration Area's recovery zones (20),
Rail Fire (65), the Secret Chamber (66) and the UFO (67).

Where a prop's geometry lives depends on its family:

- **Break families** (33-38) are one GObj for many props. The creator looks up each placed
  prop's `GrCollRecord` in the stage collision pool (`grScene_FindInstanceByKey`, 0x800d7954, by
  joint), stores the records in an array at `ydata+0x130` and sets each record's `yaku_gobj` to
  the GObj. The weak families (33-35) keep the count at `+0x134`; the strong ones (36-38) keep
  per-prop HP at `+0x134` and the count at `+0x140`. The parent's own `pos`, `model_jobj` and
  `xform_jobj` are unused.
- **Single-prop** kinds (29, 32, 69) store one record directly at `+0x130`.
- **Zone-backed** kinds (17, 18, 46, 68) claim a collision zone instead of a record and write
  the owner GObj into the zone record at `+0x138`.

So scanning the collision records for a non-NULL `yaku_gobj` reaches every breakable prop and
nothing else (terrain records leave it NULL); zones, gondolas and the Lighthouse are only in
`coll.zone`. Weak-family props are `JOBJ_SKELETON` joints; the others are static.

Breaks are counted per kind: `YakumonoGObj_IncrementBreakCount` (0x80105d80) calls
`Ply_IncrementYakumonoBreakCount` (0x8022fed8), which bumps `PlayerStats.yakumono_break[kind -
0x15]` for kinds 0x15-0x28, the array the checklist's "break N of X" cells read through
`Ply_GetYakumonoBreakCount` (0x8022fccc). `Gr_GetYakumonoSpawnTotal` (0x800f7db0) gives how many
of a kind the stage places, from a table only City Trial and Sky Sands ship.

## Breaking a prop

The City Trial break families install no `on_damage_callback`, so `YakumonoGObj_Proc10` only
accumulates `ydata->dmg` for them, and seeding their HurtData breaks nothing. The exception is
the BreakCoral family (the star pole in City Trial), whose creators install
`GrYakuBreakCoral_DropItems`.

The real break is the descriptor's `coll_func`, called from the map-collision sweep when a
collider allowed to break props (a machine, by default) touches a breakable record:
`collideWithObject(yaku_gobj, collider, gcp, tri_idx, contact)` (0x800f5004) looks up the prop's
`coll_func` and returns its result, 1 when the prop broke. The handler computes

```
force = collider->radius (CollData+0x344) * impactSpeed^2
```

`impactSpeed` comes from `grScene_GetImpactSpeed` (0x800d8edc): the collider's `pos_delta`
(`CollData+0x14`) normalized, projected onto the triangle's outward normal and negated, clamped
at 0. The delta must point into the surface to count at all, and its magnitude does not matter -
only its direction and the collider's radius set the force.

- `GrYaku_TestImpactBreak` (0x80104cd4) breaks when `force >= hp[0]` and keeps no damage;
  `hitWeakObject` (0x80107914) and the star pole's first phase use it.
- `GrYaku_ApplyImpactDamage` (0x80104be0) subtracts the force from the prop's own HP and breaks
  at 0, so hits accumulate; `hitStrongObject` (0x801086d0) uses it.

Both cap the collider's speed afterwards (`CollData.yaku_break_speed_cap`). The weak handler
swaps the prop into a broken-state model at its placed spot; the strong handler breaks it
visibly at the contact point. `hitBreakableFloor` (0x80106bd0) advances one crack stage per
call, and `hitBigStar` (0x80103eb8) arms a second phase on a weak hit that the next contact
breaks without a force test. A break runs the whole tail: collision retire, mesh hide or debris,
drops, sound, break credit, state change.

## Collision, position and scale

A prop's triangles are its solid collision; there is no separate wall behind them.
`grScene_SetInstanceColl` (0x800d7ad0) toggles the collidable bit across a record's triangles (0
= broken) and `grScene_IsInstanceCollAll` (0x800d7b0c) is the break path's "still whole?" guard.
The triangles stay baked where the prop was placed, so moving a prop's model does not move its
collision - a mod that moves one must retire its collision or leave an invisible wall behind.

- **Reading a break-family prop's position**: take the translation of `record->world` or of
  `record->jobj`'s world matrix, not the parent's `ydata->pos`, which is (0,0,0).
- **Moving a single-instance prop** (cannon, gondola, rising cube): write the root JObj's local
  translation and set `ydata->flags` bit 7; `YakumonoGObj_Proc4` then rebuilds the matrix each
  frame the bit is set. Update `ydata->pos` too, which drops and sounds read.
- **Moving a break-family prop**: move its `record->jobj`. The weak families' joints are
  skeleton joints whose matrix `JObj_SetupMtxSub` (0x8040d6b4) rebuilds from the SRT every
  frame, so a written matrix only holds with `JOBJ_USER_DEFINED_MTX` set on that joint
  (`JObj_SetFlags`, 0x8040bd64).
- **Scale**: `ydata->scale` (+0xa4) scales only the hurtboxes, every frame through
  `YakumonoGObj_Proc7`, for all of a break family's props at once. The rendered model is scaled
  through each JObj's own scale.

## Notes for modders

- **Gating damage**: hook `YakumonoGObj_Proc9_HitColl` (the whole hit pass) or a kind's
  `on_damage_callback`. The break path itself goes through the map-collision sweep.
- **Synthesizing a break** without a real collision means calling `collideWithObject` with a
  fabricated `CollData`. `gcp` must be `Gr_GetCollParam()`; `tri_idx` a global index into
  `coll.tri` (`record->tri_begin - Gr_GetCollTris()` plus an offset to a triangle with a usable
  normal); the collider needs a `radius` far above any HP, a `pos_delta` pointing into that
  normal, its owner GObj at `+0x04` so `GrYakuBreak_GetAttackerPly` (0x80105cb0) credits the
  break, and a zeroed `mpCollInfo` with `contact_tri_id` -1, because `destroyBigStar`
  (0x800d7b8c) runs first in every `coll_func` and would otherwise read triangle 0. Clear
  `GRCOLL_FLAG_MOVING` (0x20) on the target triangle for the call, since the moving-geometry
  path can rewrite the delta to zero, and make sure the record is fully collidable first. The
  weak families anchor debris to a separate stage joint at the prop's placed spot, so a moved
  prop's debris appears there unless that joint is moved for the call.
- **Destroying one directly**: `GObj_Destroy` runs the destructor, leaving no dangling collision
  entry, but skips drops, credit and effects - and for a break family it removes the parent of
  every prop while the props' meshes keep rendering. Break a single prop instead.
- **Spawning a kind on a stage that does not ship it** works at the framework level:
  `GrYaku_Create` plus the kind's tail-init runs, the procs tick and the HurtData and state
  machine work. What a foreign stage lacks is the asset side - the cannon, for one, takes its
  mesh and barrel anchor joints from `GrMachine2Model.dat`'s scene graph, and loading that whole
  archive into City Trial exhausts the heap.

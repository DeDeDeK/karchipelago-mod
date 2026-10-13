# Enemy AI

How enemies (Waddle Dee, Sword Knight, Scarfy, Gordo and the rest) and the City Trial event
actors decide what to do and move: a two-level state machine whose states install four
callbacks, an animation-script bytecode that fires sounds, hitboxes and effects, per-type
movement and targeting, and the knockback that ends them. Every one of them is an `EnemyData` on
a GObj in p_link 12 (`GAMEPLINK_ENEMY`), created by `EventActor_Create`; the game calls them all
"event actors". A modder retuning enemies, adding behavior to one, or spawning one needs this
model.

## Architecture

Three things drive an enemy every frame:

1. **A state machine.** `EventActor_ChangeState` resolves a state id to an `EnemyStateDesc` and
   installs its four callbacks into `EnemyData` (`ed+0xAB8`-`ed+0xAC4`). Those callbacks **are**
   the per-type brain.
2. **Ten GObj procs**, registered on every actor by `EventActor_Create`, which run animation,
   physics, collision and damage and dispatch the four callbacks.
3. **Per-type data.** `stc_enemy_kind_desc_table` (0x804b1d98) holds one `EnemyKindDesc` pointer
   per `EnemyKind`: the type's state table plus its init, parameter-copy, destroy, post-create
   and capture callbacks. The archive (`Em*Data.dat`) supplies the parameter blocks, model and
   animations.

The 79 kinds resolve to 47 distinct descriptors. Tier 0 enemies (0x00-0x17) each have their own;
tier 1 and tier 2 (0x18-0x47) share one descriptor per enemy, distinct from tier 0's and usually
with a shorter state table, so a T1/T2 enemy runs different AI from its T0 namesake. The `B`
variants share their base's descriptor, except Cappy B and Dayl B, which keep one descriptor in
every tier.

The descriptor's `+0x18` callback runs once at the tail of `EventActor_Create` and puts the
actor into its first state, 0x0E. `+0x1C` runs inside `EventActor_OnCapture` (0x802038c4) when a
rider inhales the actor. There is no damage callback in the descriptor.

## Per-frame procs

| Pri | Proc | Address | Work |
|---:|---|---|---|
| 0 | `EventActorGObj_ProcResetDamage` | 0x801fc670 | Zero the per-frame damage fields |
| 1 | `EventActorGObj_ProcAnim` | 0x801fc698 | Advance the animation, run the animation script, then `anim_cb` |
| 4 | `EventActorGObj_ProcPhys` | 0x801fc6fc | `phys_cb`, then `vel += accel`, `pos += vel`, and the floor kill (not for kinds >= 0x4C) |
| 5 | `EventActorGObj_ProcEnvColl` | 0x801fc7c4 | `envcoll_cb` - where most per-state logic lives |
| 6 | `EventActorGObj_ProcSharedModel` | 0x801fc7f8 | Shadow, `pri6_cb`, then the model matrix from `pos`, `forward` and `up` |
| 7 | `EventActorGObj_ProcTrigger` | 0x801fc848 | `trigger_cb` if set, HurtData update |
| 8 | `EventActorGObj_ProcHitCollInit` | 0x801fc8e8 | A bare `blr` |
| 9 | `EventActorGObj_ProcHitColl` | 0x801fc8ec | Hit collision with the actor as victim |
| 10 | `EventActorGObj_ProcDmgApply` | 0x801fc9f0 | Turns a logged hit into damage and knockback |
| 21 | `EventActorGObj_ProcEndOfFrame` | 0x801fcabc | `pos` to `pos_prev`, ground flags, lifetime and out-of-bounds despawn |

Physics integrates unconditionally, so an actor that should stand still must keep `vel` and
`accel` zero.

`trigger_cb` (`ed+0xAC8`) is a dead slot: in the whole enemy code region it is only zeroed by
`EventActor_ChangeState` (0x801fc634) and read by the priority-7 proc (0x801fc85c). No enemy
installs it.

## State machine

```c
// 0x801fc398
void EventActor_ChangeState(EnemyData *ed, int state, int flags, float start_frame, float rate);

typedef struct EnemyStateDesc {
    int anim_index;                    // -1 = none
    void (*anim_cb)(EnemyData *ed);    // -> ed+0xAB8, priority 1
    void (*phys_cb)(EnemyData *ed);    // -> ed+0xABC, priority 4
    void (*envcoll_cb)(EnemyData *ed); // -> ed+0xAC0, priority 5
    void (*pri6_cb)(EnemyData *ed);    // -> ed+0xAC4, priority 6
} EnemyStateDesc;                      // 0x14
```

States below 0x0E come from the common table (`ed+0x40`, 0x804b2950) shared by every type; 0x0E
and up index the type's own table (`ed+0x44`) at `state - 0x0E`. The new state id goes to
`ed+0x34`. The four callbacks are always installed and `trigger_cb` always cleared, whatever the
flags:

| Flag | Effect |
|---|---|
| 0x01 | Skip the animation half entirely (the new `anim_index` included) |
| 0x02 | Keep the animation playing if `anim_index` is unchanged |
| 0x04 | Skip `EventActor_StateCleanup` (0x801fe110) |
| 0x08 | Preserve `anim_pos` across the change |
| 0x10 | Keep `ed+0xAD4` |
| 0x20 | Skip the HurtData animation reset |
| 0x40 | Keep the effect groups (`efgroup`, `efgroup2`) |

A full transition also re-initializes the animation script with its timer at -1.0, so the first
command fires immediately.

### Common states

| State | Role | Callbacks |
|---|---|---|
| 0x00-0x08 | Hit reaction: stun, launch, death | `EventActor_CommonAnim` (0x8020bd68), `EventActor_CommonPhys` (0x8020be1c), `EventActor_CommonEnvColl` (0x8020c558) |
| 0x09 | Death | `EventActor_DeathAnim` (0x80203e60): death effect and SFX at frame 0, destroyed past 120 |
| 0x0A | Inhaled | follows the rider's mouth bone and shrinks; destroyed past 120 frames |
| 0x0B | Knockback | launch velocity from the knockback fields; raycasts for a landing spot or destroys |
| 0x0C | Launched | spline-driven flight to the landing point, then 0x0D |
| 0x0D | Sliding | ground friction and orientation toward the ground |

States 0x00-0x08 differ only in animation; `EventActor_ApplyKnockback` enters the one its `mode`
names.

## Animation script

Each enemy animation (`EnemyAnimSeqEntry`) carries a bytecode script that fires work on
animation frames: a sound at frame N, a hitbox, an effect. Its state sits at `ed+0x4C`: a
countdown timer, the current frame, the bytecode pointer (NULL = stopped) and a loop stack. When
the timer reaches 0, `EventActor_StateMachine` (0x802017d0) reads the next byte; the command id
is its upper six bits, the lower two an inline selector.

Commands 0-10 are the generic HSD ones (stop, wait, wait-until-frame, loop begin/end, call,
return, goto, wait forever, play and stop sound), dispatched by `zz_80068d74_` through the table
at 0x80499628. Commands 11-25 are enemy-specific, in 12-byte entries at 0x804b26b0 whose
handlers take `EnemyData*` and the script state:

| Cmd | Handler | Effect |
|---:|---|---|
| 11 | 0x80200e58 | Set attribute bits at `ed+0xB00` and a float at `ed+0xB04` |
| 12 | `EnemyAnimCmd_SetHitDesc` (0x80200eb8) | Decode and arm an attack region in the HurtData |
| 13 | `EnemyAnimCmd_DisableHit` (0x80201138) | Turn one attack region off |
| 14 | 0x80201180 | Turn every attack region off |
| 15 | 0x802011bc | Spawn a projectile or sub-actor (`EventActor_SpawnWeapon`) |
| 16 | 0x80201418 | Rumble every human player |
| 17, 19 | 0x80201488, 0x80201504 | No-ops that skip 1 and 3 operand words |
| 18 | 0x80201498 | Spawn or replace a persistent particle effect |
| 20, 21 | 0x8020152c, 0x80201578 | Play a sound, or a random one of N |
| 22 | 0x802016ac | Store a 24-bit constant into one of `ed+0xAF0`-`ed+0xAFC` |
| 23 | 0x8020172c | Set bit 1 of `ed+0xB0A` |
| 24, 25 | 0x8020174c, 0x80201798 | Apply or reset a color animation |

## Per-type AI

On spawn an actor enters 0x0E, whose `anim_cb` runs a one-time setup (velocity, ground snap,
attach to a patrol spline) and changes into the type's active state, usually 0x0F. From there
the four callbacks carry the logic, and which slot does what varies by type: one slot perceives
(a player scan), one moves, and a range, timer or crossing test calls `EventActor_ChangeState`
to step through idle, chase, attack and recover.

Tier-0 Waddle Dee (0x17) shows the shape:

| State | `anim_cb` | `phys_cb` | `envcoll_cb` | `pri6_cb` | Role |
|---|---|---|---|---|---|
| 0x0E | 0x80219638 | - | - | - | Spawn: zero velocity, attach to a spline, go to 0x0F |
| 0x0F | - | - | 0x802196e0 | 0x80219704 | Patrol; `pri6_cb` scans with `EventActor_FindNearestPlayerAhead`, a hit goes to 0x10 |
| 0x10 | 0x802197ac | 0x802197e8 | 0x802197ec | - | Wind up and turn toward the target |
| 0x11 | 0x80219908 | 0x80219988 | 0x80219a48 | 0x80219a84 | Lunge |
| 0x12 | 0x80219b4c | - | 0x80219c40 | 0x80219c64 | Recover, then back to patrol |

Sword Knight (0x05) decides in `anim_cb` instead: `SwordKnight_State0FDecide` (0x80211520) locks
the nearest player who crosses from behind to in front within attack range and, after a windup,
slashes. Composite enemies spawn a rider part in their init (Broom Hatter's 0x48, Sword Knight's
0x49, the Waddle Dee Truck's 0x4A); the parts mirror their parent through
`EventActor_FollowParent` (0x80219eec) and `parent_gobj`.

### Targeting

| Function | Address | Picks |
|---|---|---|
| `EventActor_FindNearestPlayer` | 0x801ffd78 | Nearest rider within `detect_range` (50.0), into `target_ply` (`ed+0xB24`), with a 20-39 frame retarget cooldown at `ed+0xB26` |
| `EventActor_FindNearestPlayerFOV` | 0x801ff8d8 | Nearest rider within a caller-given range, aiming a body joint at it |
| `EventActor_FindNearestPlayerAhead` | 0x801fe5d4 | Nearest rider in a forward cone (Waddle Dee) |
| `EventActor_FindPlayerInRangeFwd` | 0x801fe764 | Forward-cone probe (TAC's grab) |
| `EventActor_FindDiveTarget` | 0x801fe8dc | Bronto Burt's dive gate |
| `EventActor_PlayerAheadDist` | 0x801fea60 | Signed forward distance to one player (Sword Knight) |

`EventActor_FindNearestPlayer` also writes the direction from the target to the actor into
`turn_to` (`ed+0xB38`) and `forward` when no turn is in progress. Despite its name,
`EventActor_FindNearestPlayerFOV` excludes no player by angle - its angle gate only rejects
values above 180 degrees - so it is a nearest-in-range search; vanilla callers pass
`leash_range`. The cone scanners return a player index without touching `target_ply`.
`Enemy_DistToPlayer` (0x801fffa4) and `Enemy_RumblePlayer` (0x801ff80c) are helpers; the second
only rumbles a controller - all damage goes through hit collision.

### Movement

Ground enemies follow the splines in the stage data. `EventActor_PathInit` (0x80206e2c) picks
the nearest spline with `Spline_FindNearest` (0x800cf07c) and stores the segment (`ed+0x5DC`)
and arc parameter (`ed+0x5FC`); `EventActor_PathFollowUpdate` (0x80209ce4) evaluates the spline
each frame for actors with no leash (`leash_radius` -1.0), and `EventActor_PathAdvance`
(0x8020a040) steps along it and onto connected splines.

`EventActor_AIPhysicsTick` (0x802081ec) is the common ground-follow routine: it returns
immediately when `movement_speed` (`ed+0x964`) is 0, otherwise advances along the path with
banked turning, and returns 1 when the actor is stationary, which several states use as their
transition trigger. The grounded movers `EventActor_CombatMovement` (0x8020b490) and
`EventActor_CombatAI` (0x802069e8) switch on `is_airborne` (`ed+0x908`) between ground
following and falling under gravity. `EventActor_GroundSnap` (0x80204fac) casts from `pos +
up*5` to `pos - up*50`, places the actor `param_ground_clearance` above the hit and clears
`is_airborne`, or sets it on a miss.

Bronto Burt, Bomber and Scarfy are the true flyers. They never ground-snap their position;
`EventActor_FlyMovement` (0x8020354c) hovers around the anchor `pos_initial` or homes, and
`EventActor_FlyForward` (0x8020335c) moves along the facing by the animation's root motion.

## Taking a hit

An enemy's two attack regions are rewritten from the current animation frame each frame by
`EventActor_RefreshAttackParams` (0x80201ba4) and switched off by script command 13. The enemy's
own hit pass (priority 9) runs only while `damage_accum_1` (`ed+0x994`) is below the archive's
`param_hp` (`ed+0x3B0`) and the model alpha is 1.0; the child parts 0x48-0x4A skip it. Most
enemies have `param_hp` 1, so one hit makes them untouchable while they are knocked out; TAC's
is 40, Dyna Blade's 150 and Gordo's 1e8.

`EventActorGObj_ProcDmgApply` (0x801fc9f0) reads the HurtData. With a hit (`kb_mag != 0`) it
calls `EventActor_GiveDamage` (0x8020b680, adds to `ed+0x994` and `ed+0x998`, capped at 9999)
and then the actor's own hit handler at `ed+0xAD0` if set, else `EnemyKnockback_Default`
(0x8020bcd8). That maps the hit's hurt type 0-7 to a knockback mode through the table at
0x804b2b50 (identity) and calls `EventActor_ApplyKnockback` (0x8020b784).

Enemies have no HP; a knockback is what removes them. `EventActor_ApplyKnockback` scales the
hit's knockback by `damage_scale` (0.4) and classifies it into a tier against `tier_threshold`
(10, 21, 32):

| Tier | Scaled knockback | Stun frames | Launch speed | Intangibility base |
|---:|---|---:|---:|---:|
| 0 | < 10 | 2 | 2.0 | 20 |
| 1 | 10 - 21 | 4 | 3.0 | 30 |
| 2 | 21 - 32 | 6 | 4.0 | 40 |
| 3 | >= 32 | 8 | 5.0 | 50 |

It stores the tier (`ed+0xA1C`) and stun frames (`ed+0xA18`), grants intangibility of `base *
archive +0xA0 multiplier * hit_iframes_scale[player count]` (at least 1) plus the stun,
randomizes the shake, picks a launch direction from the attacker's `HurtKind` (away from an
attacking actor, along the hit vectors for riders, machines and projectiles) and enters common
state `mode`. The common callbacks hold the actor shaking for the stun, launch it at the tier's
speed, and once the intangibility runs out run the death (the actor's `custom_death_callback` at
`ed+0xAEC` if set) and destroy it 30 frames later.

## Actor data and parameters

`Enemy_GetActorData(kind)` (0x801fd498) finds the kind's archive and sub-entry (`flags` selects
one of five) and stores it at `ed+0x14`. The sub-entry is a pointer block: +0x00 the parameter
block, +0x04 the per-type tuning block (copied to `ed+0x40C`), +0x08 the model, +0x0C the
animation table indexed by `anim_index`, +0x10 material animations. `EventActor_CopyParamBlock`
(0x802006b4) copies 0xA4 bytes of the parameter block to `ed+0x368` - base scale, speeds,
`param_gravity` (`ed+0x3A4`), `param_hp` (`ed+0x3B0`), `param_ground_clearance` (`ed+0x3C0`) and
turn rates - then runs the descriptor's `copy_params`.

Behavior shared by every enemy lives in one global table, `EnemyParamTable` (`Enemy.dat`'s
`emDataAll`), whose pointer `Enemy_LoadCommonParams` (0x801fd580) stores at
`stc_enemy_param_table` (0x805dd878) on every 3D scene load:

| Field | Offset | Value | Use |
|---|---|---|---|
| `damage_scale` | +0x04 | 0.4 | Knockback scale before tiering |
| `tier_threshold[3]` | +0x08 | 10, 21, 32 | Tier boundaries |
| `hit_iframes[4]` | +0x30 | 20-50 | Intangibility base per tier |
| `hit_iframes_scale[4]` | +0x40 | 1, 0.8, 0.6, 0.5 | By player count |
| `kb_launch[4]` | +0x50 | 2-5 | Launch speed per tier |
| `stun_frames[4]` | +0x60 | 2-8 | Stun per tier |
| `detect_range` | +0x80 | 50 | `EventActor_FindNearestPlayer` radius |
| +0x88, +0x8C | | 30, 300 | Rumble radius of a dying actor; knockback direction blend distance |
| `leash_range` | +0x90 | 500 | Range most per-type states pass to their scans |
| `retarget_min/max` | +0x94 | 20, 40 | Retarget cooldown bounds |

The archive's `+0x0C`-`+0x14` are render-LOD values `EventActorGObj_GetLOD` (0x80206cc0) reads
straight from the archive; the copies at `ed+0x374`-`ed+0x37C` are never read. There is no
per-enemy detection range.

## Notes for modders

- **Retuning every enemy.** Write `EnemyParamTable` after `Enemy_LoadCommonParams` stores it.
  `lbLoadArchive` reuses a preloaded copy of `Enemy.dat` in place, so a write survives into the
  next scene load; restore from a saved copy rather than scaling the live values.
- **Per-frame logic on one actor.** Point `trigger_cb` (`ed+0xAC8`) at a function taking
  `EnemyData*`; the priority-7 proc calls it every frame. Every state change clears it, so set
  it again from your own proc. From there a mod can overwrite `target_ply` and `turn_to` after
  vanilla targeting, re-assert `movement_speed` (`ed+0x964`) and `idle_wander_speed`
  (`ed+0x974`), which the state callbacks rewrite each frame, or force a state. Adding a GObj
  proc of its own to the actor works too.
- **Leave the common states alone.** 0x00-0x0D are shared by every type.
- A standalone actor has no `parent_gobj` and no spline until `EventActor_PathInit` runs.
  `EventActorGObj_GetAnimRate` (0x802049b8) dereferences the parent without a check but is only
  reached from the child parts' states; `splArcLengthPoint` (0x80415958) dereferences the
  spline, so a callback that walks a path before one is assigned faults.

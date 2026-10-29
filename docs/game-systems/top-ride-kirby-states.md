# Top Ride Kirby States

Each Top Ride kirby has a polymorphic state object at `TopRideKirby+0x7C` (`state_handler`): the
kirby is "in" a state by virtue of which class instance occupies that slot. Damage reactions -
squished, frozen, paralysed, blown away - and the item powers are all states. A mod that applies
a status effect to a Top Ride player, or detects one, works through the Kirby class's state
methods described here. The state-ID enum, the state vtable constants (`TR_KSTATE_VT_NORMAL` and its siblings) and inline
helpers for the commonly used states are in hoshi's Top Ride header.

## Transitions

States derive from `KirbyOperate`, through `KirbyNormal` or `KirbyDamage`. Transitions are
non-virtual methods on the Kirby class (vtable 0x804d2304, RTTI name "Kirby"), and a guarded one
runs:

1. The invincibility predicate, Kirby vtable +0xBC, which forwards to the state's +0x44; if the
   kirby is immune, stop.
2. A `dynamic_cast` of the state handler to the target class; if it is already in that state, stop.
3. `zz_802d8ac8_(kirby, 1)`, dropping any held item.
4. The current state's +0x08 release, called with -1.
5. The target state's setter, which writes the new vtable into the state object and sets up its
   animation, timer and knockback.

## The State Methods

The guarded methods sit in Kirby vtable slots 55-68:

| Slot | Offset | Method | State | Arguments after `this` |
|---:|---:|---|---|---|
| 55 | 0xDC | `TopRide_KirbyPressMethod` (0x802d54ec) | KirbyPress | none |
| 56 | 0xE0 | `TopRide_KirbyBurnMethod` (0x802d55c0) | KirbyBurn | `u32, u32 *, u32` |
| 57 | 0xE4 | `TopRide_KirbyFreezeMethod` (0x802d56bc) | KirbyFreeze | none |
| 58 | 0xE8 | `TopRide_KirbyCrushMethod` (0x802d5760) | KirbyCrush | `u16, Vec3 *` |
| 59 | 0xEC | `TopRide_KirbyExplodeMethod` (0x802d5834) | KirbyExplode | `u16, Vec3 *, u16, u16` |
| 60 | 0xF0 | `TopRide_KirbyStrikeMethod` (0x802d5900) | KirbyStrike | `u16, Vec3 *, u16, u16` |
| 61 | 0xF4 | `TopRide_KirbySpinMethod` (0x802d59cc) | KirbySpin | `u32, Vec3 *, u32` |
| 62 | 0xF8 | `TopRide_KirbySandSpinMethod` (0x802d5aa0) | KirbySandSpin | `u32, Vec3 *, u32` |
| 64 | 0x100 | `TopRide_KirbyNumbMethod` (0x802d5b74) | KirbyNumb | none |
| 65 | 0x104 | `TopRide_KirbyElecMethod` (0x802d5be4) | KirbyElec | `u32` |
| 66 | 0x108 | `TopRide_KirbyConfuseMethod` (0x802d5c64) | KirbyConfuse | `u32` |
| 67 | 0x10C | `TopRide_KirbyShortcutMethod` (0x802d5ce4) | KirbyShortcut | `u32, u32, u32` |
| 68 | 0x110 | `TopRide_KirbySpeedDownMethod` (0x802d5da4) | KirbySpeedDown | `u32` |

Their guards differ:

- **Press, Burn, Freeze, Crush, Explode, Strike, Spin, SandSpin** run the full sequence above.
  Burn also refuses while the kirby is in KirbyElec. Crush, when the kirby is already crushed,
  re-applies the knockback (helper 0x802f53dc) instead of reinstalling the state. SandSpin
  subclasses Spin, so its cast blocks re-entry from either.
- **Shortcut and SpeedDown** skip the invincibility check. Shortcut also requires the float at
  kirby+0x138 to be below a constant; SpeedDown requires the state's +0x48 predicate to return 0.
- **Numb, Elec and Confuse** do no invincibility check and no cast: they refuse only when the state
  ID is 13 (either Doodlebug state), so a kirby in or riding a Doodlebug is immune to them.

Six more slots install their state with no guard at all - the states the engine normally enters
itself: 49 `TopRide_KirbyGrindMethod` (0x802d9f2c), 50 `TopRide_KirbyNormalMethod` (0x802da0f4),
51 `TopRide_KirbyDoodlebugMethod` (0x802da150), 52 `TopRide_KirbyDoodlebugOutMethod` (0x802da1c0,
knockback away from a source position), 63 `TopRide_KirbyWhirlpoolMethod` (0x802da2ec, likewise)
and 69 `TopRide_KirbyTransparentMethod` (0x802d5e60, which also drops the held item). Slots 53 and
54 are not state methods, and slots 47 and 48 forward to the state's +0x44 and +0x4C predicates.

**KirbySpeedUp has no method.** `TopRide_KirbyApplyItem` enters it by calling the current state's
release with -1 and then the setter 0x802fe890 with `(state_handler, kirby + 0x80)`, which rewrites
the existing state object in place.

## State Classes

| Class | ID | State vtable | Setter | Animation |
|---|---:|---|---|---|
| KirbyNormal | 0 | 0x804d6f5c | 0x802df844 | `AC_RUN_LOOP` |
| KirbyDamage (base) | 0 | 0x804da158 | 0x802f39a0 | set by subclasses |
| KirbyPress | 2 | 0x804da070 | 0x802f4068 | `AC_FLAT_START` (flattened) |
| KirbyCrush | 3 | 0x804d9ee0 | 0x802f4a48 | `AC_TOBASARE` (blown away) |
| KirbyExplode | 4 | 0x804d9dd0 | 0x802f6138 | `AC_TOBASARE` |
| KirbyStrike | 5 | 0x804d9cbc | 0x802f6c28 | `AC_TOBASARE` |
| KirbySpin | 6 | 0x804d9a90 | 0x802f7718 | `AC_TOBASARE` |
| KirbySandSpin | 6 | 0x804d9bac | 0x802f7b28 | as KirbySpin |
| KirbyNumb | 7 | 0x804d9980 | 0x802f7f24 | `AC_SIBIRE` (paralysis) |
| KirbyElec | 8 | 0x804d9870 | 0x802f858c | `AC_SIBIRE` |
| KirbyWhirlpool | 9 | 0x804d9760 | 0x802f8a5c | as KirbyDamage |
| KirbyBurn | 10 | 0x804d964c | 0x802f958c | `AC_FIRECRASH` |
| KirbyFreeze | 11 | 0x804d953c | 0x802fa16c | none; its tick drives an ice block |
| KirbyConfuse | 12 | 0x804d9434 | 0x802faa88 | `AC_PANIC` |
| KirbyDoodlebugOut | 13 | 0x804d9328 | 0x802fb060 | as KirbyDamage |
| KirbyGrind | 14 | 0x804d91f8 | - | - |
| KirbyShortcut | 15 | 0x804d90e8 | 0x802fbaf0 | none |
| KirbyTransparent | 16 | 0x804da304 | 0x802f3128 | none |
| KirbySpeedUp | 17 | 0x804dbcf8 | 0x802fe890 | `AC_RUN_LOOP`, with an effect overlay |
| KirbySpeedDown | 18 | 0x804dbac8 | 0x802ff98c | none |
| KirbyDoodlebug | 13 | 0x804d2488 | - | - |

The ID is what the state's +0x0C `get_state_id` returns, and it does not match the header's
state-ID enum one to one: no state returns 1, because KirbyNormal shares the base's ID function,
and both Doodlebug classes return 13. **Identify a state by its vtable pointer**, the
state vtable constants, which is what the guards' casts amount to.

Setters write the animation as a string pointer at `state_handler[1] + 0x4F4`, resolved later
against the character's animation table. Durations are not one argument: most states tick a timer
in their per-frame +0x28 callback, and the `u16` arguments of Explode, Strike and Spin feed slots at
+0x5C and +0x6C that time the hit and its follow-up.

| State vtable offset | Purpose |
|---:|---|
| +0x08 | Release, called with -1 on the way out |
| +0x0C | `get_state_id` |
| +0x28 | Per-frame tick: animation, gravity, velocity |
| +0x44 | Invincibility predicate |
| +0x48 | Predicate used by SpeedDown's guard |
| +0x4C | Predicate behind Kirby vtable +0xC0 |
| +0xE4 | `AC_TOBASARE` velocity rescale (0x802f3cfc on most damage states) |

State vtables are not all the same length. KirbyConfuse's ends before +0xE4, and the word there is
data, so +0xE4 is not safe to call on an arbitrary state.

## What the Game Triggers

`EffectorExplode_ApplyToKirby` (0x802e6898) bridges bombs, landmines and bomb blocks to
KirbyExplode, with knockback away from the explosion scaled by mass and distance.
`EffectorCrush_ApplyToKirby` (0x802e6630) bridges a heavy machine landing on a kirby to KirbyCrush
- a knockback launch, not a squish; the squish is KirbyPress. Burn (fire blocks and lava tiles),
Freeze (the Freeze Fan's projectile) and Elec are applied from per-item or per-tile callbacks that
call the Kirby methods directly; the burn-tile tick calls `TopRide_KirbyBurnMethod` on every kirby
it covers every frame. The SAND course's sand pit (`TopRideSandPit_Update`, 0x80331564) ejects a
swallowed kirby through KirbyDoodlebugOut, from its call at 0x80331a94.

Top Ride has no knockout: courses are bounded arenas with no pits or fall deaths, and no state ends
a player's race. A damage state is the nearest thing the mode has to a defeat.

## Applying a State From Mod Code

Fetch the KirbyMgr, require `round_state == 2`, and for each kirby whose slot is a human
(`TopRide_GetPlayerKind`) call the method. Before the race starts the state handler is not fully
wired and the release call can crash.

Pointer arguments are dereferenced, so they need real addresses even when zero is meant: Burn
dereferences its `u32 *` (at 0x802d5674) and Spin takes the magnitude of its `Vec3 *`. A pointer
to a zeroed `Vec3` is legal and gives a stun in place with no knockback. Burn also needs a real
duration: its setter reads `*arg2` as a float to seed a timer the per-frame tick counts down by 6,
so 0.0 never ends, and vanilla derives the value from its effector, with no constant to copy.

What a triggered state looks like depends on two things beyond the call:

- **The velocity rescale.** The +0xE4 callback renormalizes the charge component's velocity
  (kirby+0xA0) and reapplies its magnitude whenever that is above a threshold, so a kirby entering
  a state while moving is flung along its running direction - it reads as a teleport. It happens
  only when the state's tick calls +0xE4 and the setter left velocity nonzero.
- **Input gating.** A damage state visibly stuns only when its tick is not the shared
  `zz_802f3bd0_` (which gates no input), when its setter adds an overlay (Freeze's ice block), or
  when its animation reads as a hit by itself (Press). The animation alone decides nothing:
  `AC_SIBIRE` is shared by the visible Numb and the invisible Elec.

With the kirby's velocity zeroed immediately before and after the call:

| State | Tick | Outcome |
|---|---|---|
| Press | shared, calls +0xE4; setter zeroes velocity | A visible flattening |
| Freeze | unique, no +0xE4 | A clean ice-block stun |
| Numb | unique: `TopRide_VelocityDecay`, then +0xE4 | The paralysis pose; the decay supplies the input gating |
| Confuse | `TopRide_VelocityDecay` (0x802d1d84) | A clean panic-spin stun |
| Elec | shared; setter zeroes velocity | Silent: the state lands but the kirby keeps full control |
| Strike | shared; setter scales entry velocity by ~0.31 | Silent |
| Explode | shared; setter scales entry velocity by mass | Silent |
| Crush | shared; the knockback helper normalizes the zero `Vec3` argument into NaN | A brief vertical spin |
| Spin | unique, subtracts gravity with no drag | Still ejects the kirby: velocity builds back up every frame |

Elec is silent for a structural reason: it inherits KirbyDamage's predicate slots +0x10 to +0x24,
which are constant stubs, while Confuse overrides them with real ones. The engine polls those
predicates every frame and gates input only when the state supplies its own, so vanilla electric
hazards must set up more around the call. Spin could only be tamed by zeroing velocity every frame
or patching its gravity store at 0x802fd578.

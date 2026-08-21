# Top Ride Kirby States

Each Top Ride Kirby has a polymorphic state object at `TopRideKirby+0x7C` (`state_handler`). The kirby is "in" a state by virtue of which class instance lives in that slot. State classes derive from `KirbyOperate` and `KirbyNormal`/`KirbyDamage`; transitions are driven by non-virtual methods on the Kirby class itself (vtable at `0x804d2304`, 82 entries, `0x148` bytes, RTTI class name `"Kirby"` at `0x805d9134`).

Vanilla bombs and landmines invoke `KirbyExplode` ("tumble") via `EffectorExplode_ApplyToKirby`; vanilla heavy-machine collisions invoke `KirbyCrush` (a knockback launch, same `AC_TOBASARE` animation family as Explode - **not** a squish) via `EffectorCrush_ApplyToKirby`. The squish-flat animation belongs to `KirbyPress`.

The state-ID enum, the state vtable constants, and inline helpers for the states mod code actually uses live in `externals/hoshi/include/topride.h`.

## Transition Sequence

A state transition is:

1. (Optional) `kirby->vtable[+0xBC]()` - invincibility predicate (delegates to `state_handler->vtable[+0x44]`). If immune, abort.
2. (Optional) `dynamic_cast<KirbyTargetState*>(kirby->state_handler)` - if the kirby is already in the target state, abort.
3. `zz_802d8ac8_(kirby, 1)` - drop any held item.
4. `state_handler->vtable[+8](-1)` - release/destroy the current state.
5. The per-state setter writes the new state's vtable into `state_handler` and configures its data (animation, timer, knockback).

The Kirby vtable contains one wrapper method per "externally invocable" state - the ones with vanilla effector callers and/or item callers. Setters are not called directly by anything outside their wrapper.

## Kirby Vtable Wrappers

The 13 guarded state-transition wrappers cluster at byte offsets `0xDC..0x114` (indices 55-69):

| Index | Offset | Wrapper | Target state | Caller signature |
|------:|-------:|---------|--------------|------------------|
| 55 | 0xDC | `TopRide_KirbyPressMethod` `0x802d54ec` | KirbyPress | `(this)` |
| 56 | 0xE0 | `TopRide_KirbyBurnMethod` `0x802d55c0` | KirbyBurn | `(this, u32, u32*, u32)` |
| 57 | 0xE4 | `TopRide_KirbyFreezeMethod` `0x802d56bc` | KirbyFreeze | `(this)` |
| 58 | 0xE8 | `TopRide_KirbyCrushMethod` `0x802d5760` | KirbyCrush | `(this, u16, Vec3*)` |
| 59 | 0xEC | `TopRide_KirbyExplodeMethod` `0x802d5834` | KirbyExplode | `(this, u16, Vec3*, u16, u16)` |
| 60 | 0xF0 | `TopRide_KirbyStrikeMethod` `0x802d5900` | KirbyStrike | `(this, u16, Vec3*, u16, u16)` |
| 61 | 0xF4 | `TopRide_KirbySpinMethod` `0x802d59cc` | KirbySpin | `(this, u32, Vec3*, u32)` |
| 62 | 0xF8 | `TopRide_KirbySandSpinMethod` `0x802d5aa0` | KirbySandSpin | `(this, u32, Vec3*, u32)` |
| 64 | 0x100 | `TopRide_KirbyNumbMethod` `0x802d5b74` | KirbyNumb | `(this)` |
| 65 | 0x104 | `TopRide_KirbyElecMethod` `0x802d5be4` | KirbyElec | `(this, u32)` |
| 66 | 0x108 | `TopRide_KirbyConfuseMethod` `0x802d5c64` | KirbyConfuse | `(this, u32)` |
| 67 | 0x10C | `TopRide_KirbyShortcutMethod` `0x802d5ce4` | KirbyShortcut | `(this, u32, u32, u32)` |
| 68 | 0x110 | `TopRide_KirbySpeedDownMethod` `0x802d5da4` | KirbySpeedDown | `(this, u32)` |

Bare-release wrappers - no invincibility or `dynamic_cast` guard, they install the state unconditionally. These are the "internal" states the engine normally enters itself:

| Index | Offset | Wrapper | Target state | Caller signature |
|------:|-------:|---------|--------------|------------------|
| 49 | 0xC4 | `TopRide_KirbyGrindMethod` `0x802d9f2c` | KirbyGrind | `(this)` - also plays effect 0x8002e on `kirby+0x274` |
| 50 | 0xC8 | `TopRide_KirbyNormalMethod` `0x802da0f4` | KirbyNormal | `(this)` - also plays effect 0x8002f on `kirby+0x274` |
| 51 | 0xCC | `TopRide_KirbyDoodlebugMethod` `0x802da150` | KirbyDoodlebug (self) | `(this)` |
| 52 | 0xD0 | `TopRide_KirbyDoodlebugOutMethod` `0x802da1c0` | KirbyDoodlebugOut | `(this, ?, Vec3 *pos, Vec3 *src_pos, u16, u16)` |
| 63 | 0xFC | `TopRide_KirbyWhirlpoolMethod` `0x802da2ec` | KirbyWhirlpool | `(this, ?, Vec3 *pos, Vec3 *src_pos, u16, u16)` |
| 69 | 0x114 | `TopRide_KirbyTransparentMethod` `0x802d5e60` | KirbyTransparent | `(this)` - also calls `zz_802d8ac8_(this, 1)` to drop the held item |

Index 69 sits in the guarded cluster's address range but its body is bare-release. DoodlebugOut and Whirlpool install KirbyDamage as the base, overwrite the vtable, then compute knockback as `normalize(kirby_pos - src_pos)`; the other bare-release wrappers take `(this)` only.

Indices 53 (`0x802da23c`) and 54 (`0x802da258`) sit in the same range but are **not** state wrappers: 53 writes a `Vec3` to `kirby+0x148`, 54 sets flag bytes at `kirby+0x118..+0x119`, zeros `kirby+0xF4` and stores a float at `kirby+0xA4`. Neither touches `state_handler`.

Two neighbouring slots delegate into the current state: index 47 (`0xBC`, `TopRide_KirbyIsInvincible` `0x802d5590`) forwards to `state_handler->vt[+0x44]`, and index 48 (`0xC0`, `TopRide_KirbyStatePredicate2` `0x802d9ec0`) forwards to `vt[+0x4C]`.

## State Classes

19 derived state classes plus 3 abstract bases. Every state has a full RTTI record under `0x805d984x..0x805d9ad0`; the classes that are `dynamic_cast` targets additionally have a "compact" typeinfo entry in `0x805d9098..0x805d90f8`, which is what the guarded wrappers test against.

Animations are written by the setter as a string-pointer at `state_handler[1] + 0x4F4`, later resolved against the per-character anim table to a frame range.

| Class | ID | State vtable | get_state_id | Setter | Animation |
|-------|---:|-------------:|-------------:|-------:|-----------|
| KirbyOperate (base) | - | abstract | - | - | - |
| KirbyNormal (base) | 0 | `0x804d6f5c` | `0x802e4a44` | `0x802df844` | `AC_RUN_LOOP` |
| KirbyDamage (base) | 0 | `0x804da158` | `0x802e4a44` | `0x802f39a0` | none (subclasses pick) |
| KirbyPress | 2 | `0x804da070` | `0x802fe340` | `0x802f4068` | `AC_FLAT_START` (pancake) |
| KirbyCrush | 3 | `0x804d9ee0` | `0x802fdfd0` | `0x802f4a48` | `AC_TOBASARE` (blown away) |
| KirbyExplode | 4 | `0x804d9dd0` | `0x802fdc58` | `0x802f6138` | `AC_TOBASARE` |
| KirbyStrike | 5 | `0x804d9cbc` | `0x802fd8e0` | `0x802f6c28` | `AC_TOBASARE` |
| KirbySpin | 6 | `0x804d9a90` | `0x802fd43c` | `0x802f7718` | `AC_TOBASARE` |
| KirbySandSpin | 6 | `0x804d9bac` | `0x802fd43c` | `0x802f7b28` | inherits KirbySpin |
| KirbyNumb | 7 | `0x804d9980` | `0x802fd3a4` | `0x802f7f24` | `AC_SIBIRE` (paralysis) |
| KirbyElec | 8 | `0x804d9870` | `0x802fd194` | `0x802f858c` | `AC_SIBIRE` |
| KirbyWhirlpool | 9 | `0x804d9760` | `0x802fcfb4` | `0x802f8a5c` | inherits KirbyDamage |
| KirbyBurn | 10 | `0x804d964c` | `0x802fcc7c` | `0x802f958c` | `AC_FIRECRASH` |
| KirbyFreeze | 11 | `0x804d953c` | `0x802fc98c` | `0x802fa16c` | none - the tick drives its own |
| KirbyConfuse | 12 | `0x804d9434` | `0x802fc784` | `0x802faa88` | `AC_PANIC` |
| KirbyDoodlebugOut | 13 | `0x804d9328` | `0x802fc60c` | `0x802fb060` | inherits KirbyDamage |
| KirbyGrind | 14 | `0x804d91f8` | `0x802fc48c` | - | - |
| KirbyShortcut | 15 | `0x804d90e8` | `0x802fc31c` | `0x802fbaf0` | none |
| KirbyTransparent | 16 | `0x804da304` | `0x802fe3ac` | `0x802f3128` | none |
| KirbySpeedUp | 17 | `0x804dbcf8` | `0x80305890` | `0x802fe890` | `AC_RUN_LOOP` (effect overlay only) |
| KirbySpeedDown | 18 | `0x804dbac8` | `0x80305654` | `0x802ff98c` | none |
| KirbyDoodlebug (self) | 13 | `0x804d2488` | `0x802da550` | - | - |

The ID column is what `state_handler->vt[+0x0C]()` returns at runtime, and it does **not** line up with the `TopRideKirbyStateId` enum in `topride.h`: no state ever returns 1, because `KirbyNormal` inherits the same `get_state_id` (`0x802e4a44`, returns 0) as the abstract `KirbyDamage` base. Enum value 1 is a nominal label. State ID 13 is likewise ambiguous - `KirbyDoodlebugOut` and the self-state `KirbyDoodlebug` have distinct `get_state_id` functions that both return 13, which is deliberate: the Numb/Elec/Confuse wrappers bail on ID 13, so a kirby riding or inside a Doodlebug is immune to those status effects either way.

**Identify a state by its vtable pointer, not by its ID.** Compare `TopRide_KirbyStateVtable(kirby)` against the `TR_KSTATE_VT_*` constants; that is what the wrappers' `dynamic_cast` effectively does.

Durations are not a single u16 in the setter. Most states run a per-frame tick (`state_handler->vt[+0x28]`) that decrements an internal timer and transitions out through the state-machine helpers. Setters taking `u16` args (Explode/Strike/Spin) feed them into the state's `+0x5C`/`+0x6C` slots, which gate hit-frame and follow-up timing.

## Wrapper Guard Patterns

### Group A - invincibility + dynamic_cast

The standard pattern, used by 8 wrappers: Press, Burn, Freeze, Crush, Explode, Strike, Spin, SandSpin.

```c
void KirbyXxxMethod(TopRideKirby *kirby, /* state-specific args */)
{
    if (kirby->vtable[+0xBC](kirby))            // invincibility predicate
        return;
    if (dynamic_cast<KirbyXxx>(kirby))          // already in this state?
        return;
    zz_802d8ac8_(kirby, 1);                     // drop held item
    kirby->state_handler->vt[+8](-1);           // release current state
    setter_for_KirbyXxx(kirby, /* args */);     // install new state
}
```

Variants: **KirbyBurn** also blocks when currently in `KirbyElec` (double `dynamic_cast`). **KirbyCrush** has an `else` branch - when the cast *succeeds* (already in Crush) it re-applies knockback via the helper `0x802f53dc` instead of re-installing the state.

Pointer args are dereferenced by the setters, so a synthesised transition must pass real addresses, not literal zeroes. `KirbyBurn`'s `u32*` is dereferenced unconditionally and a literal `0` faults with a DSI. Crush/Explode/Strike/Spin take the magnitude of their `Vec3*` to size the knockback: the pointer must be valid, but an all-zero vector is legal and simply skips the knockback, giving a stun in place.

### Group A2 - dynamic_cast + extra guard, no invincibility

Shortcut (67) and SpeedDown (68). Same shape as Group A but neither loads `vt+0xBC`. Shortcut's extra guard is `(float)((int *)kirby)[0x4E] < FLOAT_805e3788` - the float at *byte* offset `0x138`, not byte `0x4E`. SpeedDown's is `state_handler->vt[+0x48]() == 0`.

### Group B - state-ID guard

Numb, Elec, Confuse. No invincibility check and no `dynamic_cast`; they bail when `state_handler->vt[+0x0C]() == 13` (Doodlebug, either form), otherwise release and call the setter.

### Group C - bare release-and-set

Grind (49), Normal (50), Doodlebug-self (51), DoodlebugOut (52), Whirlpool (63), Transparent (69). No guards at all. Calling them from mod code is safe but bypasses the natural state-machine flow.

### KirbySpeedUp has no wrapper

State ID 17 is reachable only the way `TopRide_KirbyApplyItem` does it at `0x802d8d60..0x802d8d8c`: load `kirby->state_handler`, call its `vt[+0x08](-1)` to release in place, then call the setter `0x802fe890` with `(state_handler, kirby + 0x80)`. The setter rewrites the existing state object in place, with no allocation. The same pattern works for the other internal setters near it.

## State-Handler Vtable Slots

Read as `kirby->state_handler->vtable[+N]`:

| Offset | Purpose | Notes |
|-------:|---------|-------|
| `+0x00` | typeinfo* | Points back to the state's full RTTI record |
| `+0x08` | `release(this, int)` | Called with `-1` to free the state on transition out |
| `+0x0C` | `get_state_id()` | Returns the ID column above |
| `+0x28` | per-frame physics tick | Animation, gravity, velocity update |
| `+0x44` | invincibility predicate | What `kirby->vt[+0xBC]` delegates to |
| `+0x48` | secondary predicate | Used by SpeedDown's extra guard |
| `+0x4C` | tertiary predicate | What `kirby->vt[+0xC0]` delegates to |
| `+0xE4` | AC_TOBASARE rescale callback | `0x802f3cfc` on most damage-derived vtables; Press overrides it with `0x802fe3a0` |

Most state vtables run ~58 slots (~0xE8 bytes) and inherit everything but the state-specific slots from KirbyDamage / KirbyNormal. **Vtable length is not uniform**: `KirbyConfuse`'s (`0x804d9434`) is only ~0xDC bytes, and the word at its `+0xE4` is non-pointer data - it has no AC_TOBASARE rescale slot, consistent with its tick never calling `vt[+0xE4]`. Don't index `vt[+0xE4]` on an arbitrary state without confirming the class has it.

## Vanilla Effector Triggers

State-applicator effectors live around `0x802e6...` and bridge a vanilla game event to the kirby state machine.

| Effector | Address | Invokes | Notes |
|----------|---------|---------|-------|
| `EffectorExplode_ApplyToKirby` | `0x802e6898` | KirbyExplodeMethod | Bombs, landmines, bomb-block traps. Computes knockback from `normalize(kirby_pos - explosion_pos) * mass / inv_distance`. Reads damage/hit-frame u16s from the EffectorExplode instance at `+0x42`/`+0x44`/`+0x46`. |
| `EffectorCrush_ApplyToKirby` | `0x802e6630` | KirbyCrushMethod | Heavy-machine collision (Wagon Star landing on a kirby) - a knockback launch, not a squish. The single `vt[+0xE8]` call site is the `bctrl` at `0x802e6738`. Args `(kirby, 0, &normalized_direction)`. |

The other state-trigger effectors (Burn for fire blocks, Freeze for ice, Elec for electric items) are inlined into per-item callbacks rather than standalone functions, and call the Kirby vtable wrapper directly.

## Invocation From Mod Code

Fetch `*stc_topride_kirbymgr`, require `round_state == 2`, then for each non-NULL `mgr->kirbys[i]` whose `TopRide_GetPlayerKind(k->player_slot)` is `TR_PKIND_HMN`, call one of the `TopRide_Kirby*` inline helpers in `topride.h`.

The `round_state == 2` (race active) gate is mandatory: before it, `state_handler`'s vtable is not fully wired and `vt[+8](-1)` will likely crash.

Most wrappers take extra args (knockback `Vec3`, hit-frame u16s, damage variant codes). Passing zeros - including a pointer to a zeroed stack `Vec3` - produces a static stun: the animation plays in place with default duration and no knockback impulse. Several args are dereferenced before use, so a literal `0` faults where a pointer to a stack zero does not: `KirbyBurn`'s arg2 (`lwz r0, 0(r30)` at `0x802d5674`) and `KirbySpin`'s `Vec3` (fed to `PSVECMagnitude` at `0x802f7998`) both need real addresses.

### Velocity handling and visibility

Two independent mechanisms decide what a mod-triggered state actually looks like.

**AC_TOBASARE rescale.** The rescale callback at state vtable `[+0xE4]` (`0x802f3cfc`) reads `kirby+0xA0` - the inline charge component's velocity `Vec3` - through `PSVECMagnitude`, branches `ble` to the epilogue when `magnitude * scale <= threshold`, and otherwise normalizes-and-rescales the velocity in place. A kirby with non-zero entry velocity therefore has its launch direction locked to its running direction with the magnitude re-applied every frame, which reads as a teleport. Zero entry velocity takes the early exit and nothing happens. The teleport condition is **(the tick at `vt[+0x28]` calls `vt[+0xE4]`) AND (the setter did not zero velocity)**; the mere presence of the `[+0xE4]` slot is not the discriminator.

**Input gating.** A damage state visibly stuns only if its tick at `vt[+0x28]` is *not* the shared `zz_802f3bd0_` (which has no input-gating logic), or its setter installs a JObj overlay (Freeze's ice block), or it uses a distinct animation that reads as a hit without an input lock (Press's `AC_FLAT_START` pancake). The animation string is not the discriminator: `AC_SIBIRE` is shared by the visible Numb and the silent Elec, and `AC_TOBASARE` by Strike/Explode/Crush.

`mods/archipelago/src/deathlink.c` zeroes `kirby->charge.velocity` immediately **before and after** the wrapper call - the pre-zero pre-empts setters that scale entry velocity, the post-zero overrides setters that write a value of their own (Crush's helper `0x802f53dc` `PSVECNormalize`s the `&zero` `Vec3` *argument*, producing NaN regardless of what `kirby+0xA0` held). Results in that configuration:

| State | ID | Tick fn at `vt[+0x28]` | Calls `vt[+0xE4]`? | Setter's own velocity write | Outcome |
|-------|---:|------------------------|--------------------|-----------------------------|---------|
| Press | 2 | `zz_802f3bd0_` (shared) | yes | zeroes it (`stfs f0, 32/36/40(r3)`) | Visible pancake squish |
| Freeze | 11 | `0x802fc994` (unique) | no | none (tick doesn't read velocity) | Clean ice-block stun |
| Numb | 7 | `0x802fd3ac` (unique - calls `TopRide_VelocityDecay`, then `vt[+0xE4]`) | yes | none | Visible paralysis pose; the decay sub-call supplies the input gating |
| Confuse | 12 | `TopRide_VelocityDecay` `0x802d1d84` | no | none | Clean panic-spin stun |
| Elec | 8 | `zz_802f3bd0_` (shared) | yes | zeroes it (same 3-store pattern) | **Silent** - state lands, kirby keeps full control |
| Strike | 5 | `zz_802f3bd0_` (shared) | yes | scales entry velocity by ~0.31 | **Silent** |
| Explode | 4 | `zz_802f3bd0_` (shared) | yes | scales entry velocity by ~0.5 (by mass) | **Silent** |
| Crush | 3 | `zz_802f3bd0_` (shared) | yes | writes `(NaN, ~1.69, NaN)` via `0x802f53dc` | Brief vertical spin, not a readable hit reaction |
| Spin | 6 | `0x802fd49c` (unique) | yes (via shared helper) | none | **Still ejects the kirby** - the unique tick does `velocity.y -= gravity` with no `PSVECScale` drag step, so velocity re-accumulates every frame |

The shipped deathlink pool is therefore **Press, Freeze, Numb, Confuse**.

**Why Elec is silent** is structural, not a missing runtime precondition. The state lands (ID reads 8, velocity is zero) but the kirby keeps input control and shows no paralysis pose. `KirbyElec`'s vtable inherits its `[+0x10..+0x24]` predicate slots from KirbyDamage, and those are trivial stubs (`li r3, 1; blr` at `0x802da57c..0x802da59c`, `li r3, 0; blr` at `0x802d4b90`). Confuse, which *does* visibly stun, overrides those same slots with real implementations (`0x802d4b98..0x802d4bb8` and `0x802d1d84`). The engine queries them each frame and only gates input when the state class supplies its own. Vanilla electric items must do extra call-site setup - a separate visual/input-lock effect, or a kirby-level flag one of the predicates reads - that a zero-arg wrapper call does not.

**The overlay-JObj asserts in the Freeze / Confuse setters are not load-bearing in normal play.** Both dereference `kirby+0x80+0x534` (ice block) and `kirby+0x80+0x544` (confuse marks) and `__assert` on null. Those JObjs are populated whenever `round_state == 2`, so the path is safe; a defensive null guard is still cheap.

## Constraints

- **`KirbySandSpin` (idx 62) is a `KirbySpin` subclass.** Its `dynamic_cast` guard tests the parent, so it correctly blocks re-entry from either Spin variant. Idx 61 installs the parent's vtable; SandSpin's setter calls KirbySpin's setter as super-init and then overrides the vtable.
- **No win/lose/dance state classes exist.** The round-end animations live outside this state machine.
- **`KirbySpin` cannot be made safe by apply-time velocity zeroing.** Using it would need continuous per-frame velocity zeroing or an engine-level patch on the gravity store at `0x802fd578`; excluding it from a trap/deathlink pool is the cheap option.
- **`KirbyBurn` needs a real non-zero float duration seed.** The wrapper dereferences arg2 at `0x802d5674` and the setter reads `*arg2` as a *float* (`lfs f29, 0(r30)` at `0x802f9890`) to seed `state.f04+0x24` as `*arg2 / 60.0`. The shared per-frame proc decrements that field by 6.0 per frame, so `*arg2 = 0.0` never terminates and the kirby never visibly burns. Vanilla callers at `0x80299dd4` and `0x80321a14` derive the value from effector context (`this+0x48`) or per-tick math (`frame * 0.277`) - there is no static constant to copy, which is why Burn stays out of the deathlink pool.

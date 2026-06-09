# Top Ride Kirby States

## Overview

Each Top Ride Kirby has a polymorphic state object at `TopRideKirby+0x7C` (`state_handler`). The kirby is "in" a state by virtue of which class instance lives in that slot. State classes derive from `KirbyOperate` and `KirbyNormal`/`KirbyDamage`; state transitions are typically driven by methods on the Kirby class itself (vtable at `0x804d2304`).

Vanilla bombs/landmines invoke `KirbyExplode` ("tumble") via `EffectorExplode_ApplyToKirby`. Vanilla heavy-machine collisions invoke `KirbyCrush` (a knockback launch — same `AC_TOBASARE` animation family as Explode, NOT a squish) via `EffectorCrush_ApplyToKirby`; the actual squish-flat animation belongs to `KirbyPress`. The full state set is documented below for direct invocation from mod code (deathlink, traplink, custom traps, etc.).

## Architecture

```
TopRideKirby (vtable 0x804d2304, RTTI "Kirby")
  ├── +0x00: vtable          (82 entries, ~0x148 bytes)
  ├── +0x7C: state_handler   ──► State instance (vtable + per-state data)
  │                                ├── +0x00: vtable[0]   (typeinfo*)
  │                                ├── +0x08: release(int)
  │                                ├── +0x0C: get_state_id() → int
  │                                ├── +0x44: invincibility predicate
  │                                ├── ...
  │                                └── +0xE8+: state-specific finalizers
  └── +0x80: ChargeComponent (inline)
```

A state transition is the sequence:

1. (Optional) `kirby->vtable[+0xBC]()` — invincibility predicate (delegates to `state_handler->vtable[+0x44]`). If immune, abort.
2. (Optional) `dynamic_cast<KirbyTargetState*>(kirby->state_handler)` — if the kirby is already in the target state, abort.
3. `zz_802d8ac8_(kirby, 1)` — drop any held item.
4. `state_handler->vtable[+8](-1)` — release/destroy the current state.
5. Per-state setter writes the new state's vtable into `state_handler` and configures the new state's data (animation, timer, knockback, etc.).

The Kirby vtable contains one wrapper method per "externally invocable" state (the ones with vanilla effector callers and/or item callers).

## The Kirby Vtable at `0x804d2304`

82 function-pointer entries (`0x000..0x148`), RTTI class name `"Kirby"` (the type-info name string lives at `0x805d9134`; every TR kirby instance is a `Kirby`). RTTI typeinfo record for the vtable itself: `0x805d913c` (a `__si_class_type_info` — its name pointer at `+0x00` resolves to the `"Kirby"` string at `0x805d9134`, so the doc's old `"KirbyDoodlebug"` label was wrong; the header `externals/hoshi/include/topride.h` already says `"Kirby"`).

The 13 invocable state-transition wrappers cluster at byte offsets `0xDC..0x114` (indices 55–69):

| Index | Offset | Wrapper | Target state | Setter | Caller signature |
|------:|-------:|---------|--------------|--------|------------------|
| 55 | 0xDC | `0x802d54ec` | KirbyPress | `0x802f4068` | `(this)` |
| 56 | 0xE0 | `0x802d55c0` | KirbyBurn | `0x802f958c` | `(this, u32, u32, u32)` |
| 57 | 0xE4 | `0x802d56bc` | KirbyFreeze | `0x802fa16c` | `(this)` |
| **58** | **0xE8** | **`0x802d5760`** | **KirbyCrush** ← squish | **`0x802f4a48`** | **`(this, u16, Vec3*)`** |
| 59 | 0xEC | `0x802d5834` | KirbyExplode ← tumble | `0x802f6138` | `(this, u16, Vec3*, u16, u16)` |
| 60 | 0xF0 | `0x802d5900` | KirbyStrike | `0x802f6c28` | `(this, u16, Vec3*, u16, u16)` |
| 61 | 0xF4 | `0x802d59cc` | KirbySpin | `0x802f7718` | `(this, u32, u32, u32)` |
| 62 | 0xF8 | `0x802d5aa0` | KirbySandSpin | `0x802f7b28` | `(this, u32, u32, u32)` |
| 64 | 0x100 | `0x802d5b74` | KirbyNumb | `0x802f7f24` | `(this)` |
| 65 | 0x104 | `0x802d5be4` | KirbyElec | `0x802f858c` | `(this, u32)` |
| 66 | 0x108 | `0x802d5c64` | KirbyConfuse | `0x802faa88` | `(this, u32)` |
| 67 | 0x10C | `0x802d5ce4` | KirbyShortcut | `0x802fbaf0` | `(this, u32, u32, u32)` |
| 68 | 0x110 | `0x802d5da4` | KirbySpeedDown | `0x802ff98c` | `(this, u32)` |
| 69 | 0x114 | `0x802d5e60` | KirbyTransparent | `0x802f3128` | `(this)` |

Bare-release wrappers (no invincibility / dynamic_cast guard, install state unconditionally) — these install "internal" states normally entered by the engine itself:

| Index | Offset | Wrapper | Target state | Caller signature |
|------:|-------:|---------|--------------|------------------|
| 49 | 0xC4 | `0x802d9f2c` | KirbyGrind | `(this)` — also plays effect 0x8002e on `kirby+0x274` |
| 50 | 0xC8 | `0x802da0f4` | KirbyNormal | `(this)` — also plays effect 0x8002f on `kirby+0x274` |
| 51 | 0xCC | `0x802da150` | KirbyDoodlebug (self) | `(this)` |
| 52 | 0xD0 | `0x802da1c0` | KirbyDoodlebugOut | `(this, ?, Vec3 *pos, Vec3 *src_pos, u16, u16)` — knockback math (PSVECSubtract pos − src_pos, normalize) |
| 63 | 0xFC | `0x802da2ec` | KirbyWhirlpool | `(this, ?, Vec3 *pos, Vec3 *src_pos, u16, u16)` — same knockback math as DoodlebugOut |
| 69 | 0x114 | `0x802d5e60` | KirbyTransparent | `(this)` — also calls `zz_802d8ac8_(this, 1)` to drop held item |

(Index 69 is in the 0xDC..0x114 cluster but its body is bare-release, not Group A — listed here for completeness.)

Vtable indices 53 (`0xD4`, `0x802da23c`) and 54 (`0xD8`, `0x802da258`) sit in the same address range but are **not** state-transition wrappers. 53 writes a `Vec3` to `kirby+0x148..+0x150`; 54 sets two flag bytes at `kirby+0x118..+0x119`, zeros `kirby+0xF4`, and stores a computed float at `kirby+0xA4`. They are scalar field setters on the kirby itself, with no effect on `state_handler`.

Other vtable slots (queries, getters, list ops, RTTI helpers) are not relevant to state transitions and are documented inline in the Ghidra database.

Notable non-wrapper slots:

| Index | Offset | Address | Purpose |
|------:|-------:|---------|---------|
| 47 | 0xBC | `0x802d5590` | Invincibility predicate (delegates to `state_handler->vt[+0x44]`) |
| 48 | 0xC0 | `0x802d9ec0` | State predicate (delegates to `state_handler->vt[+0x4C]`). Symbol: `TopRide_KirbyStatePredicate2`. |

## State Classes

19 derived state classes plus 3 abstract bases. Every state has:

- A **typeinfo** record under `0x805d984x..0x805d9ad0` ("full" RTTI). Some also have a "compact" entry under `0x805d9098..0x805d90f8` used as the dynamic_cast target by Group A wrappers.
- A **state vtable** (different from the Kirby vtable) — read at runtime as `state_handler->vtable[N]()`.
- A **state ID** returned by `state_handler->vt[+0x0C]()` (tracked column below). This is the *runtime* value the get_state_id slot returns — verified by decompiling each vtable's `+0x0C` slot. It is **not** the same as the `TopRideKirbyStateId` enum in `topride.h`: the enum assigns `TR_KSTATE_NORMAL = 1`, but **no state's get_state_id ever returns 1** — `KirbyNormal` inherits the same `get_state_id` (`0x802e4a44`, returns 0) as `KirbyDamage`. So the enum value 1 is a nominal label that never appears at runtime; treat "in Normal" as get_state_id == 0 (and disambiguate from the abstract base via vtable pointer — see below).
- A **setter** function that writes the state vtable and constructs the per-state data when transitioning in.

| Class | Compact RTTI | Full typeinfo | State vtable | State ID | get_state_id fn | Wrapper |
|-------|-------------:|--------------:|-------------:|---------:|----------------:|--------:|
| KirbyOperate (base) | `0x805d9098` | — | — (abstract) | — | — | — |
| KirbyNormal (base) | `0x805d90a0` | `0x805d94c8` | `0x804d6f5c` | 0 *(enum 1)* | `0x802e4a44` | 50 |
| KirbyDamage (base) | `0x805d90a8` | `0x805d9840` | `0x804da158` | 0 | `0x802e4a44` | — |
| KirbyPress | `0x805d90b0` | `0x805d98c0` | `0x804da070` | 2 | `0x802fe340` | 55 |
| **KirbyCrush** | **`0x805d90d0`** | **`0x805d98b0`** | **`0x804d9ee0`** | **3** | **`0x802fdfd0`** | **58** |
| KirbyExplode | `0x805d90d8` | `0x805d98a8` | `0x804d9dd0` | 4 | `0x802fdc58` | 59 |
| KirbyStrike | `0x805d90e0` | `0x805d98a0` | `0x804d9cbc` | 5 | `0x802fd8e0` | 60 |
| KirbySpin | `0x805d90e8` | `0x805d9890` | `0x804d9a90` | 6 | `0x802fd43c` | 61 |
| KirbySandSpin | — | `0x805d9898` | `0x804d9bac` | 6 | `0x802fd43c` | 62 |
| KirbyNumb | — | `0x805d9888` | `0x804d9980` | 7 | `0x802fd3a4` | 64 |
| KirbyElec | `0x805d90c0` | `0x805d9880` | `0x804d9870` | 8 | `0x802fd194` | 65 |
| KirbyWhirlpool | — | `0x805d9878` | `0x804d9760` | 9 | `0x802fcfb4` | (internal) |
| KirbyBurn | `0x805d90b8` | `0x805d9870` | `0x804d964c` | 10 | `0x802fcc7c` | 56 |
| KirbyFreeze | `0x805d90c8` | `0x805d9868` | `0x804d953c` | 11 | `0x802fc98c` | 57 |
| KirbyConfuse | — | `0x805d9860` | `0x804d9434` | 12 | `0x802fc784` | 66 |
| KirbyDoodlebugOut | — | `0x805d9858` | `0x804d9328` | 13 | `0x802fc60c` | 52 |
| KirbyGrind | — | `0x805d9850` | `0x804d91f8` | 14 | `0x802fc48c` | 49 |
| KirbyShortcut | `0x805d90f0` | `0x805d9848` | `0x804d90e8` | 15 | `0x802fc31c` | 67 |
| KirbyTransparent | — | `0x805d98d0` | `0x804da304` | 16 | `0x802fe3ac` | 69 |
| KirbySpeedUp | — | `0x805d9ad0` | `0x804dbcf8` | 17 | `0x80305890` | (internal — item pickup; setter `0x802fe890`) |
| KirbySpeedDown | `0x805d90f8` | `0x805d9ab0` | `0x804dbac8` | 18 | `0x80305654` | 68 |
| KirbyDoodlebug (self) | — | `0x805d9144` | `0x804d2488` | 13 | `0x802da550` | 51 |

State ID 13 is a sentinel shared by **both** `KirbyDoodlebugOut` (`0x802fc60c`) and the self-state `KirbyDoodlebug` (`0x802da550`) — their distinct get_state_id functions both `return 13`. Group B wrappers (Numb / Elec / Confuse) explicitly bail when state ID == 13, so a kirby that is riding/inside the Doodlebug item (either form) is immune to those status effects.

## State Wrapper Patterns

Wrappers fall into four groups by guard logic.

### Group A — invincibility + dynamic_cast

The standard pattern. Used by **8** wrappers: Press, Burn, Freeze, Crush, Explode, Strike, Spin, SandSpin. (Shortcut and SpeedDown were previously lumped here but do **not** call the invincibility predicate — see Group A2.)

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

Variants:
- **KirbyBurn** also blocks when currently in `KirbyElec` (double dynamic_cast).
- **KirbyCrush** has an `else` branch: when the dynamic_cast *succeeds* (already in Crush) it re-applies the knockback via the helper `0x802f53dc` (PSVECNormalizes the Vec3 arg) instead of re-installing the state.

### Group A2 — dynamic_cast + extra guard, NO invincibility

Used by Shortcut (67) and SpeedDown (68). Same shape as Group A but the `kirby->vt[+0xBC]` invincibility predicate is **absent** (neither wrapper loads `vt+0xBC`). They guard on dynamic_cast plus one extra condition:
- **KirbyShortcut** extra guard: `(float)((int *)kirby)[0x4E] < FLOAT_805e3788` — i.e. the float at byte offset `0x138` (index `0x4E` into the `int*` view of the kirby), NOT byte `0x4E`.
- **KirbySpeedDown** extra guard: `state_handler->vt[+0x48]() == 0`.

### Group B — state-ID guard (skip dynamic_cast)

Used by Numb, Elec, Confuse. Bails when `state_handler->vt[+0x0C]() == 13` (i.e. kirby is currently `KirbyDoodlebugOut` — or `KirbyDoodlebug`-self, which shares get_state_id 13). Otherwise releases current state and calls the setter — no invincibility check.

### Group C — bare release-and-set

Used by Grind (49), Normal (50), Doodlebug-self (51), DoodlebugOut (52), Whirlpool (63), Transparent (69). No guards. Always releases current state and installs the new one. These are "engine-internal" transitions; calling them from mod code is safe but they bypass the natural state machine flow.

DoodlebugOut and Whirlpool wrappers take `(this, ?, Vec3 *kirby_pos, Vec3 *src_pos, u16, u16)` — they install KirbyDamage as base and overwrite the vtable, then compute knockback as `normalize(kirby_pos − src_pos)`. The other Group C wrappers take `(this)` only.

### Calling KirbySpeedUp from mod code

KirbySpeedUp (state ID 17) has no Kirby vtable wrapper. The vanilla call site in `TopRide_KirbyApplyItem` (`0x802d8d60..0x802d8d8c`) is:

```
r3 = kirby->state_handler;     // kirby + 0x7C
state_handler->vt[+0x08](-1);  // release current state in place
KirbySpeedUpSetter(state_handler, kirby + 0x80);  // 0x802fe890
```

The setter rewrites the existing state object in place (no allocation). Same pattern works for the other "internal" setters near it (KirbySpeedDown is `0x802ff98c` but is exposed as wrapper 68 — use that instead).

## State-Handler Vtable Slots (recurring offsets)

Read as `kirby->state_handler->vtable[+N]`:

| Offset | Purpose | Notes |
|-------:|---------|-------|
| `+0x00` | typeinfo* | Points back to the state's full RTTI record |
| `+0x04` | (offset_to_top, MI) | Always 0 for these |
| `+0x08` | `release(this, int)` | Called with `-1` to free state on transition out |
| `+0x0C` | `get_state_id()` → int | Returns the state ID column above |
| `+0x28` | per-frame physics tick | Animation, gravity, velocity update |
| `+0x44` | invincibility predicate | What `kirby->vt[+0xBC]` delegates to |
| `+0x48` | secondary predicate | Used by SpeedDown's extra guard |
| `+0x4C` | tertiary predicate | What `kirby->vt[+0xC0]` delegates to |

Most state vtables run ~58 slots (~0xE8 bytes) and inherit most slots from the parent class's vtable (KirbyDamage / KirbyNormal); only the state-specific slots (animation, transitions to next state, hit response) are overridden per-class. **Vtable length is not uniform**, though — e.g. `KirbyConfuse`'s vtable (`0x804d9434`) is shorter (~0xDC bytes); the word at its `+0xE4` is non-pointer data (it has no AC_TOBASARE rescale slot, consistent with its tick never calling `vt[+0xE4]`). Don't blindly index `vt[+0xE4]` on an arbitrary state — confirm the class actually has that slot.

## Vanilla Effector Triggers

State-applicator effectors live in `0x802e6...` and bridge a vanilla game event (bomb explosion, machine collision) to the kirby state machine.

| Effector | Address | Invokes | Notes |
|----------|---------|---------|-------|
| `EffectorExplode_ApplyToKirby` | `0x802e6898` | KirbyExplodeMethod | Bombs, landmines, bomb-block traps. Computes knockback Vec3 from `(kirby_pos - explosion_pos)` normalized × `mass / inv_distance`. Reads damage/hit-frame u16s from the EffectorExplode instance at `+0x42`/`+0x44`/`+0x46`. |
| `EffectorCrush_ApplyToKirby` | `0x802e6630` | KirbyCrushMethod | Heavy-machine collision (Wagon Star landing on a kirby) — applies a knockback launch (`AC_TOBASARE`), not a squish. Function spans `0x802e6630..~0x802e6764`; the single `vt[+0xE8]` (Crush) call site is the `bctrl` at `0x802e6738`. Args `(kirby, 0, &normalized_direction)`. |

The other state-trigger effectors (Burn for fire blocks, Freeze for ice, Elec for electric items, etc.) are typically inlined into per-item callbacks rather than living as standalone functions — they call the Kirby vtable wrapper directly with appropriate args.

## Invocation From Mod Code

The standard recipe (matches the existing `TopRide_KirbyExplode` helper in `topride.h`):

```c
TopRideKirbyMgr *mgr = *stc_topride_kirbymgr;
if (!mgr || mgr->round_state != 2)
    return;                                  // race must be active

for (int i = 0; i < 4; i++)
{
    TopRideKirby *k = mgr->kirbys[i];
    if (!k) continue;
    if (TopRide_GetPlayerKind(k->player_slot) != TR_PKIND_HMN) continue;
    TopRide_KirbyCrush(k);                   // or any other state helper
}
```

### Round-state gate

`round_state` is at `KirbyMgr+0x4028`:

| Value | Meaning | Safe to invoke states? |
|------:|---------|------------------------|
| 0 | Pre-init | No — state_handler may be partially uninitialized |
| 1 | Countdown | No — same as above |
| 2 | Race active | **Yes** |

Calling a state wrapper before round_state == 2 will likely crash inside `state_handler->vt[+8](-1)` (release-current-state), since the initial state vtable may not be fully wired.

### Args defaults

Most wrappers accept extra args (knockback Vec3, hit-frame u16s, damage variant codes). Passing zeros / a zero Vec3 produces a static stun: animation plays in place, no knockback impulse, default hit duration. Matching vanilla-quality knockback would mean synthesizing a fake source position and reusing the per-effector knockback math (mass × normalized direction / distance) — that's a future refinement and is not required for trap effects.

### Empirical results from mod-triggered application

Tested through the deathlink receive path with zero-args wrappers (`round_state == 2`, single-player Top Ride, kirby running normally at the moment of the trigger). Each row's `state_after` was read via `TopRide_KirbyGetStateId(kirby)` immediately after `apply()`.

### First pass — bare wrapper, no velocity preconditioning

| State | state_after | Setter wrote vel = | Outcome |
|---|---|---|---|
| Press | 2 | (0,0,0) — setter explicitly zeros | Untested visually, predicted safe |
| Crush | 3 | (NaN, ~1.69, NaN) — setter's helper at `0x802f53dc` PSVECNormalizes the `&zero` Vec3 arg | Teleport |
| Explode | 4 | ~0.5 × vel_before (PSVECScale by per-character mass) | Teleport |
| Strike | 5 | ~0.31 × vel_before (same path, different scalar) | Teleport |
| Spin | 6 | unchanged (= entry velocity) | **Full eject from track** |
| Freeze | 11 | unchanged | Clean ice-block stun |
| Numb | 7 | unchanged | Untested |
| Elec | 8 | (0,0,0) — setter explicitly zeros | State lands but no observable effect |
| Confuse | 12 | unchanged | Clean panic-spin stun |

### Second pass — pre-apply velocity zero (`*(Vec3*)(kirby+0xA0) = 0` before wrapper)

| State | vel_after | Visual outcome |
|---|---|---|
| Press, Strike, Spin, Freeze, Numb, Elec, Confuse | (0,0,0) | All clean — Strike/Numb no longer teleport |
| Explode | (0,0,0) | Clean (its PSVECScale of zero entry velocity yields zero) |
| Crush | **(NaN, 1.69, NaN)** | Still NaN — Crush's setter doesn't read `kirby+0xA0`; its helper PSVECNormalizes the Vec3 *arg* (we pass `&zero`) which is the actual NaN source. Kirby still glitches because the next tick reads the NaN'd `kirby+0xA0`. |

### Third pass — pre-apply AND post-apply velocity zero

| State | vel_after (logged before post-zero) | Visual outcome | In current pool? |
|---|---|---|---|
| Press | (0,0,0) | Visible pancake squish (`AC_FLAT_START`) | yes |
| Freeze | (0,0,0) | Clean ice-block stun | yes |
| Numb | (0,0,0) | Visible paralysis pose (despite sharing `AC_SIBIRE` with the silent Elec — its unique tick `0x802fd3ac` calls `0x802d1d84`, the same helper Confuse uses, which contains the input-gating logic) | yes |
| Confuse | (0,0,0) | Clean panic-spin stun | yes |
| Strike | (0,0,0) | **Silent — kirby keeps running with full control** (predicate-inheritance, same as Elec/Explode) | no |
| Explode | (0,0,0) | **Silent** | no |
| Elec | (0,0,0) | **Silent** | no |
| Crush | (NaN, 1.69, NaN) (post-zero overrides) | Brief vertical spin only — not a recognizable hit reaction | no |
| Spin | (0,0,0) at apply time | **Still ejects kirby** — unique tick `0x802fd49c` does `velocity.y -= gravity` then calls `vt[+0xE4]` (rescale) with no PSVECScale drag step | no |

The third pass is the current `mods/archipelago/src/deathlink.c` behavior. Final pool: **Press, Freeze, Numb, Confuse** (4 states).

**Visibility pattern (refined)**: a damage state visibly stuns if and only if its per-frame tick at `vt[+0x28]` is *not* the shared `0x802f3bd0` (which has no input-gating logic), OR its setter installs a JObj overlay (Freeze ice block), OR it uses a distinct animation that reads as a hit without input lock (Press's `AC_FLAT_START` pancake). The tick is the strongest discriminator — Numb's `0x802fd3ac` calls `0x802d1d84`, the same helper Confuse uses, and that helper provides the input-gating that makes both stun visibly. Strike / Explode / Elec all share the bare `0x802f3bd0` tick AND lack any of the alternate visibility mechanisms, so they're silent. The animation string (`AC_SIBIRE` for Numb and Elec, `AC_TOBASARE` for Strike/Explode/Crush) is *not* the discriminator.

**The AC_TOBASARE rescale callback is at state vtable `[+0xE4]` = `0x802f3cfc`.** It reads `kirby+0xA0` (the inline ChargeComponent's velocity Vec3) via `PSVECMagnitude`, branches `ble` to the epilogue when `magnitude * scale ≤ threshold`, and otherwise normalizes-and-rescales the velocity in place. So a kirby with non-zero entry-velocity has its launch direction "locked" to its running direction and the magnitude re-applied every frame. Zero entry-velocity → branch taken → no transform. **Most damage-derived state vtables carry this exact `0x802f3cfc` slot at `[+0xE4]`** (verified: Crush, Explode, Strike, Elec, Spin, Numb, Freeze all have it). **Exceptions:** Press *overrides* `[+0xE4]` with its own `0x802fe3a0`, and Confuse's vtable is too short to even have a `[+0xE4]` slot (the word there is non-pointer data). The `[+0xE4]` slot's presence is not the discriminator; what matters is whether `[+0x28]` (the per-frame tick) actually invokes `vt[+0xE4]`, *and* whether the setter zeroed velocity before the first tick.

### Per-frame tick at state_vt[+0x28]

| State | Tick fn | Calls `vt[+0xE4]`? | Setter zeroes velocity? |
|---|---|---|---|
| Press | `0x802f3bd0` (shared AC_TOBASARE tick) | yes | **yes** (`0x802f4068` writes `stfs f0, 32/36/40(r3)`) |
| Crush | `0x802f3bd0` | yes | no |
| Explode | `0x802f3bd0` | yes | no |
| Strike | `0x802f3bd0` | yes | no |
| Elec | `0x802f3bd0` | yes | **yes** (`0x802f858c` writes the same 3-store pattern) |
| Spin | `0x802fd49c` (unique) | (calls AC_TOBASARE callback indirectly via shared helper) | no |
| Numb | `0x802fd3ac` (unique — calls `0x802d1d84` then `vt[+0xE4]`) | yes | no |
| Freeze | `0x802fc994` (unique) | no | no (but tick doesn't read velocity) |
| Confuse | `0x802d1d84` (unique) | (no `vt[+0xE4]` call in tick body) | no |

So the AC_TOBASARE teleport reduces to: **(tick calls `vt[+0xE4]`) AND (setter doesn't zero velocity)**. Crush, Explode, Strike, Spin, and Numb all hit this combination. Press and Elec hit only the first half — their setters explicitly zero velocity, so the callback's `ble` early-exit fires and the rescale is skipped. To make Crush/Explode/Strike/Spin/Numb safe, zero `*(Vec3*)(kirby + 0xA0) = {0,0,0}` immediately before invoking the wrapper. Untested.

**Elec lands but does nothing visible — the cause is structural inheritance, not a missing runtime precondition.** `state_after` reads 8 (`KirbyElec`) and `vel_after` is zero (setter zeroes it), but kirby continues to run with full input control and no paralysis animation. The KirbyElec state vtable inherits its `[+0x10..+0x24]` predicate slots from KirbyDamage — and those slots are trivial stubs (`li r3, 1; blr` at `0x802da57c..0x802da59c` and `li r3, 0; blr` at `0x802d4b90`). Confuse's vtable, which *does* visibly stun, **overrides** those same slots with non-stub implementations (`0x802d4b98..0x802d4bb8` and `0x802d1d84`). The engine's input/movement code presumably queries one or more of those predicates each frame and only gates input when the state-class supplies its own implementation. Vanilla electric items therefore must do something extra at the call site — either trigger a separate visual/input-lock effect, or write a kirby-level flag that one of the predicates checks — that our zero-arg wrapper does not. Until that extra setup is identified, Elec is silent in mod-triggered application.

**`__assert` overlay-JObj checks in Freeze / Confuse setters are not load-bearing in normal play.** Both setters dereference `kirby+0x80+0x534` (Freeze ice block) and `kirby+0x80+0x544` (Confuse marks) and `__assert` on null. In testing, all overlay JObjs were `set` whenever the deathlink triggered during round_state == 2. Static analysis suggested this could hang the GC; empirically the field is reliably populated, so the worry was overblown. Still worth a runtime null-guard in defensive code — vanilla freeze items reach the same path through a stricter precondition.

**`TopRide_KirbyGetStateId` returns 0 for kirbys in normal play**, because `KirbyNormal`'s `get_state_id` slot (state vtable `[+0x0C]`) is `0x802e4a44` — the *same* function as `KirbyDamage`'s, which `return 0`. This is the table value (State ID column shows `0 (enum 1)` for Normal): the `TR_KSTATE_NORMAL = 1` enum is purely nominal and never matches a runtime get_state_id. The helper's return values are reliable for the damage states (verified per-class: Press=2, Crush=3, Explode=4, Strike=5, Spin/SandSpin=6, Numb=7, Elec=8, Whirlpool=9, Burn=10, Freeze=11, Confuse=12, DoodlebugOut/Doodlebug-self=13, Grind=14, Shortcut=15, Transparent=16, SpeedUp=17, SpeedDown=18). Only the "running normally" (Normal vs. abstract Damage base) reading is ambiguous, since both return 0. If you need to distinguish "in Normal" from anything else, check `state_handler->vtable == 0x804d6f5c` directly (`TR_KSTATE_VT_NORMAL`).

## Per-State Animation IDs

Animations are written by setters as a string-pointer at `kirby->state_anim_ptr` (struct offset `+0x4F4` from the inner state-data block, i.e. `state_handler[1] + 0x4F4`). The string is later resolved against the per-character anim table to a frame range. Strings observed in the setters:

| Setter | State | Animation string | Notes |
|--------|-------|------------------|-------|
| `0x802df844` | KirbyNormal | `AC_RUN_LOOP` | Default running anim. |
| `0x802f4068` | KirbyPress | `AC_FLAT_START` | Pancake/squish. |
| `0x802f4a48` | KirbyCrush | `AC_TOBASARE` | "Blown away" — actually a knockback launch, despite the doc historically calling Crush a squish. The squish animation is KirbyPress. |
| `0x802f6138` | KirbyExplode | `AC_TOBASARE` | Same launch anim as Crush; the state itself differs in duration / state-machine transitions. |
| `0x802f6c28` | KirbyStrike | `AC_TOBASARE` | |
| `0x802f7718` | KirbySpin | `AC_TOBASARE` | |
| `0x802f7b28` | KirbySandSpin | (no direct AC_ ref — inherits from KirbySpin parent setter) | |
| `0x802f7f24` | KirbyNumb | `AC_SIBIRE` | "Shibire" = paralysis/numb. |
| `0x802f858c` | KirbyElec | `AC_SIBIRE` | Same numb anim. |
| `0x802f958c` | KirbyBurn | `AC_FIRECRASH` | |
| `0x802fa16c` | KirbyFreeze | (no direct AC_ ref — uses the vtable's per-frame tick to drive its own anim) | |
| `0x802faa88` | KirbyConfuse | `AC_PANIC` | |
| `0x802fb060` | KirbyDoodlebugOut | (no direct AC_ ref) | Inherits KirbyDamage base anim. |
| `0x802fbaf0` | KirbyShortcut | (no direct AC_ ref) | |
| `0x802ff98c` | KirbySpeedDown | (no direct AC_ ref) | |
| `0x802fe890` | KirbySpeedUp | `AC_RUN_LOOP` | Resumes Normal's anim — the visible difference is a particle effect overlay, not a new pose. |
| `0x802f3128` | KirbyTransparent | (no direct AC_ ref) | |
| `0x802f8a5c` | KirbyWhirlpool | (no direct AC_ ref) | Inherits KirbyDamage base anim. |
| `0x802f39a0` | KirbyDamage (base) | (no direct AC_ ref) | All Damage-derived states inherit this; the per-state vtable's tick may pick a sub-animation. |

Durations aren't a single u16 in the setter — most states use a per-frame tick (`state_handler->vt[+0x28]`) that decrements an internal timer field on the state and transitions out via the state-machine helpers. Setters that take `u16` args (Explode/Strike/Spin: `(this, u16, Vec3*, u16, u16)`) pass those into the state's `+0x5C`/`+0x6C` slots, which gate hit-frame and follow-up timing.

## Caveats & Open Items

1. **`KirbySandSpin` (idx 62) is a `KirbySpin` subclass.** Its dynamic_cast guard tests `KirbySpin` (the parent), so it correctly blocks re-entry from either Spin variant. The "pure" KirbySpin entry (idx 61) installs the parent class's vtable; KirbySandSpin's setter calls KirbySpin's setter as super-init then overrides the vtable.
2. **No win/lose/dance state classes exist** — the round-end animations live outside this state machine.
3. **`KirbyCrush` is a knockback/launch state, not a squish.** The squish animation is owned by `KirbyPress` (setter `0x802f4068`, `AC_FLAT_START`). The doc previously implied Crush was the squish state because `EffectorCrush_ApplyToKirby` is the bridge from heavy-machine landings; the bridge name is engine-original and is misleading. Verify in-game before relying on Crush vs. Press for trap visuals.
4. **AC_TOBASARE rescale is gated by both setter velocity-zeroing and tick `vt[+0xE4]` invocation.** See "Empirical results from mod-triggered application" above. The teleport happens iff (a) the per-frame tick at `vt[+0x28]` calls `vt[+0xE4]` (the rescale callback `0x802f3cfc`) AND (b) `kirby+0xA0` is non-zero entering the next tick. Pre-apply velocity zero handles most cases; Crush also needs a post-apply zero because its setter PSVECNormalizes the Vec3 *arg* (which we pass as `&zero`) and writes the resulting NaN back to `kirby+0xA0`. Spin cannot be fixed by apply-time zeroing because its per-frame tick lacks the PSVECScale drag step that the shared tick uses, so velocity accumulates each frame until the rescale fires. Spin is therefore excluded from any deathlink/traplink pool unless paired with continuous per-frame velocity zeroing or an engine-level patch on the gravity store at `0x802fd578`.
5. **`KirbyElec`, `KirbyExplode`, and `KirbyStrike` are silent when invoked from mod code.** Common cause: all three use the shared per-frame tick `0x802f3bd0`, which lacks input-gating logic, AND none of them installs a JObj overlay or uses a distinct animation that reads as a hit. Confuse visibly stuns because it overrides the predicate slots `[+0x10..+0x24]` AND its tick `0x802d1d84` does the gating. Numb is visible because its tick `0x802fd3ac` calls `0x802d1d84` as a sub-step (and so inherits the gating from there), even though its vtable predicate slots are the same KirbyDamage stubs as Elec/Strike/Explode. Freeze visibly stuns via its ice-block JObj overlay. Press visibly stuns via its distinct `AC_FLAT_START` pancake animation. Strike / Explode / Elec hit none of those visibility mechanisms — they're mechanically harmless but useless as a deathlink visual when invoked via the bare wrapper. To make any of them usable, either find and replicate the vanilla effector's extra setup at the call site, or rely on Press / Freeze / Numb / Confuse.

6. **`KirbyCrush` produces only a brief vertical spin animation, not a recognizable hit reaction.** Its setter calls `0x802f53dc` which PSVECNormalizes the Vec3 arg (we pass `&zero` → NaN), and the engine animation that gets selected is `AC_TOBASARE` but the state's per-frame physics keep the visual confined to a vertical axis spin. Mechanically harmless once post-apply velocity-zero overrides the NaN, but visually too generic to be useful as a deathlink/traplink hit; not worth a slot in the pool when Press / Freeze / Confuse give clearer stuns.

7. **`KirbyBurn` cannot be invoked from mod code without a real non-zero float arg — value still unknown.** The wrapper at `0x802d55c0` dereferences `arg2` as a pointer at `0x802d5674` (`lwz r0, 0(r30)`), so passing literal 0 DSI's on null. Even with `&zero`, the setter `0x802f958c` reads `*arg2` as a *float* (`lfs f29, 0(r30)` at `0x802f9890`) and uses it to seed `state.f04+0x24` (init = `*arg2 / 60.0`). The state's per-frame proc at `0x802f3bd0` decrements that field by 6.0 every frame with no branch on the value — termination is gated by some unidentified external check. With `*arg2 = 0.0` the engine effectively freezes and kirby never visibly burns. Vanilla callers at `0x80299dd4` and `0x80321a14` both pass real non-zero floats derived from effector context (`this+0x48`) or per-tick math (`frame*0.277`); no symbol-named `EffectorBurn` exists, so we have no static constant to copy. **Path forward to fix**: live-debug in Dolphin — break at `0x80299e0c`, trigger an in-game burn (lava tile / fire effector), read the float at `*(r5)` at the moment of the bctrl. Excluded from the deathlink pool until that value is pinned down.

# Top Ride Kirby States

Each Top Ride Kirby has a polymorphic state object at `TopRideKirby+0x7C` (`state_handler`). The kirby is "in" a state by virtue of which class instance lives in that slot. State classes derive from `KirbyOperate` and `KirbyNormal`/`KirbyDamage`; transitions are driven by non-virtual methods on the Kirby class itself (vtable at `0x804d2304`). Vanilla bombs and landmines invoke `KirbyExplode` ("tumble") via `EffectorExplode_ApplyToKirby`; vanilla heavy-machine collisions invoke `KirbyCrush` (a knockback launch, same `AC_TOBASARE` animation family as Explode — **not** a squish) via `EffectorCrush_ApplyToKirby`. The squish-flat animation belongs to `KirbyPress`.

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

82 function-pointer entries (`0x000..0x144`, `0x148` bytes), RTTI class name `"Kirby"` (the type-info name string lives at `0x805d9134`). RTTI typeinfo record for the vtable itself: `0x805d913c` (a `__si_class_type_info` — its name pointer at `+0x00` resolves to the `"Kirby"` string).

The 13 invocable state-transition wrappers cluster at byte offsets `0xDC..0x114` (indices 55–69):

| Index | Offset | Wrapper | Target state | Setter | Caller signature |
|------:|-------:|---------|--------------|--------|------------------|
| 55 | 0xDC | `TopRide_KirbyPressMethod` `0x802d54ec` | KirbyPress | `TopRide_KirbyPressSetter` `0x802f4068` | `(this)` |
| 56 | 0xE0 | `TopRide_KirbyBurnMethod` `0x802d55c0` | KirbyBurn | `TopRide_KirbyBurnSetter` `0x802f958c` | `(this, u32, u32*, u32)` |
| 57 | 0xE4 | `TopRide_KirbyFreezeMethod` `0x802d56bc` | KirbyFreeze | `TopRide_KirbyFreezeSetter` `0x802fa16c` | `(this)` |
| 58 | 0xE8 | `TopRide_KirbyCrushMethod` `0x802d5760` | KirbyCrush | `TopRide_KirbyCrushSetter` `0x802f4a48` | `(this, u16, Vec3*)` |
| 59 | 0xEC | `TopRide_KirbyExplodeMethod` `0x802d5834` | KirbyExplode | `TopRide_KirbyExplodeSetter` `0x802f6138` | `(this, u16, Vec3*, u16, u16)` |
| 60 | 0xF0 | `TopRide_KirbyStrikeMethod` `0x802d5900` | KirbyStrike | `TopRide_KirbyStrikeSetter` `0x802f6c28` | `(this, u16, Vec3*, u16, u16)` |
| 61 | 0xF4 | `TopRide_KirbySpinMethod` `0x802d59cc` | KirbySpin | `TopRide_KirbySpinSetter` `0x802f7718` | `(this, u32, Vec3*, u32)` |
| 62 | 0xF8 | `TopRide_KirbySandSpinMethod` `0x802d5aa0` | KirbySandSpin | `TopRide_KirbySandSpinSetter` `0x802f7b28` | `(this, u32, Vec3*, u32)` |
| 64 | 0x100 | `TopRide_KirbyNumbMethod` `0x802d5b74` | KirbyNumb | `TopRide_KirbyNumbSetter` `0x802f7f24` | `(this)` |
| 65 | 0x104 | `TopRide_KirbyElecMethod` `0x802d5be4` | KirbyElec | `TopRide_KirbyElecSetter` `0x802f858c` | `(this, u32)` |
| 66 | 0x108 | `TopRide_KirbyConfuseMethod` `0x802d5c64` | KirbyConfuse | `TopRide_KirbyConfuseSetter` `0x802faa88` | `(this, u32)` |
| 67 | 0x10C | `TopRide_KirbyShortcutMethod` `0x802d5ce4` | KirbyShortcut | `TopRide_KirbyShortcutSetter` `0x802fbaf0` | `(this, u32, u32, u32)` |
| 68 | 0x110 | `TopRide_KirbySpeedDownMethod` `0x802d5da4` | KirbySpeedDown | `TopRide_KirbySpeedDownSetter` `0x802ff98c` | `(this, u32)` |
| 69 | 0x114 | `TopRide_KirbyTransparentMethod` `0x802d5e60` | KirbyTransparent | `TopRide_KirbyTransparentSetter` `0x802f3128` | `(this)` |

Bare-release wrappers (no invincibility / dynamic_cast guard, install state unconditionally) — these install "internal" states normally entered by the engine itself:

| Index | Offset | Wrapper | Target state | Caller signature |
|------:|-------:|---------|--------------|------------------|
| 49 | 0xC4 | `TopRide_KirbyGrindMethod` `0x802d9f2c` | KirbyGrind | `(this)` — also plays effect 0x8002e on `kirby+0x274` |
| 50 | 0xC8 | `TopRide_KirbyNormalMethod` `0x802da0f4` | KirbyNormal | `(this)` — also plays effect 0x8002f on `kirby+0x274` |
| 51 | 0xCC | `TopRide_KirbyDoodlebugMethod` `0x802da150` | KirbyDoodlebug (self) | `(this)` |
| 52 | 0xD0 | `TopRide_KirbyDoodlebugOutMethod` `0x802da1c0` | KirbyDoodlebugOut | `(this, ?, Vec3 *pos, Vec3 *src_pos, u16, u16)` — knockback math (PSVECSubtract pos − src_pos, normalize) |
| 63 | 0xFC | `TopRide_KirbyWhirlpoolMethod` `0x802da2ec` | KirbyWhirlpool | `(this, ?, Vec3 *pos, Vec3 *src_pos, u16, u16)` — same knockback math as DoodlebugOut |
| 69 | 0x114 | `TopRide_KirbyTransparentMethod` `0x802d5e60` | KirbyTransparent | `(this)` — also calls `zz_802d8ac8_(this, 1)` to drop held item |

Index 69 sits in the `0xDC..0x114` cluster but its body is bare-release, so it appears in both tables.

Vtable indices 53 (`0xD4`, `0x802da23c`) and 54 (`0xD8`, `0x802da258`) sit in the same address range but are **not** state-transition wrappers. 53 writes a `Vec3` to `kirby+0x148..+0x150`; 54 sets two flag bytes at `kirby+0x118..+0x119`, zeros `kirby+0xF4`, and stores a computed float at `kirby+0xA4`. They are scalar field setters on the kirby itself, with no effect on `state_handler`.

Notable non-wrapper slots:

| Index | Offset | Address | Symbol | Purpose |
|------:|-------:|---------|--------|---------|
| 47 | 0xBC | `0x802d5590` | `TopRide_KirbyIsInvincible` | Invincibility predicate (delegates to `state_handler->vt[+0x44]`) |
| 48 | 0xC0 | `0x802d9ec0` | `TopRide_KirbyStatePredicate2` | State predicate (delegates to `state_handler->vt[+0x4C]`) |

Other vtable slots (queries, getters, list ops, RTTI helpers) are not relevant to state transitions.

## State Classes

19 derived state classes plus 3 abstract bases. Every state has:

- A **typeinfo** record under `0x805d984x..0x805d9ad0` ("full" RTTI). Some also have a "compact" entry under `0x805d9098..0x805d90f8` used as the dynamic_cast target by Group A wrappers.
- A **state vtable** (different from the Kirby vtable) — read at runtime as `state_handler->vtable[N]()`.
- A **state ID** returned by `state_handler->vt[+0x0C]()` (tracked column below). This is the *runtime* value the get_state_id slot returns. It is **not** the same as the `TopRideKirbyStateId` enum in `topride.h`: the enum assigns `TR_KSTATE_NORMAL = 1`, but **no state's get_state_id ever returns 1** — `KirbyNormal` inherits the same `get_state_id` (`0x802e4a44`, returns 0) as `KirbyDamage`. So the enum value 1 is a nominal label that never appears at runtime; treat "in Normal" as get_state_id == 0 and disambiguate from the abstract base via the vtable pointer.
- A **setter** function that writes the state vtable and constructs the per-state data when transitioning in.

| Class | Compact RTTI | Full typeinfo | State vtable | State ID | get_state_id fn | Wrapper |
|-------|-------------:|--------------:|-------------:|---------:|----------------:|--------:|
| KirbyOperate (base) | `0x805d9098` | — | — (abstract) | — | — | — |
| KirbyNormal (base) | `0x805d90a0` | `0x805d94c8` | `0x804d6f5c` | 0 *(enum 1)* | `0x802e4a44` | 50 |
| KirbyDamage (base) | `0x805d90a8` | `0x805d9840` | `0x804da158` | 0 | `0x802e4a44` | — |
| KirbyPress | `0x805d90b0` | `0x805d98c0` | `0x804da070` | 2 | `0x802fe340` | 55 |
| KirbyCrush | `0x805d90d0` | `0x805d98b0` | `0x804d9ee0` | 3 | `0x802fdfd0` | 58 |
| KirbyExplode | `0x805d90d8` | `0x805d98a8` | `0x804d9dd0` | 4 | `0x802fdc58` | 59 |
| KirbyStrike | `0x805d90e0` | `0x805d98a0` | `0x804d9cbc` | 5 | `0x802fd8e0` | 60 |
| KirbySpin | `0x805d90e8` | `0x805d9890` | `0x804d9a90` | 6 | `0x802fd43c` | 61 |
| KirbySandSpin | — | `0x805d9898` | `0x804d9bac` | 6 | `0x802fd43c` | 62 |
| KirbyNumb | — | `0x805d9888` | `0x804d9980` | 7 | `0x802fd3a4` | 64 |
| KirbyElec | `0x805d90c0` | `0x805d9880` | `0x804d9870` | 8 | `0x802fd194` | 65 |
| KirbyWhirlpool | — | `0x805d9878` | `0x804d9760` | 9 | `0x802fcfb4` | 63 |
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

`TopRide_KirbyGetStateId` is therefore reliable for the damage states but ambiguous for "running normally": to distinguish `KirbyNormal` from the abstract `KirbyDamage` base, compare `state_handler->vtable` against `0x804d6f5c` directly (`TR_KSTATE_VT_NORMAL`).

## State Wrapper Patterns

Wrappers fall into four groups by guard logic.

### Group A — invincibility + dynamic_cast

The standard pattern. Used by **8** wrappers: Press, Burn, Freeze, Crush, Explode, Strike, Spin, SandSpin. (Shortcut and SpeedDown do **not** call the invincibility predicate — see Group A2.)

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

### Group A2 — dynamic_cast + extra guard, no invincibility

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

The setter rewrites the existing state object in place (no allocation). The same pattern works for the other "internal" setters near it (KirbySpeedDown is `0x802ff98c` but is exposed as wrapper 68 — use that instead).

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
| `+0xE4` | AC_TOBASARE rescale callback | `0x802f3cfc` on most damage-derived vtables; Press overrides it with `0x802fe3a0` |

Most state vtables run ~58 slots (~0xE8 bytes) and inherit most slots from the parent class's vtable (KirbyDamage / KirbyNormal); only the state-specific slots (animation, transitions to next state, hit response) are overridden per-class. **Vtable length is not uniform**, though — e.g. `KirbyConfuse`'s vtable (`0x804d9434`) is shorter (~0xDC bytes); the word at its `+0xE4` is non-pointer data (it has no AC_TOBASARE rescale slot, consistent with its tick never calling `vt[+0xE4]`). Don't blindly index `vt[+0xE4]` on an arbitrary state — confirm the class actually has that slot.

## Vanilla Effector Triggers

State-applicator effectors live in `0x802e6...` and bridge a vanilla game event (bomb explosion, machine collision) to the kirby state machine.

| Effector | Address | Invokes | Notes |
|----------|---------|---------|-------|
| `EffectorExplode_ApplyToKirby` | `0x802e6898` | KirbyExplodeMethod | Bombs, landmines, bomb-block traps. Computes knockback Vec3 from `(kirby_pos - explosion_pos)` normalized × `mass / inv_distance`. Reads damage/hit-frame u16s from the EffectorExplode instance at `+0x42`/`+0x44`/`+0x46`. |
| `EffectorCrush_ApplyToKirby` | `0x802e6630` | KirbyCrushMethod | Heavy-machine collision (Wagon Star landing on a kirby) — applies a knockback launch (`AC_TOBASARE`), not a squish. Function spans `0x802e6630..~0x802e6764`; the single `vt[+0xE8]` (Crush) call site is the `bctrl` at `0x802e6738`. Args `(kirby, 0, &normalized_direction)`. |

The other state-trigger effectors (Burn for fire blocks, Freeze for ice, Elec for electric items, etc.) are inlined into per-item callbacks rather than living as standalone functions — they call the Kirby vtable wrapper directly with appropriate args.

## Invocation From Mod Code

The standard recipe (matches the `TopRide_Kirby*` helpers in `topride.h`):

```c
TopRideKirbyMgr *mgr = *stc_topride_kirbymgr;
if (!mgr || mgr->round_state != 2)
    return;                                  // race must be active

for (int i = 0; i < 4; i++)
{
    TopRideKirby *k = mgr->kirbys[i];
    if (!k) continue;
    if (TopRide_GetPlayerKind(k->player_slot) != TR_PKIND_HMN) continue;
    TopRide_KirbyPress(k);                   // or any other state helper
}
```

The `round_state == 2` (race active) gate is mandatory: before that, `state_handler`'s vtable is not fully wired and `state_handler->vt[+8](-1)` (release-current-state) will likely crash.

Most wrappers accept extra args (knockback Vec3, hit-frame u16s, damage variant codes). Passing zeros / a zero Vec3 produces a static stun: animation plays in place, no knockback impulse, default hit duration. Note that several args are dereferenced before use, so a literal `0` faults where a pointer to a stack zero does not — `KirbyBurn`'s arg2 (`lwz r0, 0(r30)` at `0x802d5674`) and `KirbySpin`'s Vec3 (fed to `PSVECMagnitude` at `0x802f7998`) both need a real address.

### Velocity handling and visibility

Two independent mechanisms decide what a mod-triggered state actually looks like.

**AC_TOBASARE rescale.** The rescale callback at state vtable `[+0xE4]` = `0x802f3cfc` reads `kirby+0xA0` (the inline ChargeComponent's velocity Vec3) via `PSVECMagnitude`, branches `ble` to the epilogue when `magnitude * scale ≤ threshold`, and otherwise normalizes-and-rescales the velocity in place. A kirby with non-zero entry velocity therefore has its launch direction locked to its running direction and the magnitude re-applied every frame — a visible teleport. Zero entry velocity takes the early-exit branch and no transform happens. The teleport reduces to **(the tick at `vt[+0x28]` calls `vt[+0xE4]`) AND (the setter did not zero velocity)**. The presence of the `[+0xE4]` slot alone is not the discriminator.

**Input gating.** A damage state visibly stuns only if its per-frame tick at `vt[+0x28]` is *not* the shared `0x802f3bd0` (which has no input-gating logic), OR its setter installs a JObj overlay (Freeze's ice block), OR it uses a distinct animation that reads as a hit without input lock (Press's `AC_FLAT_START` pancake). The animation string is *not* the discriminator: `AC_SIBIRE` is shared by the visible Numb and the silent Elec, and `AC_TOBASARE` by Strike/Explode/Crush.

`mods/archipelago/src/deathlink.c` zeroes `kirby->charge.velocity` immediately **before and after** the wrapper call — the pre-zero pre-empts setters that scale entry velocity, the post-zero overrides setters that write a value of their own (Crush's helper `0x802f53dc` PSVECNormalizes the `&zero` Vec3 *arg*, producing NaN regardless of what `kirby+0xA0` held). Results in that configuration:

| State | ID | Tick fn at `vt[+0x28]` | Calls `vt[+0xE4]`? | Setter's own velocity write | Outcome | In pool |
|-------|---:|------------------------|--------------------|-----------------------------|---------|---------|
| Press | 2 | `0x802f3bd0` (shared) | yes | zeroes it (`0x802f4068`, `stfs f0, 32/36/40(r3)`) | Visible pancake squish (`AC_FLAT_START`) | yes |
| Freeze | 11 | `0x802fc994` (unique) | no | none (tick doesn't read velocity) | Clean ice-block stun | yes |
| Numb | 7 | `0x802fd3ac` (unique — calls `0x802d1d84`, then `vt[+0xE4]`) | yes | none | Visible paralysis pose; the `0x802d1d84` sub-call supplies the input gating | yes |
| Confuse | 12 | `0x802d1d84` (unique) | no | none | Clean panic-spin stun | yes |
| Elec | 8 | `0x802f3bd0` (shared) | yes | zeroes it (`0x802f858c`, same 3-store pattern) | **Silent** — state lands, kirby keeps full control | no |
| Strike | 5 | `0x802f3bd0` (shared) | yes | scales entry velocity ×~0.31 (PSVECScale) | **Silent** | no |
| Explode | 4 | `0x802f3bd0` (shared) | yes | scales entry velocity ×~0.5 (PSVECScale by mass) | **Silent** | no |
| Crush | 3 | `0x802f3bd0` (shared) | yes | writes `(NaN, ~1.69, NaN)` via `0x802f53dc` | Brief vertical spin, not a readable hit reaction | no |
| Spin | 6 | `0x802fd49c` (unique) | yes (via shared helper) | none | **Still ejects the kirby** — the unique tick does `velocity.y -= gravity` with no PSVECScale drag step, so velocity re-accumulates every frame | no |

The shipped deathlink pool is therefore **Press, Freeze, Numb, Confuse**.

**Why Elec is silent** is structural, not a missing runtime precondition. `state_after` reads 8 and `vel_after` is zero, but the kirby keeps input control and no paralysis pose. The KirbyElec state vtable inherits its `[+0x10..+0x24]` predicate slots from KirbyDamage, and those slots are trivial stubs (`li r3, 1; blr` at `0x802da57c..0x802da59c` and `li r3, 0; blr` at `0x802d4b90`). Confuse, which *does* visibly stun, **overrides** those same slots with real implementations (`0x802d4b98..0x802d4bb8` and `0x802d1d84`). The engine queries those predicates each frame and only gates input when the state class supplies its own. Vanilla electric items must do additional setup at the call site — a separate visual/input-lock effect, or a kirby-level flag one of the predicates reads — that a zero-arg wrapper call does not.

**Overlay-JObj asserts in the Freeze / Confuse setters are not load-bearing in normal play.** Both setters dereference `kirby+0x80+0x534` (Freeze ice block) and `kirby+0x80+0x544` (Confuse marks) and `__assert` on null. Those overlay JObjs are populated whenever `round_state == 2`, so the path is safe in practice; a defensive null-guard is still cheap insurance.

## Per-State Animation IDs

Animations are written by setters as a string-pointer at `kirby->state_anim_ptr` (struct offset `+0x4F4` from the inner state-data block, i.e. `state_handler[1] + 0x4F4`). The string is later resolved against the per-character anim table to a frame range. The strings each setter writes:

| Setter | State | Animation string | Notes |
|--------|-------|------------------|-------|
| `0x802df844` | KirbyNormal | `AC_RUN_LOOP` | Default running anim. |
| `0x802f4068` | KirbyPress | `AC_FLAT_START` | Pancake/squish. |
| `0x802f4a48` | KirbyCrush | `AC_TOBASARE` | "Blown away" — a knockback launch, not a squish. The squish animation is KirbyPress. |
| `0x802f6138` | KirbyExplode | `AC_TOBASARE` | Same launch anim as Crush; the state differs in duration / state-machine transitions. |
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

## Known Limitations

1. **`KirbySandSpin` (idx 62) is a `KirbySpin` subclass.** Its dynamic_cast guard tests `KirbySpin` (the parent), so it correctly blocks re-entry from either Spin variant. The "pure" KirbySpin entry (idx 61) installs the parent class's vtable; KirbySandSpin's setter calls KirbySpin's setter as super-init then overrides the vtable.
2. **No win/lose/dance state classes exist** — the round-end animations live outside this state machine.
3. **`KirbySpin` cannot be made safe by apply-time velocity zeroing.** Its per-frame tick lacks the PSVECScale drag step the shared tick uses, so velocity accumulates each frame until the rescale fires. Excluding it from a deathlink/traplink pool is the only cheap option; using it would need continuous per-frame velocity zeroing or an engine-level patch on the gravity store at `0x802fd578`.
4. **`KirbyElec`, `KirbyExplode`, and `KirbyStrike` are mechanically harmless but invisible when invoked from mod code.** All three use the shared tick `0x802f3bd0` and none installs a JObj overlay or a distinct pose. Reproducing a vanilla-looking hit would mean replicating the vanilla effector's extra call-site setup; otherwise use Press / Freeze / Numb / Confuse.
5. **`KirbyCrush` produces only a brief vertical spin.** Its setter's helper `0x802f53dc` PSVECNormalizes the Vec3 arg, so a zero arg yields NaN; even with a post-apply velocity zero the resulting visual is a generic vertical axis spin rather than a readable hit reaction.
6. **`KirbyBurn` needs a real non-zero float duration seed.** The wrapper `0x802d55c0` dereferences arg2 as a pointer at `0x802d5674`, and the setter `0x802f958c` reads `*arg2` as a *float* (`lfs f29, 0(r30)` at `0x802f9890`) to seed `state.f04+0x24` (init = `*arg2 / 60.0`). The state's per-frame proc `0x802f3bd0` decrements that field by 6.0 per frame, so `*arg2 = 0.0` never terminates and the kirby never visibly burns. Vanilla callers at `0x80299dd4` and `0x80321a14` pass values derived from effector context (`this+0x48`) or per-tick math (`frame*0.277`); there is no static constant to copy, so Burn stays out of the deathlink pool.

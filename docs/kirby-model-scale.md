# Kirby Model Scale (Big / Small Kirby)

Big Kirby (`AP_ITEM_BIG_KIRBY`, 972) and Small Kirby (`AP_ITEM_SMALL_KIRBY`, 973)
are all-mode cosmetic filler items. On receipt they scale every human player's
Kirby model up (×1.5) or down (×0.5), easing in over ~1 second rather than
snapping. The effect is live in City Trial, Air Ride, and Top Ride, and lasts
until the next scene change.

Implementation: `mods/archipelago/src/kirby_scale.c`.

## How scaling is applied

Both player systems expose a per-object `model_scale` float that the engine
multiplies into the model's transform **every frame**. Writing that field is the
entire mechanism — no JObj poking required — and the change naturally persists
until the object is recreated (scene change), at which point it resets to 1.0.

### City Trial / Air Ride — `RiderData.model_scale` (+0x348)

Initialized to 1.0. Consumed by `Rider_ApplyModelMatrix` (0x80190848):

```c
gmLanMenu_Scale3DObject(base_scale(rider+0x2c8) * model_scale(rider+0x348),
                        rider_model_jobj, forward(+0x324), up(+0x330), pos(+0x300));
```

`gmLanMenu_Scale3DObject` (0x80054...) bakes `scale × orientation` into the
model JObj's matrix (JOBJ+0x44) and marks it dirty. `Rider_ApplyModelMatrix` is
called from `Rider_ModelMatrixThink` (0x8018f79c), which `Rider_Create`
registers as GObj proc priority 6 — i.e. it runs **every frame** (it also
refreshes the hand-bone world position for held items). So the rider model
matrix, including `model_scale`, is rebuilt each frame.

### Top Ride — `TopRideKirby.model_scale` (+0x524)

Initialized to 1.0 (`TopRide_KirbyChargeInit`). The model's root JObj is
`TopRideKirby+0x4E0` (`model_jobj`). Consumed every frame by
`TopRide_KirbyModelThink` (0x802e26dc):

```c
float s = model_scale(kirby+0x524) * dataTable[0x20f8];
JOBJ *m = child_of(model_jobj(kirby+0x4E0));
m->scale = { s, s, s };          // JOBJ +0x2c/+0x30/+0x34
HSD_JObjSetMtxDirtySub(m);
```

Accessed from mod code via `TopRide_KirbyModelScalePtr` (see `topride.h`).

Unlike City Trial / Air Ride — where the rider (`RiderData`) and machine
(`MachineData`) are separate objects and `model_scale` touches only the rider —
Top Ride merges Kirby and his star into a **single JObj tree** under the scaled
root (`RdKirby.dat` + a `VcStar*.dat`, combined at runtime). `model_scale` scales
that shared root, so Big / Small Kirby grows / shrinks Kirby **and** the star
together. This is intentional: scaling Kirby alone would require counter-scaling
the star's attach joint inside the shared skeleton (which differs by star type),
and the merged "big Kirby on a big star" look is acceptable for cosmetic filler.

## Behavior

- **Multiplicative, clamped.** Each Big ×1.5, each Small ×0.5, applied to the
  current multiplier and clamped to `[0.5, 2.0]` so the model never grows large
  enough to disturb the camera or collision feel. Filler is draw-with-
  replacement, so a seed may contain many copies — clamping bounds the stack.
- **Applies to all human players** uniformly, matching the rest of the receive
  path (All Up, box grants, etc.).
- **Eases in over ~1 second.** Rather than snapping `model_scale` to the new
  target, the per-frame applier interpolates the displayed multiplier from the
  size currently on screen to the target with a smoothstep over
  `KIRBY_SCALE_ANIM_FRAMES` (60 frames ≈ 1 s at 60 fps). A second item received
  mid-ease just retargets and restarts from the current size. Once the ease
  settles the value sits exactly on the target.
- **Resets to 1.0 on every scene change** (`KirbyScale_OnSceneChange`), since the
  Kirby objects are recreated at `model_scale` 1.0 there. The reset snaps (no
  ease — the old models are gone). This is what makes the effect last only for
  the current scene.
- **Queued until the round is ready.** If received outside a scalable scene
  (menus, CSS), or before the round is actually underway, `KirbyScale_HandleItem`
  returns `AP_ITEM_RETRY` so the item waits in the queue and applies once the
  player is in live gameplay — matching the gate every other received item uses.
  "Underway" is mode-specific:
  - **City Trial / Air Ride** — the intro flyover / countdown must finish
    (`Gm_GetIntroState() == GMINTRO_END`).
  - **Top Ride** — has no intro, so `Gm_GetIntroState` is always `GMINTRO_END`;
    instead `InScalableScene` requires `TopRideKirbyMgr.round_state == 2` (race
    active, post-countdown). Without this the item applies the instant the mode
    loads and the ~1 s ease plays out during the countdown, off-screen, so the
    Kirby looks already-grown at "GO" — an apparent instant snap. `round_state`
    is the master per-frame physics gate and reaches 2 in solo Time Attack /
    Free Run as well.
- A per-frame applier (`KirbyScale_On3DLoadEnd` / `KirbyScale_OnTopRideLoadEnd`)
  re-writes the field each frame so the scale also survives mid-scene model
  recreation (respawns). It is a no-op until a Big / Small Kirby is received
  this scene (the target stays neutral, 1.0), so vanilla behavior is untouched.

## In-game surfaces

Besides AP item receipt, the two scale items can be triggered locally:

- **Debug menu** — `Give Items → Upgrades → Big Kirby / Small Kirby`
  (`mods/archipelago_debug/src/debug_menu.c`), which queues the AP item id via
  `ArchipelagoAPI.QueueItem`.
- **EnergyLink shop** — `Cosmetic → Big Kirby / Small Kirby`, 500 MJ each
  (`energylink_spend.c`). Both route through the normal receive queue, so the
  same scene-gating / RETRY behavior applies.

## Symbols

| Symbol | Address | Notes |
|--------|---------|-------|
| `Rider_ApplyModelMatrix` | 0x80190848 | Builds rider model matrix from `base × model_scale` |
| `Rider_ModelMatrixThink` | 0x8018f79c | GObj proc pri 6 (per-frame); calls the above + hand-bone update |
| `TopRide_KirbyModelThink` | 0x802e26dc | Per-frame; applies `model_scale × table` to the model JObj |
| `gmLanMenu_Scale3DObject` | 0x80054... | Generic helper: writes `scale × orientation` into a JObj matrix |

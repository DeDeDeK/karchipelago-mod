# Event Source Drops

## Overview

`grBoxGeneInfo->item_desc->event_source_drop[]` is the per-stage table that drives item drops from non-box sources (Tac, meteor, secret chamber, UFO, etc.). Each entry pairs an `ItemKind` with six `u16` chance fields — one per drop source. The array is loaded from the stage data file (e.g. `GrCity1.dat`) at scene init.

Box drops are governed by a separate table (`grBoxGeneObj`); see `gate-boxes.md` and `item_spawn_filter.c` for the box pool side.

## Struct (from `game.h`)

```c
struct {
    int it_kind;        // 0x0
    u16 chance_dyna;         // 0x4
    u16 chance_tac;          // 0x6
    u16 chance_meteor;       // 0x8
    u16 chance_destructible; // 0xA
    u16 chance_chamber;      // 0xC
    u16 chance_ufo;          // 0xE
} *event_source_drop;
int event_source_drop_num;
```

Filtering note: locked items get all six chance fields zeroed by the per-gate `*_FilterEventDropTables` functions (see `item_spawn_filter.c`). The array is never compacted because other code references entries by index.

## Field-to-Source Mapping

| Field | Drop source |
|---|---|
| `chance_dyna` | Dyna Blade hits/exits (patches-only pool; may also cover other untested rare sources) |
| `chance_tac` | Tac (cat enemy) |
| `chance_meteor` | Meteor explosion |
| `chance_destructible` | Generic destructible structures: **star pole, event pillar, volcano walls** |
| `chance_chamber` | Secret chamber |
| `chance_ufo` | UFO |

### How this was confirmed

With the debug menu set to "everything locked except hot dog" — leaving `FOODHOTDOG` as the only entry with nonzero chances (`tac=2`, `destructible=2`, `chamber=2`; `dyna=0`, `meteor=0`, `ufo=0`) — the following tests were run:

| Source broken | Hot dog dropped? | Conclusion |
|---|---|---|
| Star pole | Yes | → on a field where FOODHOTDOG ≠ 0; by elimination, `chance_destructible` |
| Event pillar | Yes | → also `chance_destructible` (shared pool with star pole) |
| Volcano walls | Yes | → also `chance_destructible` (shared pool) |
| Dyna Blade hit/exit | No | → on a field where FOODHOTDOG = 0; by elimination, `chance_dyna` |

Of the three sources, only Dyna Blade keys off `chance_dyna`; the volcano walls and star pole are the destructible-structures pool (`chance_destructible`).

## Drop Pipeline

```
                 ┌─ destruction callback (obj+0x100)
                 │  for rock/house, or direct call for coral
                 ▼
   GrYakuBreakRock_DropItems   ──┐  (0x8010203c)
   GrYakuBreakHouse_DropItems  ──┼──▶  City_SpawnMiscItems(desc)   (0x80104db0)
   GrYakuBreakCoral_DropItems  ──┘            │   (dispatches on desc shape flag at +0x20)
                                              ▼
                       City_SpawnMiscItemsRing  /  shootPowerUps?
                          (0x80104e10)            (0x801058c0)
                                              │   (descriptor selects shape)
                                              ▼
                              CityItem_GetEventItem(desc.drop_source)   (0x80254114)
                                              │
                                              ▼
                              _CityItem_GetEventItem(source_enum)   (0x800ebe44)
                                              │
                                              ▼
                              event_source_drop[] weighted random pick
```

The per-instance descriptor passed to `City_SpawnMiscItems` (0x80104db0) carries a `drop_source` field at `+0x1c`. If `drop_source != -1`, that value is passed to `CityItem_GetEventItem` (a thin wrapper that tail-calls `_CityItem_GetEventItem`) as the source enum. Otherwise the emitter falls back to `CityEvent_GetRandomItem` (0x80252f28, current event's pool).

## Source Enum

`_CityItem_GetEventItem` (0x800ebe44) accepts an integer 0..12 dispatched through a 13-entry jump table at `0x804a5290`. Inputs 0..12 fan in to one of six chance columns (or no-match → returns -1):

| Input | Column | Meaning | Caller(s) |
|---|---|---|---|
| 0 | `chance_dyna` | Dyna Blade | `zz_8021db44_` (Dyna Blade actor) |
| 1 | `chance_tac` | Tac | `zz_8021c8ec_` (Tac actor) |
| 2 | `chance_meteor` | Meteor | `zz_8021efd8_` (Meteor actor) |
| **3** | **`chance_destructible`** | **yaku-break objects** | only via `City_SpawnMiscItems` (see below) |
| 4-8 | — | unmapped, returns -1 | — |
| 9 | `chance_chamber` | Secret Chamber | `spawnSecretChamberItems` |
| 10-11 | — | unmapped, returns -1 | — |
| 12 | `chance_ufo` | UFO | `spawnUFOItems` + 4 unnamed UFO event handlers |

The unmapped slots (4-8, 10-11) suggest the enum was originally sized for 13 distinct sources and some were removed or never wired up.

Note that `chance_destructible` (input 3) is **never passed as a literal** by any caller. It is reached exclusively through the per-instance descriptor's `drop_source` field, populated from stage data. This is why the same drop column is shared by every yaku-break object that drops items.

## Yaku-Break Object Families

Destructible objects in City Trial are implemented in a family of `gryakubreak*.c` source files. Each registers a destruction callback at `obj+0x100` (rock/house pattern) or calls a per-file drop helper directly (coral pattern). All three drop-capable families share `chance_destructible`.

| Source file | Drop helper | Drops? | Examples |
|---|---|---|---|
| `gryakubreakrock.c` | `GrYakuBreakRock_DropItems` (0x8010203c) | yes | volcano walls, **event pillars** (the `event_pillar` event spawns rock-type objects — see `event_pillar` at 0x80111604 calling `zz_80101a00_` which assigns the rock destroy callback) |
| `gryakubreakhouse.c` | `GrYakuBreakHouse_DropItems` (0x80102794) | yes | houses |
| `gryakubreakcoral.c` | `GrYakuBreakCoral_DropItems` (0x801040fc), `hitBigStar` (0x80103eb8) | yes | "BigStar" — the **star pole** structure (despite the `coral` filename, in-game these are the tall poles with stars on top) |
| `gryakubreakicicle.c` | — | no | icicles (calls `destroyBigStar` for the breakable behavior, but no drop call) |
| `gryakuanimfloor.c` | — | no | animated floor |
| `gryakubreakfloor.c` | — | no | breakable floor |
| `gryakubreakfan.c` | — | no | fan |
| `gryakubreakcommon.c` | — | n/a | shared helpers (range checks, etc.) |

Both `City_SpawnMiscItemsRing` (0x80104e10, the omnidirectional/ring emitter) and `shootPowerUps?` (0x801058c0, the directed-cone variant — `?` is the map's name, the trailing punctuation reflects an unconfirmed signature) read `drop_source` from `desc[7]` (offset 0x1c). The choice between them is made by `City_SpawnMiscItems` (0x80104db0) based on a shape flag at `desc[8]` (`+0x20`): value `1` → `shootPowerUps?`, value `0` (or any value <1) → `City_SpawnMiscItemsRing`; values >1 hit an assert.

### Per-Instance Drop Gate

Each drop-capable family gates the spawn on a NULL check of an optional "drop descriptor pointer" inside the per-instance param block. If NULL, no items spawn — the destruction proceeds without drops. The offset varies by family:

| Family | Field | Source line |
|---|---|---|
| rock | `param[0x24]` | `if (param->[0x24] != NULL) City_SpawnMiscItems(..., param->[0x24], ...)` |
| coral | `param[0x28]` | same pattern |
| house | `param[0x30]` | same pattern |

This is why two instances of the same yaku-break kind can behave differently: the stage data decides per-placement whether to attach a drop descriptor. In CT, the **star pole** instances (handled by `gryakubreakcoral.c`'s "BigStar" code) carry a non-NULL drop descriptor with `drop_source = 3`, so they pull from `chance_destructible`. Coral-shaped instances (if any are placed in the shipped stages) leave the pointer NULL and silently skip the drop call — same code path, different per-instance config. This explains the apparent contradiction "the file is named `coral` but coral never drops in-game": the file's drop code only runs for instances whose stage data wires up the drop descriptor, and in shipped CT stages that's the star pole.

## Enumerated Table — City Trial (`GrCity1.dat`)

Captured via runtime dump in `FilterAllSpawnTables`. `num=60` entries; only nonzero rows shown. All-zero rows in the table: every `*DOWN` patch, SPEEDMAX, SPEEDMIN, OFFENSEMAX, DEFENSEMAX, CHARGENONE, CANDY, every COPY* not listed, and every FAKE patch. Indices 0-2 (`BOX*`) and 55-60 (Hydra/Dragoon pieces) are absent from the table entirely.

| Item | dyna | tac | meteor | destructible | chamber | ufo |
|---|---:|---:|---:|---:|---:|---:|
| ACCEL | 2 | 4 | 5 | 20 | 6 | 20 |
| TOPSPEED | 2 | 4 | 5 | 20 | 6 | 20 |
| OFFENSE | 2 | 4 | 5 | 20 | 6 | 20 |
| DEFENSE | 2 | 4 | 5 | 20 | 6 | 20 |
| TURN | 2 | 4 | 5 | 10 | 3 | 10 |
| GLIDE | 8 | 4 | 5 | 10 | 3 | 10 |
| CHARGE | 2 | 4 | 5 | 20 | 6 | 20 |
| WEIGHT | 2 | 4 | 5 | 20 | 6 | 20 |
| HP | 2 | 4 | 5 | 10 | 6 | 20 |
| ALLUP | 0 | 2 | 2 | 1 | 1 | 10 |
| CHARGEMAX | 0 | 0 | 0 | 0 | 0 | 5 |
| COPYBOMB | 0 | 0 | 0 | 10 | 0 | 0 |
| COPYSLEEP | 0 | 2 | 0 | 5 | 0 | 0 |
| COPYMIC | 0 | 0 | 0 | 10 | 0 | 0 |
| FOODMAXIMTOMATO | 0 | 2 | 0 | 2 | 2 | 0 |
| FOODENERGYDRINK | 0 | 2 | 0 | 2 | 0 | 0 |
| FOODICECREAM | 0 | 2 | 0 | 2 | 0 | 0 |
| FOODRICEBALL | 0 | 10 | 0 | 4 | 4 | 0 |
| FOODCHICKEN | 0 | 2 | 0 | 2 | 0 | 0 |
| FOODCURRY | 0 | 2 | 0 | 2 | 0 | 0 |
| FOODRAMEN | 0 | 2 | 0 | 2 | 4 | 0 |
| FOODOMELET | 0 | 2 | 0 | 2 | 0 | 0 |
| FOODHAMBURGER | 0 | 2 | 0 | 4 | 0 | 0 |
| FOODSUSHI | 0 | 5 | 0 | 2 | 4 | 0 |
| FOODHOTDOG | 0 | 2 | 0 | 2 | 2 | 0 |
| FOODAPPLE | 0 | 2 | 0 | 4 | 4 | 0 |
| FIREWORKS | 0 | 0 | 0 | 2 | 3 | 0 |
| PANICSPIN | 0 | 0 | 0 | 2 | 0 | 0 |
| SENSORBOMB | 0 | 0 | 0 | 2 | 0 | 0 |
| GORDO | 0 | 0 | 0 | 2 | 0 | 0 |

### Observations

- **`chance_dyna` is patches-only.** Only ACCEL through HP carry nonzero weights, with GLIDE getting a 4× boost (8 vs. 2). Consistent with Dyna Blade only dropping patches, and explains why GLIDE feels disproportionately common from Dyna Blade interactions.
- **`chance_meteor` is also patches-only**, with GLIDE again at 5 like the others (no boost).
- **`chance_ufo` is patches + ALLUP + CHARGEMAX.** Notably the *only* source for `ALLUP` (weight 10) and `CHARGEMAX` (weight 5) outside of mid-low odds elsewhere. UFO is the "big stat boost" source.
- **`chance_destructible` is the broadest pool**: patches, three copy abilities (Bomb, Sleep, Mic), most foods, all three traps (Fireworks, PanicSpin, SensorBomb), and Gordo. Consistent with it being the generic "destructible structure" pool shared across star pole, event pillar, and volcano walls.
- **`chance_tac` skews toward food.** Tac mostly drops food, with patches at modest weight and Sleep as the only copy ability.
- **`chance_chamber` is patches + a few foods + Fireworks.** Narrower than pilar.
- **No "down" patches drop from any event source.** They presumably only come from boxes.
- **No fake patches drop from any event source.** Same — box-only.

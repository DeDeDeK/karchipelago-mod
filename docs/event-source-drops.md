# Event Source Drops

`grBoxGeneInfo->item_desc->event_source_drop[]` is the per-stage table that drives item drops from **non-box** sources - Tac, meteor, Dyna Blade, destructible structures, secret chamber, UFO. It is loaded from the stage data file (e.g. `GrCity1.dat`) at scene init; `grBoxGeneInfo` itself lives at `*stc_grBoxGeneInfo` (r13+0x610). Box drops come from a separate table, `grBoxGeneObj`.

Each row is 0x10 bytes: an `ItemKind` followed by six `u16` weight columns, one per drop source (declared inline in `grBoxGeneInfo` in `game.h`). `event_source_drop_num` at `item_desc+0x1c` is the row count.

| Field | Drop source |
|---|---|
| `chance_dyna` | Dyna Blade hits/exits |
| `chance_tac` | Tac (cat enemy) |
| `chance_meteor` | Meteor explosion |
| `chance_destructible` | Generic destructible structures: star pole, event pillar, volcano walls, houses |
| `chance_chamber` | Secret chamber |
| `chance_ufo` | UFO |

Star pole, event pillar, volcano walls and houses all share the destructible pool; only Dyna Blade keys off `chance_dyna`.

**The array is indexed positionally by other code, so rows must never be reordered or compacted.** Gating a kind off means zeroing all six of its columns in place - that is what `GatePatches_FilterEventDropTables` (`gate_patches.c`), `GateAbilities_FilterEventDropTables` (`gate_abilities.c`) and `GateItems_FilterEventDropTables` (`gate_items.c`) do, all driven from `item_spawn_filter.c`. `custom_items` adds rows by copying the stage's rows into a static array, appending after them, and repointing `item_desc->event_source_drop`/`_num` at the copy (`item_registry.c`); the per-event re-bias overwrites that pointer, so it is re-applied by `CustomItemRegistry_ReinjectPools`.

## Drop Pipeline

Rock and house breakables call their drop helper from the destruction callback at `obj+0x100`; coral calls it directly. All three - `GrYakuBreakRock_DropItems` (0x8010203c), `GrYakuBreakHouse_DropItems` (0x80102794), `GrYakuBreakCoral_DropItems` (0x801040fc) - funnel into `City_SpawnMiscItems` (0x80104db0) with a per-instance drop descriptor.

`City_SpawnMiscItems` picks the emitter from a shape flag at `desc[8]` (`+0x20`): value `1` -> `shootPowerUps?` (directed cone, 0x801058c0), value `0` or lower -> `City_SpawnMiscItemsRing` (omnidirectional, 0x80104e10). Values > 1 hit an assert. (The trailing `?` is part of the map name, marking an unconfirmed signature.)

Both emitters read `drop_source` from `desc[7]` (`+0x1c`). If it is not -1 they pass it to `CityItem_GetEventItem` (0x80254114, a thin wrapper that tail-calls `_CityItem_GetEventItem` at 0x800ebe44), which does the weighted random pick over the source's column in `event_source_drop[]`. If it is -1 the emitter falls back to `CityEvent_GetRandomItem` (0x80252f28), the current event's own pool.

## Source Enum

`_CityItem_GetEventItem` accepts an integer 0..12 dispatched through a 13-entry jump table at `0x804a5290`. The named values are the `EventDropSource` enum in `game.h`; inputs fan in to one of six chance columns, and 4-8, 10 and 11 fall to a no-match arm that returns -1.

| Input | Column | Caller(s) |
|---|---|---|
| 0 | `chance_dyna` | `DynaBlade_ThrowItems` (0x8021db44) |
| 1 | `chance_tac` | `Tac_ScatterItems` (0x8021c8ec) |
| 2 | `chance_meteor` | `zz_8021efd8_` (meteor actor) |
| **3** | **`chance_destructible`** | only via `City_SpawnMiscItems` |
| 9 | `chance_chamber` | `spawnSecretChamberItems` (0x8010a998) |
| 12 | `chance_ufo` | `spawnUFOItems` (0x8010be88) + 4 unnamed UFO event handlers |

`chance_destructible` (input 3) is **never passed as a literal** by any caller. It is reached exclusively through the per-instance descriptor's `drop_source` field, populated from stage data - which is why one drop column is shared by every yaku-break object that drops items.

## Destructible Sources

Destructible objects in City Trial are a family of `gryakubreak*.c` source files. Only three emit items, and all three route through `chance_destructible`:

| Source file | Drop helper | Examples |
|---|---|---|
| `gryakubreakrock.c` | `GrYakuBreakRock_DropItems` (0x8010203c) | volcano walls, **event pillars** - the `event_pillar` event (0x80111604) calls `zz_80101a00_`, which assigns the rock destroy callback |
| `gryakubreakhouse.c` | `GrYakuBreakHouse_DropItems` (0x80102794) | houses |
| `gryakubreakcoral.c` | `GrYakuBreakCoral_DropItems` (0x801040fc), `hitBigStar` (0x80103eb8) | "BigStar" - the **star pole** structure. Despite the `coral` filename, in shipped City Trial these are the tall poles with stars on top. |

The other families (`gryakubreakicicle.c`, `gryakuanimfloor.c`, `gryakubreakfloor.c`, `gryakubreakfan.c`, `gryakubreakcommon.c`) have no drop call at all.

Each drop-capable family gates the spawn on a NULL check of an optional drop-descriptor pointer inside its per-instance param block: `param[0x24]` for rock, `param[0x28]` for coral, `param[0x30]` for house. If it is NULL the destruction proceeds with no drops. So two instances of the same yaku-break kind behave differently purely by stage data: in City Trial the star pole instances carry a non-NULL descriptor with `drop_source = 3`, while coral-shaped instances (if any are placed) leave the pointer NULL and silently skip the drop call.

## Enumerated Table - City Trial (`GrCity1.dat`)

`event_source_drop_num = 60`; only nonzero rows are shown. All-zero rows: every `*DOWN` patch, SPEEDMAX, SPEEDMIN, OFFENSEMAX, DEFENSEMAX, CHARGENONE, CANDY, every COPY* not listed, and every FAKE patch. Indices 0-2 (`BOX*`) and 55-60 (Hydra/Dragoon pieces) are absent from the table entirely.

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

Shape of the pools:

- `chance_dyna` and `chance_meteor` are patches-only. Dyna Blade weights GLIDE 4x higher than the other patches (8 vs 2); meteor weights every patch the same.
- `chance_ufo` is patches + ALLUP + CHARGEMAX. It is the only source with meaningful weight for ALLUP (10) and the only source at all for CHARGEMAX (5) - the "big stat boost" source.
- `chance_destructible` is the broadest pool: patches, three copy abilities (Bomb, Sleep, Mic), most foods, all three traps (Fireworks, PanicSpin, SensorBomb), and Gordo.
- `chance_tac` skews toward food, with patches at modest weight and Sleep as the only copy ability.
- `chance_chamber` is patches + a few foods + Fireworks - narrower than destructible.
- No "down" patches and no fake patches drop from any event source. Both are box-only.

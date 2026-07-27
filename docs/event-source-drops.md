# Event Source Drops

`grBoxGeneInfo->item_desc->event_source_drop[]` is the per-stage table that drives item drops from **non-box** sources - Tac, meteor, Dyna Blade, destructible structures, secret chamber, UFO. Each entry pairs an `ItemKind` with six `u16` chance columns, one per drop source. The array is loaded from the stage data file (e.g. `GrCity1.dat`) at scene init. Box drops are governed by a separate table, `grBoxGeneObj`.

## Struct

From `game.h` (`grBoxGeneInfo` is at `*stc_grBoxGeneInfo`, r13+0x610; the sub-struct sits at `item_desc+0x18`):

```c
struct {
    int it_kind;             // 0x0
    u16 chance_dyna;         // 0x4
    u16 chance_tac;          // 0x6
    u16 chance_meteor;       // 0x8
    u16 chance_destructible; // 0xA
    u16 chance_chamber;      // 0xC
    u16 chance_ufo;          // 0xE
} *event_source_drop;
int event_source_drop_num;   // 0x1c
```

The per-gate `*_FilterEventDropTables` functions in `item_spawn_filter.c` zero all six chance fields for a locked item. The array is never compacted, because other code references entries by index.

## Field-to-Source Mapping

| Field | Drop source |
|---|---|
| `chance_dyna` | Dyna Blade hits/exits (patches-only pool; may also cover other untested rare sources) |
| `chance_tac` | Tac (cat enemy) |
| `chance_meteor` | Meteor explosion |
| `chance_destructible` | Generic destructible structures: **star pole, event pillar, volcano walls, houses** |
| `chance_chamber` | Secret chamber |
| `chance_ufo` | UFO |

Star pole, event pillar, volcano walls and houses all share the destructible-structures pool; only Dyna Blade keys off `chance_dyna`.

## Drop Pipeline

```
   destruction callback (obj+0x100) for rock/house, or a direct call for coral
                            |
                            v
   GrYakuBreakRock_DropItems   (0x8010203c)   --+
   GrYakuBreakHouse_DropItems  (0x80102794)   --+--> City_SpawnMiscItems(desc)  (0x80104db0)
   GrYakuBreakCoral_DropItems  (0x801040fc)   --+                |
                                                                |  dispatch on desc[8] (+0x20)
                                                                v
                        City_SpawnMiscItemsRing (0x80104e10)  /  shootPowerUps? (0x801058c0)
                                                                |
                                                                v
                             CityItem_GetEventItem(desc.drop_source)  (0x80254114)
                                                                |
                                                                v
                             _CityItem_GetEventItem(source_enum)  (0x800ebe44)
                                                                |
                                                                v
                             event_source_drop[] weighted random pick
```

The per-instance descriptor passed to `City_SpawnMiscItems` carries a `drop_source` field at `desc[7]` (`+0x1c`). If `drop_source != -1`, that value is passed to `CityItem_GetEventItem` (a thin wrapper that tail-calls `_CityItem_GetEventItem`) as the source enum. Otherwise the emitter falls back to `CityEvent_GetRandomItem` (0x80252f28, the current event's pool).

`City_SpawnMiscItems` picks the emitter from a shape flag at `desc[8]` (`+0x20`): value `1` -> `shootPowerUps?` (directed cone), value `0` (or any value < 1) -> `City_SpawnMiscItemsRing` (omnidirectional). Values > 1 hit an assert. Both emitters read `drop_source` from `desc[7]`. (`shootPowerUps?`'s trailing `?` is part of the map name and reflects an unconfirmed signature.)

## Source Enum

`_CityItem_GetEventItem` accepts an integer 0..12 dispatched through a 13-entry jump table at `0x804a5290`. The named values are the `EventDropSource` enum in `game.h`; inputs fan in to one of six chance columns, or to a no-match arm that returns -1.

| Input | Column | Meaning | Caller(s) |
|---|---|---|---|
| 0 | `chance_dyna` | Dyna Blade | `zz_8021db44_` (Dyna Blade actor) |
| 1 | `chance_tac` | Tac | `zz_8021c8ec_` (Tac actor) |
| 2 | `chance_meteor` | Meteor | `zz_8021efd8_` (Meteor actor) |
| **3** | **`chance_destructible`** | **yaku-break objects** | only via `City_SpawnMiscItems` |
| 4-8 | - | unmapped, returns -1 | - |
| 9 | `chance_chamber` | Secret Chamber | `spawnSecretChamberItems` |
| 10-11 | - | unmapped, returns -1 | - |
| 12 | `chance_ufo` | UFO | `spawnUFOItems` + 4 unnamed UFO event handlers |

The unmapped slots (4-8, 10-11) suggest the enum was sized for 13 distinct sources with several never wired up.

`chance_destructible` (input 3) is **never passed as a literal** by any caller. It is reached exclusively through the per-instance descriptor's `drop_source` field, populated from stage data - which is why one drop column is shared by every yaku-break object that drops items.

## Destructible Sources

Destructible objects in City Trial are implemented in a family of `gryakubreak*.c` source files (fully covered in `yakumono-system.md`). Only three of them emit items, and all three route through `chance_destructible`:

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

### Observations

- **`chance_dyna` is patches-only.** Only ACCEL through HP carry nonzero weights, with GLIDE getting a 4x boost (8 vs 2). Consistent with Dyna Blade only dropping patches, and explains why GLIDE feels disproportionately common from Dyna Blade interactions.
- **`chance_meteor` is also patches-only**, with GLIDE at 5 like the others (no boost).
- **`chance_ufo` is patches + ALLUP + CHARGEMAX.** It is the *only* source with meaningful weight for `ALLUP` (10) and the only source at all for `CHARGEMAX` (5). UFO is the "big stat boost" source.
- **`chance_destructible` is the broadest pool**: patches, three copy abilities (Bomb, Sleep, Mic), most foods, all three traps (Fireworks, PanicSpin, SensorBomb), and Gordo.
- **`chance_tac` skews toward food**, with patches at modest weight and Sleep as the only copy ability.
- **`chance_chamber` is patches + a few foods + Fireworks** - narrower than destructible.
- **No "down" patches and no fake patches drop from any event source.** Both are box-only.

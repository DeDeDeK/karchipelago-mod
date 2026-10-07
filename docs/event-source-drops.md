# Event Source Drops

`grBoxGeneInfo->item_desc->event_source_drop[]` is the per-stage table that drives item drops from **non-box** sources - Tac, meteor, Dyna Blade, destructible structures, secret chamber, UFO. It is loaded from the stage data file (e.g. `GrCity1.dat`) at scene init; `grBoxGeneInfo` itself lives at `*stc_grBoxGeneInfo` (r13+0x610). Box drops come from a separate table, `grBoxGeneObj`.

Each row is 0x10 bytes: an `ItemKind` followed by six `s16` weight columns, one per drop source (declared inline in `grBoxGeneInfo` in `game.h`). `event_source_drop_num` at `item_desc+0x1c` is the row count. `_CityItem_GetEventItem` (0x800ebe44) reads each weight with `lhax`/`extsh`, so a weight of 0x8000 or more subtracts from the column total instead of adding to it.

| Field | Drop source |
|---|---|
| `chance_dyna` | Dyna Blade hits/exits |
| `chance_tac` | Tac (cat enemy) |
| `chance_meteor` | Meteor explosion |
| `chance_destructible` | Generic destructible structures: star pole, event pillars |
| `chance_chamber` | Secret chamber |
| `chance_ufo` | UFO |

The star pole and the event pillars share the destructible pool; only Dyna Blade keys off `chance_dyna`. City Trial's HP-coll props (volcano walls, volcano-base holes, houses) do not use this table: their broken-state callback `zz_80109458_` spawns each prop's own item list through `CityEvent_GetRandomItem`.

**The array is indexed positionally by other code, so rows must never be reordered or compacted.** Gating a kind off means zeroing all six of its columns in place - that is what `item_spawn_filter.c` does in one pass, over every row the archipelago mod's combined locked predicate (abilities, patches, individual items) rejects. `custom_items` adds rows by copying the stage's rows into a static array, appending after them, and repointing `item_desc->event_source_drop`/`_num` at the copy (`item_registry.c`). Nothing in the event start or end paths writes that pointer or count, so the repoint holds for the whole round.

## Drop Pipeline

`GrYakuBreakRock_DropItems` (0x8010203c) and `GrYakuBreakCoral_DropItems` (0x801040fc) are `on_damage_callback`s at `obj+0x100`; `GrYakuBreakHouse_DropItems` (0x80102794) is a descriptor `coll_func`, and `hitBigStar` (0x80103eb8, the BreakCoral `coll_func`) also drops directly on a force break. All four funnel into `City_SpawnMiscItems` (0x80104db0) with a per-instance drop descriptor.

`City_SpawnMiscItems` picks the emitter from a shape flag at `desc[8]` (`+0x20`): value `1` -> `City_SpawnMiscItemsCone` (directed cone, 0x801058c0), value `0` or lower -> `City_SpawnMiscItemsRing` (omnidirectional, 0x80104e10). Values > 1 hit an assert.

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
| 12 | `chance_ufo` | the UFO's five state thinks: `CityUFO_State0Think` (0x8010b024), `CityUFO_State1Think` (0x8010b714), `CityUFO_State2Think` (0x8010be88), `CityUFO_State3Think` (0x8010c560), `CityUFO_State4Think` (0x8010cca4) |

`chance_destructible` (input 3) is **never passed as a literal** by any caller. It is reached exclusively through the per-instance descriptor's `drop_source` field, populated from stage data - which is why one drop column is shared by every yaku-break object that drops items.

## Destructible Sources

Destructible objects are a family of `gryakubreak*.c` source files. Only three emit items this way, and all three route through `chance_destructible`:

| Source file | Drop helper | Examples |
|---|---|---|
| `gryakubreakrock.c` | `GrYakuBreakRock_DropItems` (0x8010203c), `on_damage_callback` | the Pillar event's **huge pillars** (`YAKUKIND_EVENTPILLAR`, 40) only - `event_pillar` (0x80111604) calls `zz_80101a00_`, which installs it, and kind 40's state 1 (`zz_80101ca4_`) re-installs it. The file's other kind, 39 (Frozen Hillside), never does |
| `gryakubreakhouse.c` | `GrYakuBreakHouse_DropItems` (0x80102794), the `coll_func` of kinds 22/23 | `YAKUKIND_BREAKHOUSE` (22, placed only on the `GrSimple2` test ground) and kind 23, the Destruction Derby 1 rocks. City Trial's houses are the HP-coll kind 38, not this file |
| `gryakubreakcoral.c` | `GrYakuBreakCoral_DropItems` (0x801040fc) as `on_damage_callback`, `hitBigStar` (0x80103eb8) as `coll_func` | Sky Sands coral (`YAKUKIND_BREAKCORAL`, 24), kind 28 (Celestial Valley) and the City Trial **star pole** (`YAKUKIND_STARPOLE`, 29). Each creator installs `GrYakuBreakCoral_DropItems` right after its initial state change (star pole: `0x801043c8`), and `hitBigStar` re-installs it when a weak hit arms the second phase. City Trial's coral is kind 33 in `gryakubreakcoll.c`, which has no drop call |

The other families (`gryakubreakicicle.c`, `gryakuanimfloor.c`, `gryakubreakfloor.c`, `gryakubreakfan.c`, `gryakubreakcommon.c`) have no drop call at all.

Each drop-capable family gates the spawn on a NULL check of an optional drop-descriptor pointer inside its per-instance param block: `param[0x24]` for rock, `param[0x28]` for coral, `param[0x30]` for house. If it is NULL the destruction proceeds with no drops. So two instances of the same yaku-break kind can behave differently purely by stage data: in City Trial the star pole carries a non-NULL descriptor with `drop_source = 3`, and a placement that leaves the pointer NULL silently skips the drop call.

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
| COPYMIKE | 0 | 0 | 0 | 10 | 0 | 0 |
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
- Two throws skip the table entirely. Slot 0 of every UFO ring is a hardcoded `ITKIND_ALLUP`, and Dyna Blade throws one hardcoded `ITKIND_ALLUP` (`bl CityItem_Throw` at 0x8021ddf4) once enough damage lands, instead of that throw's `chance_dyna` rolls. Zeroing the ALLUP row does not reach either one.
- `chance_destructible` is the broadest pool: patches, three copy abilities (Bomb, Sleep, Mic), most foods, all three traps (Fireworks, PanicSpin, SensorBomb), and Gordo.
- `chance_tac` skews toward food, with patches at modest weight and Sleep as the only copy ability.
- `chance_chamber` is patches + a few foods + Fireworks - narrower than destructible.
- No "down" patches and no fake patches drop from any event source. Both are box-only.

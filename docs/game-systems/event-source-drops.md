# Event-Source Drops

City Trial items that come from anything but a box or the sky - TAC, Dyna Blade, the meteor, the
star pole and the event pillars, the Secret Chamber, the UFO - are drawn from one per-stage
weight table, `event_source_drop`. A mod that gates or adds item kinds has to cover this table
as well as the box tables, or those sources keep handing out what the boxes no longer do.

## The table

`grBoxGeneInfo` (`*stc_grBoxGeneInfo`, r13+0x610) is loaded from the stage file at scene init;
its `item_desc->event_source_drop` (`+0x18`) and `event_source_drop_num` (`+0x1c`) are the
table. Each 0x10-byte `ItemEventSourceDrop` row is an `ItemKind` followed by six `s16` weights,
one column per source: `chance_dyna`, `chance_tac`, `chance_meteor`, `chance_destructible`,
`chance_chamber`, `chance_ufo`. Box drops use a separate table, `grBoxGeneObj`.

`_CityItem_GetEventItem` (0x800ebe44), reached through `CityItem_GetEventItem` (0x80254114),
makes a weighted draw over one column. It sign-extends each weight, so a value of 0x8000 or more
subtracts from the column total. Its argument is an `EventDropSource` 0-12, dispatched through
the 13-entry jump table at 0x804a5290; inputs 4-8, 10 and 11 return -1.

| Source | Column | Callers |
|---:|---|---|
| 0 | `chance_dyna` | `DynaBlade_ThrowItems` (0x8021db44) |
| 1 | `chance_tac` | `Tac_ScatterItems` (0x8021c8ec) |
| 2 | `chance_meteor` | the meteor actor (0x8021efd8) |
| 3 | `chance_destructible` | only through a break family's drop descriptor |
| 9 | `chance_chamber` | `spawnSecretChamberItems` (0x8010a998) |
| 12 | `chance_ufo` | the UFO's five state callbacks, `CityUFO_State0Think` (0x8010b024) to `CityUFO_State4Think` (0x8010cca4) |

Two throws skip the table: slot 0 of every UFO ring is a hardcoded All Up, and Dyna Blade throws
one hardcoded All Up once enough damage has landed. Zeroing the All Up row reaches neither.

In City Trial's table (`GrCity1.dat`) Dyna Blade and the meteor drop only stat patches (Dyna
Blade weights Glide four times the others); the UFO adds All Up and is the only source of Charge
Max; the destructible column is the broadest, with patches, three copy abilities (Bomb, Sleep,
Mic), most foods, the three traps and Gordo; TAC leans toward food; the chamber gives patches, a
few foods and Fireworks. No source drops a down patch or a fake patch - those are box-only.

## Destructible drops

The break families that drop items call `City_SpawnMiscItems` (0x80104db0) with a per-instance
drop descriptor from the prop's parameter block. The descriptor's `drop_source` (`+0x1c`) is 3
in the shipped data, which is the only way column 3 is ever reached, and -1 falls back to the
current event's own pool (`CityEvent_GetRandomItem`, 0x80252f28). Its shape flag (`+0x20`) picks
the emitter: 1 `City_SpawnMiscItemsCone` (0x801058c0), 0 or less `City_SpawnMiscItemsRing`
(0x80104e10); anything above 1 asserts. `+0x14` is the initial item velocity and `+0x28` the
spawn offset.

| Family | Drop path | Descriptor | Props |
|---|---|---|---|
| BreakRock | `GrYakuBreakRock_DropItems` (0x8010203c), an `on_damage_callback` | `param+0x24` | the Pillar event's huge pillars (kind 40) only |
| BreakHouse | `GrYakuBreakHouse_DropItems` (0x80102794), the family's `coll_func` | `param+0x30` | kind 22 (only on `GrSimple2`) and kind 23, the Destruction Derby 1 rocks |
| BreakCoral | `GrYakuBreakCoral_DropItems` (0x801040fc) as `on_damage_callback`, and `hitBigStar` (0x80103eb8) on a force break | `param+0x28` | Sky Sands coral (24), kind 28 and the City Trial star pole (29) |

A NULL descriptor pointer skips the drop with the rest of the break unchanged, so two props of
one kind can differ purely by stage data. City Trial's houses, volcano walls and hole covers
(kinds 36-38) do not use the table: their broken-state callback (0x80109458) spawns each prop's
own item list through `CityEvent_GetRandomItem`.

## Changing it from a mod

- **Removing a kind** - zero all six of its weights in place. The draw then never lands on it,
  and the row count and order stay what the stage file shipped.
- **Adding rows** - copy the table into mod memory, append, and point `item_desc->
  event_source_drop` and `event_source_drop_num` at the copy. Nothing in the event start or end
  paths rewrites either field, so the repoint holds for the round.

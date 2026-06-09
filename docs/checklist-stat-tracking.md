# Checklist Stat Tracking (`plclearcheckerlib`)

How the game accumulates the per-player gameplay stats that drive checklist
completion. This is the *measurement* layer that sits underneath the clear-bit
storage described in `clearchecker-system.md`: this doc covers **what counts and
where the running totals live**, the sister doc covers **how a completed
condition is stored and detected**.

The game-side source file (from the assert strings, e.g.
`s_plclearcheckerlib_c_804b4cdc`) is `plclearcheckerlib.c` — the "player
clear-checker library." Everything here is reverse-engineered. As of the last
exploration pass, **all 120 City Trial cells (clear_kind 0x00–0x77) are mapped
to a game-side condition** — see the master table below. The remaining unknowns
are the *exact physical meaning* of a few stat fields (flagged in each table)
and the canonical name of the stadium-group discriminant.

## Architecture

Three layers turn gameplay into a checked checklist box:

1. **Per-player stat struct** — a live per-player record updated every frame
   during a game (item pickups, boxes broken, distance raced, KOs, …). Accessor:
   `Ply_GetItemCollectArray` (`0x8022d248`).
2. **Evaluators** — `CityTrial_CheckForNewUnlocks` (`0x8004db74`) and its five CT
   siblings read the per-player struct via the `Ply_Get*` getter family, fold each
   player's totals into a **persistent records block**, test every condition's
   threshold, and call `ClearChecker_SetNewUnlock(mode, clear_kind)` for each one
   met.
3. **Clear-bit storage / detection** — `ClearChecker_SetNewUnlock` flips the
   `GameClearData.clear[clear_kind]` bit. See `clearchecker-system.md`. (The AP
   mod hooks this function in `check_detection.c` to capture every check.)

```
gameplay  --writes-->  per-player stat struct (+0x4c8 item_collect[], +0x62b yakumono[], …)
                              |  Ply_Get* getter family
                              v
CityTrial_Check*Objectives    --accumulates-->  records block (GameData_CityTrialClear[1] tail)
                              --threshold met-->  ClearChecker_SetNewUnlock(2, clear_kind)
                              v
                       GameClearData.clear[clear_kind]   (clearchecker-system.md)
```

All six CT evaluators share a skeleton: bail unless `Checklist_IsCacheValid()`
(`0x8007b650`) returns 0, then loop player slots 0..4, gate on
`plGetPlayerKind(player)` (`0x8022c858`; `0` = human), and call
`ClearChecker_SetNewUnlock(2, kind)` (`0x8004a054`) for each met threshold.
Per-game cells test a stat directly; cumulative cells fold the per-game value
into the records block first. **Scope varies**: some accumulators sum over
*human* players only, others over all *present* players (`plGetPlayerKind != 4`)
— noted per cell where it matters.

## Per-player stat struct

`Ply_GetItemCollectArray(int player)` (`0x8022d248`) returns the base of the
calling player's stat record:

```
base = player * 0x90c + 0x8055AAA0      // players 0..4
```

The name is historical — it returns the whole record, not just the item array.
Known fields (offsets relative to `base`):

| Offset | Type | Meaning | Getter | Drives | Conf |
|--------|------|---------|--------|--------|------|
| `+0x37a` | u8 bits | Copy-Chance ability flags: bit3 = got Bomb, bit5 = got Sleep | `0x8022ed50` (bomb) / `0x8022eda8` (sleep) | 0x46 / 0x47 | verified |
| `+0x37c` | int[26] | Per-MachineKind change counter; sum = total Air Ride machine changes | `0x8022f19c` (sums all 26) | 0x06 | verified |
| `+0x4b4` | int | KO-by-cause: CPU machine broken (written by `Ply_AddDeath` on cause byte) | `0x8022f418` | 0x4d | verified |
| `+0x4b8` | int | KO-by-cause: Firework | `0x8022f46c` | 0x60 | verified |
| `+0x4bc` | int | KO-by-cause: Gold Spike | `0x8022f4c0` | 0x5f | verified |
| `+0x4c0` | int | KO-by-cause: Sensor Bomb | `0x8022f514` | 0x5e | verified |
| `+0x4c4` | u16 | **Vehicle-bust bitfield** (MSB-first, bit `15−idx`). idx 0..7 → cells 0x6f..0x76; entries 8/9 (bits 7/6) are Dragoon↔Hydra mutual busts, never read. | `0x8022f3a4(player,idx)` | 0x6f–0x76 | verified |
| `+0x4c8` | int[] | **Item-collect array**, indexed by `ItemKind` (valid `0..0x44`). 0/1/2 = boxes; 3..0x43 = everything else. | see Item-collect subsystem | many | verified |
| `+0x5e4`, `+0x5e8` | int | Two drive-time components (frames); summed = Free Run drive time | `0x80231510` (sums both) | 0x09–0x0B | verified (sum); component split partial |
| `+0x5f4` | int | Airborne time, 60 fps frames | `0x802315c0` | 0x18 / 0x1C / 0x22 | verified |
| `+0x604` | int | "First 20 seconds" guard for the `+0x804` aggregate (increment skipped while zero). | — | 0x48 | partial |
| `+0x60c`, `+0x610` | f32 | Two distance components; summed by `0x80231614`, accumulated into `records+0x14` for the "race over N miles" cells. | `0x80231614` (sums both) | 0x00 / 0x01 | verified |
| `+0x62b` | u8[20] | **Yakumono-break bucket array**, valid idx `0x15..0x28`. One counter per destructible-object descriptor stat-index. See dedicated section. | `0x8022fccc(player,idx)` | many | verified |
| `+0x653` | u8 | Valid flag for the `+0x830` pillar timer (also = yakumono idx 0x28 region). | — | 0x33 | verified |
| `+0x7ec` | int | Sky rings flown through | `0x80230838` | 0x39 | verified |
| `+0x804` | int | Items (boxes excluded) picked up while the `+0x604` first-20s guard is set | `0x8022faac` | 0x48 | verified |
| `+0x808` | int | Items whose pickup source tag (`item+0x20`) == 4 → **items stolen from Tac** | `0x8022fa58` | 0x34 | verified |
| `+0x820` | int | Waterwheel rides | `0x80230be8` | 0x3d | verified |
| `+0x830` | int | Fastest huge-pillar break time (frames; valid iff `+0x653`) | `0x8022fdc0` | 0x33 | verified |
| `+0x834` | int | High-plains hole entries | `0x80230420` | 0x40 | verified |
| `+0x838` | int | Super-jump-ramp building landings | `0x80230474` | 0x43 | verified |
| `+0x840` | int | "All players off machines" value (`!= -1 && <= 60` ⇒ unlock) | `0x8022de74` | 0x4a | verified (cond); meaning partial |
| `+0x848` | int | King Dedede KO timestamp (frame of first KO of the KD boss = victim slot 4). 0 = not yet. | `0x8022f568` | 0x2f | verified |
| `+0x84c` | u8 bits | bit0 = damaged Dyna Blade, bit1 = trampled by Dyna Blade, bit7 = damaged a rival within 10 s (`ClearChecker_CheckJustUnlocked_CityTrial_RivalDamage10Sec`, `0x8022ebdc`). | bit7 via `0x8022ebdc` | 0x30 / 0x31 / 0x49 | verified |
| `+0x84d` | u8 bits | bit1 = reached sky garden, bit2 = Hydra assembled, bit3 = Dragoon assembled, bit4 = entered castle chamber, bit5 = used a restoration area. bits2/3 written by `Ply_MarkLegendaryMachineAssembled` (`0x80231198`). | inline | 0x3e/0x77/0x38/0x36 | verified |
| `+0x850` | int | Grind-rail-into-crater flag (`!= 0` ⇒ unlock) | `0x80230240` | 0x42 | verified |
| `+0x854` | u8 bits | bit3 = off machine at timeout, bit4 = on rails at timeout. (`+0x855` bit6 = suppress-yakumono-increment.) | inline | 0x4b / 0x4c | verified |

> **Two distinct arrays, same-looking indices.** `+0x4c8[k]` is the *ItemKind*
> int array (`collect[0x28]` = Energy Drink). `+0x62b+idx` is the *yakumono*
> u8 array (`byte[0x28]` = huge pillars). They are unrelated — don't conflate
> the index spaces.

### KO-event recorder: `Ply_AddDeath` (`0x8022f648`)

Misleadingly named — it is the unified **KO-event recorder**, run on every KO.
Args: victim = arg0, achiever/killer = `*(arg1+0x1c)`. From a single KO it
writes several of the killer's stat fields:

- kills-by-machine `+0x44c[]`, deaths-by-machine `+0x3e4[]`
- the four KO-by-cause counters `+0x4b4/+0x4b8/+0x4bc/+0x4c0`, selected by the
  victim's cause byte (`victim+7`)
- the **vehicle-bust bitfield** `+0x4c4` via a 10-entry / 8-byte table at
  `0x804B4C68` (see Vehicle-bust section)
- the **King Dedede KO time** `+0x848` (only when victim slot == 4 and the field
  is still 0): `*(+0x848) = current frame`

## Item-collect subsystem (verified)

The item-collect array at `+0x4c8` is fully traced. **`ItemKind` 0/1/2 are the
three boxes (`ITKIND_BOX{BLUE,GREEN,RED}`); 3..0x43 are everything else**
(patches, copy-ability panels, food, special items, hazards, legendary parts —
see `item.h`).

### Producers

| Address | Name | Role |
|---------|------|------|
| `0x8022fbcc` | `Ply_IncrementItemCollectNum(player, itemkind, src_tag)` | `collect[itemkind]++`. Also: if `src_tag == 4`, `+0x808++` (Tac); if `itemkind > 2` and `+0x604 != 0`, aggregate `+0x804++` (first-20s items). |
| `0x8022fb58` | `Ply_DecrementItemCollectNum(player, itemkind)` | Drop-side partner: `collect[itemkind]--`; if `itemkind > 2` and `+0x604 != 0`, `+0x804--` (first-20s aggregate). **Two params only** — no `src_tag`, and unlike the producer it never decrements `+0x808`, so the Tac count is increment-only. |

`Ply_IncrementItemCollectNum` has a **single caller**: `Machine_OnTouchItem`
(`0x801db34c`, call site `0x801db928`), on the common path for *every*
collected item (`itemkind = item+0x1c`, `src_tag = item+0x20`).
`Ply_DecrementItemCollectNum` has **two callers**, both on the drop pipeline:
`Rider_SpawnDropPatchSeq` (`0x8019ce50`, two sites) when a rider sheds patches,
and the all-up legendary drop when a collected Hydra/Dragoon piece is thrown —
see `patch-drop-system.md`.

### Getters

| Address | Name | Returns |
|---------|------|---------|
| `0x8022f898` | `Ply_GetItemCollectNum(player, itemkind)` | `collect[itemkind]` (one kind). |
| `0x8022f920` | `Ply_GetItemCollectTotal(player)` | `Σ collect[3..0x43]` — items picked up this game, **boxes excluded**. |
| `0x8022f9bc` | `Ply_GetBoxCollectTotal(player)` | `Σ collect[0..2]` — boxes collected/broken this game. |

The `3..0x43` lower bound in `Ply_GetItemCollectTotal` is *why* the "pick up N
items" checklist counts patches/abilities/food/etc. but never boxes.

**ItemKind indices used by checklist cells** (in addition to the patch list in
`How CheckForNewUnlocks consumes them`): `0x27` = Maxim Tomato (→ 0x61),
`0x28` = Energy Drink (→ 0x62), `0x30` = sushi (→ 0x6b), `0x31` = hot dog (→ 0x6c).

## City Trial evaluators — which function owns which cells

| Address | Function | Cells owned |
|---------|----------|-------------|
| `0x8004db74` | `CityTrial_CheckForNewUnlocks` | 0x00–0x05, 0x07, 0x08, 0x30–0x36, 0x38–0x50*, 0x52, 0x54, 0x56, 0x58, 0x5a, 0x5c–0x62, 0x6b, 0x6c, 0x6f–0x77 (*the per-patch / gameplay cells; see master table) |
| `0x8004e660` | `CityTrial_CheckFreeRunObjectives` | 0x06, 0x09, 0x0A, 0x0B |
| `0x8004e748` | `CityTrial_CheckStadiumPlayedObjectives` | 0x0C, 0x0D |
| `0x8004e810` | `CityTrial_CheckStadiumScoreObjectives` | 0x16, 0x17, 0x18 (High Jump), 0x1C, 0x1E (Target Flight airborne/total) |
| `0x8004e998` | `CityTrial_CheckStadiumResultObjectives` | 0x0C, 0x0D (re-count), 0x0E–0x15, 0x19, 0x1A, 0x1B, 0x1D, 0x1F–0x22, 0x28, 0x29, 0x2A, 0x2D, 0x2E, 0x2F, 0x63–0x6A |
| `0x8004f03c` | `CityTrial_CheckStadiumKOObjectives` | 0x23–0x27, 0x2B, 0x2C, 0x51, 0x53, 0x55, 0x57, 0x59, 0x5B |
| `0x8017e490` | `Checklist_ProcessUnlock` (meta) | 0x37, 0x6d, 0x6e — see `clearchecker-system.md` |

> **`CityTrial_CheckStadiumKOObjectives` handles single-game Destruction
> Derby / Kirby Melee KO-count cells** — not the "Single Race" stadium or any
> lap/time cell, despite where it sits in the table. It switches on
> `GameData+0xa94` (14 = Destruction Derby, 13 = Kirby Melee) then
> `GameData+0x5ad` (specific stadium id: DD1..DD5 = 9..13, KM1/KM2 = 7/8).

## Master cell table (City Trial, mode = 2)

Conditions use the *game's actual comparison* (most "over N" objectives compile
to `>= N`). Time thresholds are 60 fps frames. `records+N` = the persistent
accumulator block (see below); `+0xNN` = per-player stat offset; `byte[idx]` =
yakumono array `+0x62b+idx`; `collect[k]` = item array `+0x4c8[k]`. Evaluator
codes: **FNU** = CheckForNewUnlocks, **FR** = CheckFreeRun, **SP** =
CheckStadiumPlayed, **SS** = CheckStadiumScore, **ST** = CheckStadiumResult,
**SR** = CheckSingleRace (KO), **PU** = Checklist_ProcessUnlock.

| ck | Objective | Eval | Condition |
|----|-----------|------|-----------|
| 0x00 | race over 60 miles | FNU | `records+0x14` (Σ dist) → miles `= raw*11.4286/160934 >= 60.0` |
| 0x01 | race over 200 miles | FNU | same `>= 200.0` |
| 0x02 | pick up >100 items | FNU | `records+0xe` (Σ `Ply_GetItemCollectTotal`) `>= 100` |
| 0x03 | >500 items | FNU | `records+0xe >= 500` |
| 0x04 | >1000 items | FNU | `records+0xe >= 1000` |
| 0x05 | >3000 items | FNU | `records+0xe >= 3000` |
| 0x06 | Free Run: change machines 10× | FR | `Σ +0x37c[26] >= 10` (per human) |
| 0x07 | break >500 boxes | FNU | `records+0x10` (Σ `Ply_GetBoxCollectTotal`) `>= 500` |
| 0x08 | >1000 boxes | FNU | `records+0x10 >= 1000` |
| 0x09 | Free Run: drive 10 min | FR | `(Σ_human (+0x5e4 + +0x5e8)) + records+0x18 >= 36000` |
| 0x0A | drive 30 min | FR | `>= 108000` |
| 0x0B | drive 2 hours | FR | `>= 432000` |
| 0x0C | play >10 stadium modes | SP, ST | `Σ_{0..23} Gm_StadiumIsAvailable(i) >= 10` |
| 0x0D | play >20 stadium modes | SP, ST | `>= 20` |
| 0x0E | DRAG1 < 24.00s | ST | finish `GameData+0x8B8[p] <= 1440` |
| 0x0F | DRAG1 < 20.00s | ST | `<= 1200` |
| 0x10 | DRAG2 < 24.00s | ST | `<= 1440` |
| 0x11 | DRAG2 < 20.00s | ST | `<= 1200` |
| 0x12 | DRAG3 < 35.00s | ST | `<= 2100` |
| 0x13 | DRAG3 < 27.00s | ST | `<= 1620` |
| 0x14 | DRAG4 < 24.00s | ST | `<= 1440` |
| 0x15 | DRAG4 < 19.00s | ST | `<= 1140` |
| 0x16 | High Jump > 500 ft | SS | `GameData+0xA4C[p]` (f32 m) `/0.3048 >= 500.0` |
| 0x17 | High Jump > 1000 ft | SS | `>= 1000.0` |
| 0x18 | High Jump airborne > 10 s | SS | `+0x5f4 >= 600` |
| 0x19 | TF one game > 150 pts | ST | `GameData+0xA38[p] >= 150` |
| 0x1A | TF one game **exactly 90 pts** | ST | `GameData+0xA38[p] == 90` (equality `bne`) |
| 0x1B | TF perfect 200 pts | ST | `GameData+0xA38[p] >= 200` |
| 0x1C | TF airborne > 15 s | SS | `+0x5f4 >= 900` |
| 0x1D | TF played 15+ times | ST | `records-0x4` (u8) `>= 15` |
| 0x1E | TF total > 1500 pts | SS | `records+0x8` (Σ TF pts, u16) `>= 1500` |
| 0x1F | Air Glider > 330 ft | ST | `GameData+0xA4C[p]/0.3048 >= 330.0` |
| 0x20 | Air Glider > 660 ft | ST | `>= 660.0` |
| 0x21 | Air Glider > 1300 ft | ST | `>= 1300.0` |
| 0x22 | Air Glider airborne > 30 s | ST | `+0x5f4 >= 1800` |
| 0x23 | DD1 KO rivals 5× | SR | `GameData+0xA38[p] >= 5` (id 9) |
| 0x24 | DD2 KO rivals 5× | SR | `>= 5` (id 10) |
| 0x25 | DD3 KO rivals 5× | SR | `>= 5` (id 11; no 10× cell) |
| 0x26 | DD4 KO rivals 5× | SR | `>= 5` (id 12) |
| 0x27 | DD5 KO rivals 5× | SR | `>= 5` (id 13) |
| 0x28 | DD1 bust all rocks | ST | `Σ_present byte[0x17] >= 2` (id 9) |
| 0x29 | DD (all) KO enemies 50× | ST | `records+0xa` (u16) `>= 50` |
| 0x2A | DD (all) KO enemies 150× | ST | `records+0xa >= 150` |
| 0x2B | Melee1 KO 50× | SR | `GameData+0xA38[p] >= 50` (id 7) |
| 0x2C | Melee2 KO 30× | SR | `>= 30` (id 8) |
| 0x2D | Melee (all) KO 500× | ST | `records+0xc` (u16) `>= 500` |
| 0x2E | Melee (all) KO 1500× | ST | `records+0xc >= 1500` |
| 0x2F | KO King Dedede < 1 min | ST | `+0x848 != 0 && <= 3600` |
| 0x30 | damage Dyna Blade | FNU | `+0x84c bit0` |
| 0x31 | trampled by Dyna Blade | FNU | `+0x84c bit1` |
| 0x32 | break 5+ huge pillars | FNU | `records-0x3` (Σ_human byte[0x28]) `>= 5` |
| 0x33 | break pillar within 40 s | FNU | `+0x830` (valid `+0x653`) `>= 0 && <= 2400` |
| 0x34 | steal >8 items from Tac | FNU | `+0x808 >= 8` |
| 0x35 | meteor attacks 3× | FNU | `records-0x2` (Σ global event-count `0x800ee93c(2)`) `>= 3` |
| 0x36 | use restoration area | FNU | `+0x84d bit5` |
| 0x37 | fill 100 checklist blocks | PU | meta auto-unlock (direct `stb`) |
| 0x38 | enter castle chamber | FNU | `+0x84d bit4` |
| 0x39 | fly through 5 sky rings | FNU | `+0x7ec >= 5` |
| 0x3a | bust the star pole | FNU | `byte[0x1d] != 0` |
| 0x3b | bust star pole 10× | FNU | `records-0x1` (Σ_human byte[0x1d]) `>= 10` |
| 0x3c | open all volcano-base holes | FNU | `Σ_present byte[0x25] >= 3` |
| 0x3d | waterwheel carry 10× | FNU | `records+0x6` (Σ_human `+0x820`) `>= 10` |
| 0x3e | reach the sky garden | FNU | `+0x84d bit1` |
| 0x3f | open forest pitfall | FNU | `byte[0x20] != 0` |
| 0x40 | high-plains hole 3× | FNU | `Σ_human +0x834 >= 3` |
| 0x41 | break all volcano+highplains rocks | FNU | `Σ_present byte[0x23] >= 41` |
| 0x42 | grind-rail into crater | FNU | `+0x850 != 0` |
| 0x43 | super-jump onto building 10× | FNU | `records+0x4` (Σ_human `+0x838`) `>= 10` |
| 0x44 | destroy all dilapidated houses | FNU | `Σ_present byte[0x26] >= 30` |
| 0x45 | knock down all forest trees | FNU | `Σ_present byte[0x22] >= 53` |
| 0x46 | Bomb from Copy Chance | FNU | `+0x37a bit3` |
| 0x47 | Sleep from Copy Chance | FNU | `+0x37a bit5` |
| 0x48 | 10 items in first 20 s | FNU | `+0x804 >= 10` |
| 0x49 | damage rival in first 10 s | FNU | `+0x84c bit7` |
| 0x4a | all players off machines | FNU | `+0x840 != -1 && <= 60` |
| 0x4b | timeout while all off machines | FNU | present-count `== Σ +0x854 bit3` |
| 0x4c | timeout while all on rails | FNU | present-count `== Σ +0x854 bit4` |
| 0x4d | break CPU machine 5× | FNU | `records+0x5` (Σ_human `+0x4b4`) `>= 5` |
| 0x4e | damage all 3 CPU rivals | FNU | distinct-rivals-damaged (`0x80231cec`) `>= 3` |
| 0x4f | 50+ items in one game | FNU | `Ply_GetItemCollectTotal(p) >= 50` |
| 0x50 | 10+ Boost patches | FNU | `collect[3] >= 10` |
| 0x51 | DD1 KO a rival 10× | SR | `GameData+0xA38[p] >= 10` (id 9) |
| 0x52 | 10+ Top Speed patches | FNU | `collect[5] >= 10` |
| 0x53 | DD2 KO a rival 10× | SR | `>= 10` (id 10) |
| 0x54 | 10+ Turn patches | FNU | `collect[0xb] >= 10` |
| 0x55 | DD4 KO a rival 10× | SR | `>= 10` (id 12) |
| 0x56 | 10+ Charge patches | FNU | `collect[0xf] >= 10` |
| 0x57 | DD5 KO a rival 10× | SR | `>= 10` (id 13) |
| 0x58 | 10+ Weight patches | FNU | `collect[0x11] >= 10` |
| 0x59 | Melee1 KO 75× by self | SR | `GameData+0xA38[p] >= 75` (id 7) |
| 0x5a | 10+ Defense patches | FNU | `collect[9] >= 10` |
| 0x5b | Melee2 KO 40× by self | SR | `>= 40` (id 8) |
| 0x5c | 10+ Glide patches | FNU | `collect[0xd] >= 10` |
| 0x5d | 30+ Glide patches (cumulative) | FNU | `records+0x3` (Σ_human `collect[0xd]`) `>= 30` |
| 0x5e | Sensor Bomb KO 3× | FNU | `records+0x0` (Σ_human `+0x4c0`) `>= 3` |
| 0x5f | Gold Spike KO 3× | FNU | `records+0x1` (Σ_human `+0x4bc`) `>= 3` |
| 0x60 | Firework KO 10× | FNU | `records+0x2` (Σ_human `+0x4b8`) `>= 10` |
| 0x61 | eat 2+ Maxim Tomatoes | FNU | `collect[0x27] >= 2` |
| 0x62 | drink 3+ Energy Drinks | FNU | `collect[0x28] >= 3` |
| 0x63 | DRAG1 < 26.00s on Warpstar | ST | time `<= 1560` & `(IsBike=0, Mk=0)` |
| 0x64 | DRAG1 < 17.00s on Formula Star | ST | `<= 1020` & `(0,7)` |
| 0x65 | DRAG2 < 27.00s on Wagon Star | ST | `<= 1620` & `(0,9)` |
| 0x66 | DRAG2 < 29.00s on Winged Star | ST | `<= 1740` & `(0,2)` |
| 0x67 | DRAG3 < 28.00s on Swerve Star | ST | `<= 1680` & `(0,11)` |
| 0x68 | DRAG3 < 31.00s on Wheelie Bike | ST | `<= 1860` & `(1,2)` |
| 0x69 | DRAG4 < 33.00s on Turbo Star | ST | `<= 1980` & `(0,12)` |
| 0x6A | DRAG4 < 24.00s on Rex Wheelie | ST | `<= 1440` & `(1,3)` |
| 0x6b | eat 3+ sushi | FNU | `collect[0x30] >= 3` |
| 0x6c | eat 3+ hot dogs | FNU | `collect[0x31] >= 3` |
| 0x6d | Unlock Dragoon Parts (meta) | PU | meta auto-unlock |
| 0x6e | Unlock Hydra Parts (meta) | PU | meta auto-unlock |
| 0x6f–0x76 | bust vehicle X while riding Y | FNU | `+0x4c4 bit(15−idx) != 0`, idx = ck−0x6f |
| 0x77 | complete Dragoon AND Hydra | FNU | `(any present +0x84d bit3) && (any present bit2)` |

The drag-race finish-time field `GameData+0x8B8[p]` and the polymorphic score
field `GameData+0xA38[p]` are unit-`1/60 s` and plain-int respectively.
`(IsBike, Mk)` = `Ply_GetIsBike` (PlayerData `+0x8E`, `0x8022c8b0`) and
`Ply_GetMachineKind` (PlayerData `+0x8F`, `0x8022c8e0`); these are *per-category*
machine indices, distinct from `VCKIND_*`.

## The records / accumulator block (`GameData_CityTrialClear[1]`)

`gmGetClearcheckerType2Ptr()` (`0x8000774c`) returns the second
`GameClearData`-sized slot (base `0x80536980`), reused as a **cross-game stat
accumulator**. The accumulators live in the slot's tail, past the nominal `0xF4`
struct. The doc's `records+N` convention is anchored at **base + 0xF8**
(`records+0` = `0x80536A78`); the raw offset from the type2 base is `0xF8 + N`.
Each field folds in per-game player totals (capped) and is threshold-tested.

| `records+N` | raw off | type | role | cap | drives |
|-------------|---------|------|------|-----|--------|
| `-0x4` | `+0xF4` | u8 | Target Flight games played | 255 | 0x1D |
| `-0x3` | `+0xF5` | u8 | huge pillars broken (Σ byte[0x28]) | 255 | 0x32 |
| `-0x2` | `+0xF6` | u8 | meteor strikes (Σ event-count[2]) | 255 | 0x35 |
| `-0x1` | `+0xF7` | u8 | star-pole busts (Σ byte[0x1d]) | 255 | 0x3b |
| `+0x0` | `+0xF8` | u8 | Sensor Bomb KOs | 255 | 0x5e |
| `+0x1` | `+0xF9` | u8 | Gold Spike KOs | 255 | 0x5f |
| `+0x2` | `+0xFA` | u8 | Firework KOs | 255 | 0x60 |
| `+0x3` | `+0xFB` | u8 | Glide patches | 255 | 0x5d |
| `+0x4` | `+0xFC` | u8 | super-jump building landings | 255 | 0x43 |
| `+0x5` | `+0xFD` | u8 | CPU-machine breaks | 255 | 0x4d |
| `+0x6` | `+0xFE` | u8 | waterwheel rides | 255 | 0x3d |
| `+0x8` | `+0x100` | u16 | Target Flight points total | 65535 | 0x1E |
| `+0xa` | `+0x102` | u16 | Destruction Derby enemy-KO total | 65535 | 0x29 / 0x2A |
| `+0xc` | `+0x104` | u16 | Kirby Melee enemy-KO total | 65535 | 0x2D / 0x2E |
| `+0xe` | `+0x106` | u16 | item total (boxes excluded) | 65535 | 0x02–0x05 |
| `+0x10` | `+0x108` | u16 | box total | 65535 | 0x07 / 0x08 |
| `+0x14` | `+0x10C` | f32 | distance total | ~8.75e7 | 0x00 / 0x01 |
| `+0x18` | `+0x110` | int | Free Run drive time (frames; only *read* here — writer elsewhere) | — | 0x09–0x0B |

## GameData stadium-result fields

Written at stadium finish; per-player fields are `[5]` arrays, stride 4, based at
`GameData+0x830`. These match the `x830/x8b8/xa38/xa4c/xa94` placeholders in
`item.h`.

| Offset | Type | Meaning |
|--------|------|---------|
| `+0x5AD` | u8 | `StadiumKind` (full enum; DD1 = 9, etc.) |
| `+0x852+p` | u8 | per-player participated/finished flag (gate) |
| `+0x8B8+4p` | s32 | drag-race finish time (1/60 s ticks) |
| `+0xA38+4p` | s32 | polymorphic score: TF points / DD enemy-KO / Melee enemy-KO / per-game KO count |
| `+0xA4C+4p` | f32 | distance in meters (High Jump height / Air Glider distance); feet = `/0.3048` |
| `+0xA94` | u8 | **stadium-group discriminant** (the switch key): 7 = Drag Race, 8 = Air Glider, 9 = Target Flight, 11 = High Jump, 13 = Kirby Melee, 14 = Destruction Derby, 18 = VS King Dedede. Not `StadiumGroup` (0..7) nor `StadiumKind`; name/source unconfirmed. |

ST's jump table is at `0x80497738` (12 × u32, indexed `(disc − 7)`).

## The yakumono-break bucket array (`+0x62b`)

**Correction to earlier notes:** this array is **not** the broad event-counter
set (rings/meteor/waterwheel/etc. live in dedicated int/flag fields above). It is
a **per-destructible-object break-count bucket array** — one u8 counter per
yakumono descriptor *stat-index*.

- **Getter** `0x8022fccc(player, idx)`: `return *(u8*)(base + 0x62b + idx)` for
  `idx ∈ [0x15, 0x28]` (returns 0 outside; asserts non-fatally for `player >= 5`).
  Index is **not** biased. Absolute struct offsets `0x640..0x653`.
- **Incrementer** `0x8022fed8(player, idx)`: `byte[idx]++`, gated by
  `idx ∈ (0x14, 0x29)`, `player != 5`, and `+0x855` bit6 clear. For **idx 0x28
  only** it also records the fastest break time at `+0x830` (drives 0x33).
- **Reset** `0x8022d8c8(player)`: zeros `0x640..0x653` — **per game**.
  Cumulative carry lives in the records block, not here.
- **Producers** are yakumono destroy handlers (`hitWeakObject` /
  `hitStrongObject` / `hitBigStar` / `event_pillar_start` / breakrock/breakhouse,
  ~30 callers) → wrapper `GrYaku_IncrementBreakCount` (`0x80105d80`):
  `idx = GrYakumono_GetDescId(obj)` (`0x800f7a64`, the descriptor stat-index,
  `*(int*)(*(int*)(obj+0x2c) + 4)`) → `Ply_IncrementYakumonoBreakCount`.
  (See `yakumono-system.md`.)

Because the index comes from the object descriptor, the *meaning of a given
index is stage/mode-dependent* — the same bucket is reused across stages (e.g.
idx 0x18 = Sky Sands coral in Air Ride → AR cell 0x67; idx 0x20 = forest-pitfall
cover in CT → 0x3f *and* Frozen Hillside ice platforms in AR → AR cell 0x66).

### Index → meaning (City Trial / Stadium consumers)

| idx | offset | meaning (active stage) | consumer → clear_kind | conf |
|-----|--------|------------------------|-----------------------|------|
| 0x17 | 0x642 | Destruction Derby rocks | ST, id 9, `Σ_present > 1` → **0x28** | high |
| 0x1d | 0x648 | star-pole busts | FNU: `!= 0` → **0x3a**; `records-0x1 Σ > 9` → **0x3b** | high |
| 0x20 | 0x64b | forest-pitfall cover | FNU: `!= 0` → **0x3f** | high |
| 0x22 | 0x64d | forest trees | FNU: `Σ_present > 0x34` → **0x45** | high |
| 0x23 | 0x64e | volcano + high-plains rocks | FNU: `Σ_present > 0x28` → **0x41** | high |
| 0x25 | 0x650 | volcano-base hole covers | FNU: `Σ_present > 2` → **0x3c** | high |
| 0x26 | 0x651 | dilapidated houses | FNU: `Σ_present > 0x1d` → **0x44** | high |
| 0x28 | 0x653 | huge pillars | FNU: `records-0x3 Σ > 4` → **0x32**; `!= 0` + timer → **0x33** | high |

Indices with a producer path but **no checklist consumer** (other stages'
destructibles or spare): `0x15, 0x16, 0x18*, 0x19, 0x1a, 0x1b, 0x1c, 0x1e, 0x1f,
0x21, 0x24, 0x27` (*0x18 is consumed in Air Ride, not CT). These are left
unlabeled deliberately rather than guessed.

## Vehicle-bust bitfield (`+0x4c4`) — cells 0x6f–0x76

`Ply_GetVehicleBustFlag(player, idx)` (`0x8022f3a4`) returns `*(u16*)(base+0x4c4) & (1 << (15 − idx))`
(MSB-first). `CityTrial_CheckForNewUnlocks` reads idx 0..7 (bits 15..8) and sets
`clear_kind = 0x6f + idx` when the bit is set. The bits are written by
`Ply_AddDeath` from a 10-entry table at **`0x804B4C68`** (each entry = `{busted
machine X, riding machine Y}`); on a KO matching `bustedMachine ==
table[i].field0 && Ply_GetMachineKind(killer) == table[i].field1` it sets bit
`15 − i` on the killer's `+0x4c4`.

| idx | bit | clear_kind | busted X | riding Y |
|-----|-----|-----------|----------|----------|
| 0 | 15 | 0x6f | Wheelie Scooter (0x17) | Compact Star (0x01) |
| 1 | 14 | 0x70 | Wheelie Bike (0x15) | Warp Star (0x00) |
| 2 | 13 | 0x71 | Swerve Star (0x0b) | Wheelie Bike (0x15) |
| 3 | 12 | 0x72 | Warp Star (0x00) | Swerve Star (0x0b) |
| 4 | 11 | 0x73 | Formula Star (0x07) | Turbo Star (0x0c) |
| 5 | 10 | 0x74 | Slick Star (0x06) | Formula Star (0x07) |
| 6 | 9 | 0x75 | Rocket Star (0x0a) | Slick Star (0x06) |
| 7 | 8 | 0x76 | Turbo Star (0x0c) | Rocket Star (0x0a) |
| (8) | 7 | (unread) | Dragoon (0x08) | Hydra (0x04) |
| (9) | 6 | (unread) | Hydra (0x04) | Dragoon (0x08) |

Table entries 8/9 (Dragoon↔Hydra mutual busts) are written but **never read** by
the getter (idx only 0..7) — likely vestigial. **Legendary-machine assembly** is
a *separate* mechanism: `+0x84d` bit2 (Hydra) / bit3 (Dragoon), written by
`Ply_MarkLegendaryMachineAssembled` (`0x80231198`), which drives cell 0x77.

## Distance → miles math (cells 0x00 / 0x01)

`Ply_GetTotalDistance` (`0x80231614`) = `*(f32*)(base+0x60c) + *(f32*)(base+0x610)`.
FNU accumulates the per-game sum (capped at `0x4CA6E49C` = 8.75e7) into
`records+0x14`, then:

```
miles = raw * 11.4285717 / 160934.40625      // constA ≈ 80/7, constB = 160934.4 cm/mile
0x00:  miles >= 60.0      (≈ 844,906 raw)
0x01:  miles >= 200.0     (≈ 2,816,352 raw;  ≈ 14081.76 raw/mile)
```

The two distance components `+0x60c` / `+0x610` are accumulated per-frame in
`Ply_UnkUpdate` (`0x80231340`) into two buckets selected by a flag at
`stat+0x754` (ground vs air or similar — bucket semantics partial; the miles math
is exact). A third per-frame accumulator at `+0x614` is *not* summed by the
getter.

## Per-player getter names

These per-player `Ply_Get*` getters are present under these names in
`GKYE01.map`. Listed here as the canonical address↔field index for the stat
layer:

| Address | Name | Field |
|---------|------|-------|
| `0x8022fccc` | `Ply_GetYakumonoBreakCount(player, idx)` | `+0x62b+idx` |
| `0x8022fed8` | `Ply_IncrementYakumonoBreakCount(player, idx)` | `+0x62b+idx` |
| `0x8022d8c8` | `Ply_ResetGameStats(player)` | zeros yakumono array (full scope TBD) |
| `0x8022f3a4` | `Ply_GetVehicleBustFlag(player, idx)` | `+0x4c4` bit |
| `0x8022f568` | `Ply_GetKingDededeKOTime(player)` | `+0x848` |
| `0x80231614` | `Ply_GetTotalDistance(player)` | `+0x60c` + `+0x610` |
| `0x8022f19c` | `Ply_GetMachineChangeCount(player)` | `Σ +0x37c[26]` |
| `0x80231510` | `Ply_GetDriveTime(player)` | `+0x5e4` + `+0x5e8` |
| `0x802315c0` | `Ply_GetAirborneTime(player)` | `+0x5f4` |
| `0x8022fa58` | `Ply_GetTacStolenCount(player)` | `+0x808` |
| `0x8022faac` | `Ply_GetFirst20sItemCount(player)` | `+0x804` |
| `0x8022fdc0` | `Ply_GetFastestPillarBreakTime(player)` | `+0x830` |
| `0x8022f418` | `Ply_GetCpuMachineBreakCount(player)` | `+0x4b4` |
| `0x8022f46c` | `Ply_GetFireworkKOCount(player)` | `+0x4b8` |
| `0x8022f4c0` | `Ply_GetGoldSpikeKOCount(player)` | `+0x4bc` |
| `0x8022f514` | `Ply_GetSensorBombKOCount(player)` | `+0x4c0` |
| `0x80230838` | `Ply_GetSkyRingCount(player)` | `+0x7ec` |
| `0x80230be8` | `Ply_GetWaterwheelRideCount(player)` | `+0x820` |
| `0x80230420` | `Ply_GetHighPlainsHoleCount(player)` | `+0x834` |
| `0x80230474` | `Ply_GetSuperJumpBuildingCount(player)` | `+0x838` |
| `0x80230240` | `Ply_GetGrindRailCraterFlag(player)` | `+0x850` |
| `0x8022de74` | `Ply_GetAllOffMachinesValue(player)` | `+0x840` |
| `0x8022ed50` | `Ply_GetCopyChanceBombFlag(player)` | `+0x37a` bit3 |
| `0x8022eda8` | `Ply_GetCopyChanceSleepFlag(player)` | `+0x37a` bit5 |
| `0x8022ec34` | `Ply_GetRivalsDamagedCount(player)` | via `zz_80231cec_` |

(`Ply_GetItemCollectNum/Total`, `Ply_GetBoxCollectTotal`, `Ply_AddDeath`,
`Ply_MarkLegendaryMachineAssembled`, `Ply_UnkUpdate` also named.) The distinct-
rivals-damaged helper at `0x80231cec` (backing `Ply_GetRivalsDamagedCount`)
remains `zz_80231cec_` in the map — the one unnamed symbol referenced by this
doc.

## Open questions / to expand

- **`GameData+0xA94` discriminant identity.** Behaviorally mapped (7=Drag,
  8=AirGlider, 9=TargetFlight, 11=HighJump, 13=Melee, 14=Destruction,
  18=VSKingDedede; gaps at 10/12/15–17), but it is neither `StadiumGroup` nor
  `StadiumKind`. Watch `+0xA94` vs `+0x5AD` live across each stadium to label it.
- **`Gm_StadiumIsAvailable` semantics** (cells 0x0C/0x0D): is "available" =
  "played at least once" or "unlocked/selectable"? Confirm live — it changes
  whether 0x0C/0x0D really count *distinct modes played*.
- **`+0x840` all-off-machines value** (0x4a): condition `!= -1 && <= 60` is solid;
  the field's exact semantics (timer? min-on-machine count?) need a live watch.
- **`0x800ee93c(2)` meteor count** (0x35): reads a global event-count table at
  index 2; confirm idx 2 = the meteor event specifically.
- **Distance bucket flag** (`stat+0x754`) and the physical split of
  `+0x60c`/`+0x610` — live race capture would pin them. Miles math itself is exact.
- **`+0x5e4`/`+0x5e8` drive-time split** — only the sum (drive frames) is
  confirmed; individual component meaning is partial.
- **Unlabeled yakumono indices** — `0x15,0x16,0x19,0x1a,0x1b,0x1c,0x1e,0x1f,
  0x21,0x24,0x27` have producers but no checklist consumer; likely other-stage
  destructibles.

## Extend the same mapping to Air Ride / Top Ride (not yet done)

This pass mapped City Trial (mode 2) only. The equivalent full
clear_kind → condition map for **Air Ride (mode 0)** and **Top Ride (mode 1)**
is still open. Each mode has its own 120-cell `clear[]` and its own evaluators;
the *measurement* layer (the `Ply_Get*` getters and the yakumono break-bucket
array documented above) is **mode-agnostic and already mapped**, so AR/TR cells
that reuse those stats are partly done for free — the work is tracing the
mode-0/1 evaluator functions and their per-mode accumulator slots.

**Ground truth:** `docs/checklist-mappings.csv` — Air Ride rows (clear byte base
`0x805367BC`, so `clear_kind = addr − 0x805367BC`) and Top Ride rows (its own
base). Same cross-reference method as CT: find each cell's English objective,
then locate the `SetNewUnlock(mode, k)` call site that sets it.

**Air Ride — known leads (from this pass's incidental finds):**
- Evaluators already named: `AirRide_CheckObjectivesPerFrame` (`0x8004a7f0`) and
  `AirRide_CheckFreeRunTimeObjectives` (`0x8004d248`). Note AR uses a **per-frame**
  evaluation model, unlike CT's per-game finalization — the accumulation pattern
  will differ.
- AR checker dispatch table at `0x80497618` (slot 24) with a clear_kind array at
  `0x80497674`, driven by dispatcher `zz_8004aa58_` (`0x8004aa58`, still `zz_`).
- Two AR yakumono consumers found while tracing CT: `zz_8004c8c4_` (`0x8004c8c4`)
  → AR cell **0x66** "FROZEN HILLSIDE: split 20 ice platforms"
  (`Ply_GetYakumonoBreakCount(player, 0x20) > 0x13`); `zz_8004c9d0_`
  (`0x8004c9d0`) → AR cell **0x67** "SKY SANDS: break all coral"
  (`Ply_GetYakumonoBreakCount(player, 0x18) == stage_total`, via single-offset
  getter `zz_8022fd48_` @`0x8022fd48` reading `+0x643`). These confirm the
  yakumono array is shared across modes (index meaning is per-stage).

**Top Ride — starting points (less explored):**
- No TR evaluator located yet. Start from the dispatcher
  `ClearChecker_CheckForNewUnlocks` (`0x8004a1a4`) and grep `SetNewUnlock`
  (`0x8004a054`) call sites for `mode == 1`. The TR item/state subsystem
  (`topride-system.md`, `topride-item-system.md`, `topride-kirby-states.md`)
  likely supplies the stat sources.

**Per-mode accumulator slots:** CT's records block is
`GameData_CityTrialClear[1]` via `gmGetClearcheckerType2Ptr` (`0x8000774c`,
returns `0x80536980`). AR/TR will have analogous second-slot accumulators —
identify the per-mode pointer before mapping cumulative cells.

Suggested approach: same fan-out as the CT pass — one subagent per evaluator
function plus the shared dispatch table, cross-referencing the CSV.

## Evaluator family (named)

| Address | Name |
|---------|------|
| `0x8004db74` | `CityTrial_CheckForNewUnlocks` |
| `0x8004e660` | `CityTrial_CheckFreeRunObjectives` |
| `0x8004e748` | `CityTrial_CheckStadiumPlayedObjectives` |
| `0x8004e810` | `CityTrial_CheckStadiumScoreObjectives` |
| `0x8004e998` | `CityTrial_CheckStadiumResultObjectives` |
| `0x8004f03c` | `CityTrial_CheckStadiumKOObjectives` (DD/Melee KO cells) |
| `0x8004a7f0` | `AirRide_CheckObjectivesPerFrame` |
| `0x8004d248` | `AirRide_CheckFreeRunTimeObjectives` |
| `0x8004a1a4` | `ClearChecker_CheckForNewUnlocks` (dispatcher) |
| `0x8022ebdc` | `ClearChecker_CheckJustUnlocked_CityTrial_RivalDamage10Sec` |
| `0x8022f648` | `Ply_AddDeath` (unified KO-event recorder) |
| `0x80231198` | `Ply_MarkLegendaryMachineAssembled` |
| `0x80231340` | `Ply_UnkUpdate` (per-frame distance accumulator) |

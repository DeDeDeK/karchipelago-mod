# Checklist Stat Tracking (`plclearcheckerlib`)

How the game accumulates the per-player gameplay stats that drive checklist
completion. This is the *measurement* layer that sits underneath the clear-bit
storage described in `clearchecker-system.md`: this doc covers **what counts and
where the running totals live**, the sister doc covers **how a completed
condition is stored and detected**.

The game-side source file (from the assert strings, e.g.
`s_plclearcheckerlib_c_804b4cdc`) is `plclearcheckerlib.c` — the "player
clear-checker library." (Top Ride's evaluators instead live in
`a2d_gamesession.cpp`.) **All 120 cells
of all three modes (City Trial mode 2, Air Ride mode 0, Top Ride mode 1) are
mapped to a game-side condition** — City Trial in the master table below, Air
Ride and Top Ride in their own sections near the end. The few remaining
unknowns are collected in the two "Open questions" sections; most need a
live-only check of values already pinned statically.

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

The name notwithstanding, it returns the whole record, not just the item array.
This record is the `PlayerStats stat_record` member of `PlayerData` (at
`PlayerData+0xB0`) in `game.h`; the offsets below are record-relative.
Known fields (offsets relative to `base`):

| Offset | Type | Meaning | Getter | Drives |
|--------|------|---------|--------|--------|
| `+0x37a` | u8 bits | Copy-Chance ability flags: bit3 = got Bomb, bit5 = got Sleep | `0x8022ed50` (bomb) / `0x8022eda8` (sleep) | 0x46 / 0x47 |
| `+0x37c` | int[26] | Per-MachineKind change counter; sum = total Air Ride machine changes | `0x8022f19c` (sums all 26) | 0x06 |
| `+0x4b4` | int | KO-by-cause: CPU machine broken (written by `Ply_AddDeath` on cause byte) | `0x8022f418` | 0x4d |
| `+0x4b8` | int | KO-by-cause: Firework | `0x8022f46c` | 0x60 |
| `+0x4bc` | int | KO-by-cause: Gold Spike | `0x8022f4c0` | 0x5f |
| `+0x4c0` | int | KO-by-cause: Sensor Bomb | `0x8022f514` | 0x5e |
| `+0x4c4` | u16 | **Vehicle-bust bitfield** (MSB-first, bit `15−idx`). idx 0..7 → cells 0x6f..0x76; entries 8/9 (bits 7/6) are Dragoon↔Hydra mutual busts, never read. | `0x8022f3a4(player,idx)` | 0x6f–0x76 |
| `+0x4c8` | int[] | **Item-collect array**, indexed by `ItemKind` (valid `0..0x44`). 0/1/2 = boxes; 3..0x43 = everything else. | see Item-collect subsystem | many |
| `+0x5e4`, `+0x5e8` | int | Drive-time components (frames): `+0x5e4` = grounded, `+0x5e8` = airborne ("glide"; AR reuses it). Split by `MachineData+0x754`, speed-gated; sum = drive time. | `0x80231510` (sums both) | 0x09–0x0B |
| `+0x5f4` | int | Airborne time, 60 fps frames | `0x802315c0` | 0x18 / 0x1C / 0x22 |
| `+0x604` | int | 20 s round timer: frame countdown seeded to 1200 (`PlData.dat` `plDataCommon`), decremented per live frame. `+0x804` increments only while nonzero. Sibling `+0x608` = 600-frame (10 s) timer (cell 0x49). | — | 0x48 |
| `+0x60c`, `+0x610` | f32 | Distance components: `+0x60c` = grounded, `+0x610` = airborne (split by `MachineData+0x754`). Summed by `0x80231614`, accumulated into `records+0x14` for the "race over N miles" cells. | `0x80231614` (sums both) | 0x00 / 0x01 |
| `+0x62b` | u8[20] | **Yakumono-break bucket array**, valid idx `0x15..0x28`. One counter per destructible-object descriptor stat-index. See dedicated section. | `0x8022fccc(player,idx)` | many |
| `+0x653` | u8 | Valid flag for the `+0x830` pillar timer (also = yakumono idx 0x28 region). | — | 0x33 |
| `+0x7ec` | int | Sky rings flown through | `0x80230838` | 0x39 |
| `+0x804` | int | Items (boxes excluded) picked up while the `+0x604` first-20s guard is set | `0x8022faac` | 0x48 |
| `+0x808` | int | Items whose pickup source tag (`item+0x20`) == 4 → **items stolen from Tac** | `0x8022fa58` | 0x34 |
| `+0x820` | int | Waterwheel rides | `0x80230be8` | 0x3d |
| `+0x830` | int | Fastest huge-pillar break time (frames; valid iff `+0x653`) | `0x8022fdc0` | 0x33 |
| `+0x834` | int | High-plains hole entries | `0x80230420` | 0x40 |
| `+0x838` | int | Super-jump-ramp building landings | `0x80230474` | 0x43 |
| `+0x840` | int | Min consecutive-frame run during which **all** human players were simultaneously off-machine (init −1; live counter `+0x83c`; updater `Ply_UpdateAllOffMachines` (`0x8022df1c`), 2+ humans only). `!= -1 && <= 60` ⇒ unlock. | `0x8022de74` | 0x4a |
| `+0x848` | int | King Dedede KO timestamp (frame of first KO of the KD boss = victim slot 4). 0 = not yet. | `0x8022f568` | 0x2f |
| `+0x84c` | u8 bits | bit0 = damaged Dyna Blade, bit1 = trampled by Dyna Blade, bit7 = damaged a rival within 10 s (`ClearChecker_CheckJustUnlocked_CityTrial_RivalDamage10Sec`, `0x8022ebdc`). | bit7 via `0x8022ebdc` | 0x30 / 0x31 / 0x49 |
| `+0x84d` | u8 bits | bit1 = reached sky garden, bit2 = Hydra assembled, bit3 = Dragoon assembled, bit4 = entered castle chamber, bit5 = used a restoration area. bits2/3 written by `Ply_MarkLegendaryMachineAssembled` (`0x80231198`). | inline | 0x3e/0x77/0x38/0x36 |
| `+0x850` | int | Grind-rail-into-crater flag (`!= 0` ⇒ unlock) | `0x80230240` | 0x42 |
| `+0x854` | u8 bits | bit3 = off machine at timeout, bit4 = on rails at timeout. (`+0x855` bit6 = suppress-yakumono-increment.) | inline | 0x4b / 0x4c |

> **Two distinct arrays, same-looking indices.** `+0x4c8[k]` is the *ItemKind*
> int array (`collect[0x28]` = Energy Drink). `+0x62b+idx` is the *yakumono*
> u8 array (`byte[0x28]` = huge pillars). They are unrelated — don't conflate
> the index spaces.

### KO-event recorder: `Ply_AddDeath` (`0x8022f648`)

The name notwithstanding, it is the unified **KO-event recorder**, run on every KO.
Args: victim = arg0, achiever/killer = `*(arg1+0x1c)`. From a single KO it
writes several of the killer's stat fields:

- kills-by-machine `+0x44c[]`, deaths-by-machine `+0x3e4[]`
- the four KO-by-cause counters `+0x4b4/+0x4b8/+0x4bc/+0x4c0`, selected by the
  victim's cause byte (`victim+7`)
- the **vehicle-bust bitfield** `+0x4c4` via a 10-entry / 8-byte table at
  `0x804B4C68` (see Vehicle-bust section)
- the **King Dedede KO time** `+0x848` (only when victim slot == 4 and the field
  is still 0): `*(+0x848) = current frame`

## Item-collect subsystem

The item-collect array at `+0x4c8`: **`ItemKind` 0/1/2 are the
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
| 0x35 | meteor attacks 3× | FNU | `records-0x2` (Σ global event-count `CityEvent_GetOccurrenceCount(2)`) `>= 3` |
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

Cells 0x0C/0x0D count stadium modes **unlocked**, not played: `Gm_StadiumIsAvailable`
(`0x8000c228`) is purely an unlock check (default-unlock table + checklist reward via
`ClearChecker_CheckUnlocked`), with no "played" bit anywhere — so they fire once ≥10/≥20
of the 24 `StadiumKind`s are unlocked, regardless of whether each was actually played.

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
This block is typed as `CityTrialClearRecords` in `game.h` (the Air Ride analog is
`AirRideClearRecords`, the Top Ride analog the existing `TopRideStats`), reached as
`(CityTrialClearRecords *)((u8 *)gmGetClearcheckerTypeP(GMMODE_CITYTRIAL) + 0xF4)`.

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
| `+0xA94` | u8 | **`city_kind`** (named in `game.h`): City Trial scene/minigame kind. 5 = free-roam City Trial (6 = variant); stadium minigames 7 = Drag Race, 8 = Air Glider, 9 = Target Flight, 11 = High Jump, 13 = Kirby Melee, 14 = Destruction Derby, 18 = VS King Dedede (each family collapsed to one value). Set on stadium load (`0x8004051c`) from `stadium_desc[StadiumKind].city_kind` (first byte of the 6-byte descriptor; siblings → `stage_kind` @0xA97, time @0xA9C). `CityTrial_IsInStadium` (`0x8000ad48`) treats 7–18 as in-stadium. Not `StadiumGroup`/`StadiumKind`. |

ST's jump table is at `0x80497738` (12 × u32, indexed `(city_kind − 7)`).

## The yakumono-break bucket array (`+0x62b`)

This array is **not** the broad event-counter
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

| idx | offset | meaning (active stage) | consumer → clear_kind |
|-----|--------|------------------------|-----------------------|
| 0x17 | 0x642 | Destruction Derby rocks | ST, id 9, `Σ_present > 1` → **0x28** |
| 0x1d | 0x648 | star-pole busts | FNU: `!= 0` → **0x3a**; `records-0x1 Σ > 9` → **0x3b** |
| 0x20 | 0x64b | forest-pitfall cover | FNU: `!= 0` → **0x3f** |
| 0x22 | 0x64d | forest trees | FNU: `Σ_present > 0x34` → **0x45** |
| 0x23 | 0x64e | volcano + high-plains rocks | FNU: `Σ_present > 0x28` → **0x41** |
| 0x25 | 0x650 | volcano-base hole covers | FNU: `Σ_present > 2` → **0x3c** |
| 0x26 | 0x651 | dilapidated houses | FNU: `Σ_present > 0x1d` → **0x44** |
| 0x28 | 0x653 | huge pillars | FNU: `records-0x3 Σ > 4` → **0x32**; `!= 0` + timer → **0x33** |

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
`Ply_UnkUpdate` (`0x80231340`): the magnitude of the frame's displacement is added
to `+0x60c` when the machine action-state class **`MachineData+0x754`** is 0
(grounded / charging / respawn states) or to `+0x610` when it is 1 (launched /
airborne states) — so `+0x60c` = grounded distance, `+0x610` = airborne distance.
The miles math sums both, so it is unaffected by the split. The same
`MachineData+0x754` flag drives the drive-time split (`+0x5e4` / `+0x5e8`). A third
per-frame accumulator at `+0x614` adds the same delta only while
`MachineData+0xbae` bit 5 (a consolidated hit/damage-reaction flag) is set —
distance travelled while taking a hit — and is summed by no getter.

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
| `0x8022ec34` | `Ply_GetRivalsDamagedCount(player)` | via `Ply_CountDistinctRivalsDamaged` |

(`Ply_GetItemCollectNum/Total`, `Ply_GetBoxCollectTotal`, `Ply_AddDeath`,
`Ply_MarkLegendaryMachineAssembled`, `Ply_UnkUpdate` also named.)
`Ply_CountDistinctRivalsDamaged` (`0x80231cec`, backing
`Ply_GetRivalsDamagedCount`) counts the set bits 0–4 of the per-rival
"damaged this game" bitfield at `statbase+0x331` (one bit per opponent slot),
so cell 0x4e fires at 3 distinct rivals. `Ply_GetStatRecordBase` (`0x8022d260`)
is a second accessor returning the same per-player record base as
`Ply_GetItemCollectArray`.

## Open questions / to expand

- **Unlabeled yakumono indices** — `0x15,0x16,0x19,0x1a,0x1b,0x1c,0x1e,0x1f,
  0x21,0x24,0x27` have producers but no checklist consumer; likely other-stage
  destructibles, left unlabeled rather than guessed.
- **`MachineData+0x754` state taxonomy** — the 0/1 action-state class that splits
  the distance (`+0x60c`/`+0x610`) and drive-time (`+0x5e4`/`+0x5e8`) buckets is set
  0 on entry to grounded/charging/respawn states and 1 on entry to launched/airborne
  states; the full enumeration of which states fall
  on each side is not exhaustively mapped.

## Air Ride (mode 0)

Air Ride clear bits are the `clear[]` of the **type-0** `GameClearData` slot
(`gmGetClearcheckerType0Ptr`, `0x8000771c`, base `0x80536740`; `clear[]` at
`+0x7c` → `0x805367BC`, so `clear_kind = addr − 0x805367BC`). Cells are set by
`ClearChecker_SetNewUnlock(0, kind)` (`0x8004a054`), gated by
`ClearChecker_GetKindClear` (`0x8004a130`) returning `(flags & 5) == 0`. Unlike
City Trial's single per-game finalizer, Air Ride runs **three independent entry
points**, all sharing the gate `Checklist_IsCacheValid()==0 &&
!_D_CheckIfReplay() && _DHud_GetUnkFromPKind()==0 && Scene_GetCurrentMajor()==4`:

| Entry point | Addr | Trigger | Owns |
|---|---|---|---|
| `AirRide_CheckObjectivesPerFrame` | `0x8004a7f0` | every frame from `Game_Think` | per-frame / cumulative cells (Table B) |
| `AirRide_CheckRaceFinishObjectives` | `0x8004aa58` | race finish, via `MinorExit_AirRideMachineSelect` | race-finish cells (Table A) + writes the cumulative accumulators + the distance cells |
| `AirRide_DispatchFreeRunObjectives` / `AirRide_DispatchRaceTimeAttackObjectives` | `0x8004a90c` / `0x8004a994` | per-lap from `AirRide_OnFinishRace` (free-run) / `race3D_isFinished` (race) | the per-stage time/lap/distance evaluators |

**Sub-mode discriminant** `Gm_GetAirRideMode()` (`0x8003d5f0`, `GameData+0x35d`,
enum `AIRRIDEMODE_*`) selects which per-stage evaluator runs:

- `== 0` (`AIRRIDEMODE_RACE`) → `AirRide_CheckRaceLapObjectives` (`0x8004d248`),
  the "Air Ride: *stage* finish N laps under T" cells (the normal-race lap
  evaluator, distinct from the Free Run `== 2` path below).
- `== 1` (`AIRRIDEMODE_TIME`) → `AirRide_CheckTimeAttackObjectives` (`0x8004d5d4`)
  — the "Time Attack: *stage* under T" cells.
- `== 2` (`AIRRIDEMODE_FREE`) → `AirRide_CheckFreeRunLapObjectives` (`0x8004d8a8`)
  — the "Free Run: *stage* 1 lap under T" cells.
- the distance cells run from the race-finish path when `Gm_GetCityKind()==1`:
  `AirRide_CheckRaceDistanceObjectives` (`0x8004d454`) — "*stage* race over N
  feet in 2 minutes".

`Gm_GetAirRideMode()` doubles as a player-inclusion gate in the cumulative
accumulators: each sums `PKIND_HMN` (0) players always, plus `PKIND_CPU` (1) players
**only when** the mode is `AIRRIDEMODE_TIME` (1) — so a Time-Attack CPU's laps/etc. fold
into the cross-game totals, while in Race/Free Run only human players are summed.

**gr_kind → stage** (`Stage_GetGrKindFromStageKind`, `0x80261ce8`): 0 = Fantasy
Meadows, 1 = Magma Flows, 2 = Sky Sands, 3 = Checker Knights, 4 = Celestial
Valley, 5 = Machine Passage, 7 = Beanstalk Park, 8 = Frozen Hillside (gr_kind 6
is not used by the checklist). This engine gr_kind is **not** the `AirRideCourse`
menu enum.

### Two handler tables (race-finish + per-frame)

Both drivers walk a function-pointer table paired with a clear_kind byte array;
a handler returns 1 when its objective is met for some player and the driver then
sets that cell. Duplicate handler entries are one function parameterized by the
clear_kind it is passed (the two glide-time thresholds, the four enemy-swallow
types, etc.).

- **Table A** (race finish): handlers `0x804975b8` (30), clear_kinds `0x80497674`.
- **Table B** (per-frame): handlers `0x80497630` (17), clear_kinds `0x80497694`,
  four slots per frame round-robin via the cursor at `GameData+0xa88`.

### Air Ride records / cumulative accumulators

The cross-game accumulators live in the **tail of the type-0 block** (anchored at
`0x80536838` = base `+0xF8`, same layout idea as the City Trial records block),
written once per race finish by `AirRide_CheckRaceFinishObjectives`. Each folds
the per-game `Σ_human` value into the running total with plain `int` adds — **no
saturating caps**, unlike CT's u8/u16 records.

| records | abs | role (per-game source summed) | writer | drives |
|---|---|---|---|---|
| `-0x4` | `0x80536834` | total laps (`Σ GameData+0x868[p]`) | `0x8004ab90` | 0x00 / 0x01 |
| `+0x0` | `0x80536838` | total glide time, frames (`Σ +0x5e8`) | `0x8004acf4` | 0x02 / 0x03 |
| `+0x4` | `0x8053683C` | total enemies defeated (`Σ (+0xe0)+(+0x6a0)`) | `0x8004ae50` | 0x04 / 0x05 |
| `+0x8` | `0x80536840` | goals reached (`# humans finished`) | `0x8004b1d0` | 0x0a |
| `+0xc` | `0x80536844` | enemies swallowed (`Σ +0x6a0`) | `0x8004b2b8` | 0x0e |
| `+0x10` | `0x80536848` | exhaled-star defeats (`Σ defeat[0xf]+defeat[0x15]`) | `0x8004b404` | 0x0f |
| `+0x14` | `0x8053684C` | Quick-Spin defeats (`Σ defeat[0x10]`) | `0x8004b814` | 0x1f |
| `+0x18` | `0x80536850` | Quick-Spin rival hits (`Σ hit[0x10]`) | `0x8004b968` | 0x20 |
| `+0x1c` | `0x80536854` | played-courses bitmask (`OR 1<<gr_kind`) | `0x8004bb1c` | 0x54 |

### Air Ride per-player stat fields

On the shared per-player struct (`base = player*0x90c + 0x8055AAA0`, accessor
`Ply_GetItemCollectArray` `0x8022d248`). These extend the City Trial table near
the top of this doc; the `+0x84c`/`+0x854`/`+0x855` bytes reuse bytes also listed
for CT, with Air-Ride-specific bits.

| Offset | Type | Meaning | Getter | Drives |
|---|---|---|---|---|
| `+0x74` | int[0x1b] | rival-hit-by-method; idx 0x10 = Quick Spin | `0x8022ea98` | 0x20 |
| `+0xe0` | int | enemies defeated (non-swallow) | `0x8022eb88` | 0x04 / 0x05 |
| `+0xe4` | int[0x1b] | enemy-defeat-by-method; idx 0xf/0x15 = exhaled star, 0x10 = Quick Spin | `0x8022eb10` | 0x0f / 0x1f |
| `+0x5e8` | int | glide time, frames (AR uses this single field, not the CT drive-time pair) | `0x8023156c` | 0x02 / 0x03 |
| `+0x654` | u8[13] | volcano-rail used-bitmask (Magma) | `0x802300b4` | 0x6e |
| `+0x661` | u8[63] | boost-panel used-bitmask (Magma) | `0x80230294` | 0x70 |
| `+0x6a0` | int | enemies swallowed | `0x802306d4` | 0x0e, 0x04/0x05 |
| `+0x6a8` | int[ ] | per-ACTORID swallow counter; one enemy = base+T1+T2 tiers (stride 0x18) summed | `0x8023077c` | 0x06–0x09, 0x5f/0x62/0x65 |
| `+0x7c8` | int | consecutive garbage-enemy swallows (no copy) | `0x802307e4` | 0x10 |
| `+0x7d0` | int | enemies swallowed this race | `0x80230728` | 0x5f / 0x62 / 0x65 |
| `+0x7d4` | int | sword swings this race | `0x8022e390` | 0x1c |
| `+0x7d8` | int | Tornado-Kirby KOs | `0x8022e3e4` | 0x1e |
| `+0x7f0` | int | Copy-Chance-wheel-on-tree count (Celestial) | `0x8023088c` | 0x74 |
| `+0x7f4` | int | spin-panel uses (Checker Knights) | `0x802308e0` | 0x63 |
| `+0x7fc` | int | trapdoor opens (Sky Sands) | `0x8022e438` | 0x6a |
| `+0x800` | int | walls broken (Checker Knights) | `0x8023098c` | 0x64 |
| `+0x810` | int | cliff drops this game | `0x802309e0` | 0x5e |
| `+0x814` | int | quicksand entries (Sky Sands) | `0x80230a34` | 0x68 |
| `+0x81c` | int | laps without riding the Ferris wheel (Beanstalk) | `0x8022e838` | 0x61 |
| `+0x82c` | int | positions gained over the final lap = (rank at prev lap boundary) − (rank at final-lap-start boundary); written at finish from the per-lap rank tracker `AirRide_TrackLapRank` (`0x8022e88c`) | `0x8022e48c` | 0x55 |
| `+0x844` | int | cannon simultaneous-launch count (Machine Passage) | `0x8022dec8` | 0x6b |
| `+0x84c` bit2 | bit | bumped a flaming dragon (Magma) | `0x80230ae0` | 0x6f |
| `+0x84c` bit5 | bit | completed a lap ≥20 mph (Fantasy Meadows) | `0x8022e788` | 0x60 |
| `+0x84c` bit6 | bit | touched a wall (Machine Passage; cell wants it **clear**) | `0x80230a88` | 0x6d |
| `+0x84d` bit6 | bit | lap-time last two digits equal | `0x8022e7e0` | 0x53 |
| `+0x854` bit0 / bit1 | bits | finished with Needle / Fire | `0x8022e2e0` / `0x8022e288` | 0x5b / 0x5a |
| `+0x854` bit2/5/6/7 | bits | finished with Sleep / while damaged / flying / spinning | `0x8022e230` / `0x8022e0d0` / `0x8022e128` / `0x8022e078` | 0x59 / 0x57 / 0x58 / 0x56 |
| `+0x855` bit7 | bit | finished with Wing | `0x8022e338` | 0x5c |

The yakumono-break array `+0x62b` is shared with City Trial; Air Ride adds two
consumers: idx 0x18 = Sky Sands coral (single-offset getter `0x8022fd48` reading
`+0x643`, compared against the live stage spawn-count `0x800f7db0(0x18)`) → 0x67,
and idx 0x20 = Frozen Hillside ice platforms → 0x66. Placement comes from
`GameData` byte arrays, not the stat struct: `GameData+0x848[p]` (finish rank,
`0 = 1st`, getter `0x80009534`), `GameData+0x852[p]` (finished flag, `0x8000979c`),
`GameData+0x868[4p]` (lap count, `0x8000b1ec`).

The `+0x854` / `+0x855` finish-state bits are all set by one writer `AirRide_RecordFinishStats` (`0x8022e4e0`),
called per finishing player from `race3D_isFinished` the moment they cross the line; the
ability bits (Needle / Fire / Sleep / Wing = `+0x854` bit0/1/2 + `+0x855` bit7) are keyed
on the rider's live `RiderData.copy_kind` (`+0x454`; CopyKind 6 / 0 / 2 / 10). The
1st-place requirement for cells 0x56–0x5c is applied by the cell checker, not this writer.
The per-machine cells gate on `Ply_GetVehicleKind` (`0x8022c910`) →
`Machine_EncodeVehicleKind` (`0x801c85a8`): `mk` for non-bikes, `mk+0x13` for bikes
(`VCKIND_*` enum + `MachineKind_Names[]` in `machine.h`).

### Master cell table — gameplay & cumulative cells (Air Ride, mode = 0)

The per-stage *time/distance* cells are tabulated separately below; everything
else is here. "& 1st" = the per-player finish-rank check `GameData+0x848[p]==0`.

| ck | Objective | Eval | Condition |
|----|-----------|------|-----------|
| 0x00 | race over 100 laps | A-finish | `records-0x4 (Σ laps) >= 100` |
| 0x01 | race over 300 laps | A-finish | `records-0x4 >= 300` |
| 0x02 | glide > 30 min | B-frame | `Σ_human +0x5e8 + records+0x0 >= 108000` (30 min) |
| 0x03 | glide > 1 hour | B-frame | `>= 216000` |
| 0x04 | defeat > 300 enemies | B-frame | `Σ (+0xe0)+(+0x6a0) + records+0x4 >= 300` |
| 0x05 | defeat > 1000 enemies | B-frame | `>= 1000` |
| 0x06 | swallow Chilly 3× & 1st | A-finish | `Σ_tiers +0x6a8[ACTORID 0x0b] >= 3` & 1st |
| 0x07 | swallow Plasma Wisp 3× & 1st | A-finish | `+0x6a8[0x0d] tiers >= 3` & 1st |
| 0x08 | swallow Sword Knight 3× & 1st | A-finish | `+0x6a8[0x05] tiers >= 3` & 1st |
| 0x09 | swallow Wheelie 3× & 1st | A-finish | `+0x6a8[0x08] tiers >= 3` & 1st |
| 0x0a | reach goal 3× (non–Free Run) | A-finish | `records+0x8 (goals) >= 3` |
| 0x0e | swallow 200+ enemies | B-frame | `Σ +0x6a0 + records+0xc >= 200` |
| 0x0f | defeat 100+ with exhaled stars | B-frame | `Σ (+0xe4[0xf]+[0x15]) + records+0x10 >= 100` |
| 0x10 | swallow 5 consecutive garbage enemies & 1st | A-finish | `+0x7c8 >= 5` & 1st |
| 0x1c | sword challenge: swing exactly 10× & 1st | A-finish | `+0x7d4 == 10` & 1st |
| 0x1e | tornado challenge: 15+ KOs & 1st | A-finish | `+0x7d8 >= 15` & 1st |
| 0x1f | 10+ Quick Spin defeats | B-frame | `Σ +0xe4[0x10] + records+0x14 >= 10` |
| 0x20 | hit 20+ rivals with Quick Spin | B-frame | `Σ +0x74[0x10] + records+0x18 >= 20` |
| 0x53 | lap time's last two digits equal | B-frame | any human `+0x84d bit6` |
| 0x54 | race all standard courses | B-frame | `records+0x1c` mask has all of bits 0,1,2,3,4,5,7,8 |
| 0x55 | start final lap 4th, win | A-finish | `+0x82c >= 3` & 1st |
| 0x56 | cross finish line spinning & 1st | A-finish | `+0x854 bit7` & 1st |
| 0x57 | finish 1st while taking damage | A-finish | `+0x854 bit5` & 1st |
| 0x58 | finish 1st flying through the air | A-finish | `+0x854 bit6` & 1st |
| 0x59 | finish 1st with Sleep | A-finish | `+0x854 bit2` & 1st |
| 0x5a | finish 1st with Fire | A-finish | `+0x854 bit1` & 1st |
| 0x5b | finish 1st with Needle | A-finish | `+0x854 bit0` & 1st |
| 0x5c | finish 1st with Wing | A-finish | `+0x855 bit7` & 1st |
| 0x5e | drop from cliffs 3× in one game | B-frame | any human `+0x810 >= 3` |
| 0x5f | FANTASY MEADOWS: swallow >20 & 1st | A-finish | gr 0, `+0x7d0 >= 20` & 1st |
| 0x60 | FANTASY MEADOWS: lap ≥20 mph | B-frame | gr 0, any human `+0x84c bit5` |
| 0x61 | BEANSTALK: 3 laps without Ferris wheel | B-frame | gr 7, any human `+0x81c >= 3` |
| 0x62 | BEANSTALK: swallow >20 & 1st | A-finish | gr 7, `+0x7d0 >= 20` & 1st |
| 0x63 | CHECKER KNIGHTS: spin panels 7× & 1st | A-finish | gr 3, `+0x7f4 >= 7` & 1st |
| 0x64 | CHECKER KNIGHTS: break ≥2 walls & 1st | A-finish | gr 3, `+0x800 >= 2` & 1st |
| 0x65 | CHECKER KNIGHTS: swallow >20 & 1st | A-finish | gr 3, `+0x7d0 >= 20` & 1st |
| 0x66 | FROZEN HILLSIDE: split 20 ice platforms & 1st | A-finish | gr 8, `byte[0x20] >= 20` & 1st |
| 0x67 | SKY SANDS: break all coral & 1st | A-finish | gr 2, `byte[0x18] == stage_total && != 0` & 1st |
| 0x68 | SKY SANDS: enter quicksand 3× & 1st | A-finish | gr 2, `+0x814 == 3` & 1st |
| 0x6a | SKY SANDS: open trapdoor exactly 3× & 1st | A-finish | gr 2, `+0x7fc == 3` & 1st |
| 0x6b | MACHINE PASSAGE: cannon-launch 3 chars at once | B-frame | gr 5, any human `+0x844 >= 3` |
| 0x6d | MACHINE PASSAGE: 1st without touching walls | A-finish | gr 5, `+0x84c bit6 == 0` & 1st |
| 0x6e | MAGMA FLOWS: use all volcano rails & 1st | A-finish | gr 1, `+0x654` mask == stage rail mask & 1st |
| 0x6f | MAGMA FLOWS: bump a flaming dragon | B-frame | gr 1, any human `+0x84c bit2` |
| 0x70 | MAGMA FLOWS: use all Boost Panels & 1st | A-finish | gr 1, `+0x661` mask == stage panel mask & 1st |
| 0x72 | CELESTIAL VALLEY: ride both bridge railings | B-frame | gr 4, all required rail-segment bits set in `+0x654` |
| 0x74 | CELESTIAL VALLEY: Copy Chance on top of the tree | B-frame | gr 4, any human `+0x7f0 != 0` |
| 0x18 | fill 100 checklist blocks (meta) | PU | `Checklist_ProcessUnlock` auto-unlock (AR analogue of CT 0x37) |

### Per-stage time / distance cells (Air Ride)

Distance is `GameData+0xa4c[p]` (f32 metres); feet = `metres / 0.3047879934`
(`FLOAT_805de928`), and the distance floats (e.g. `0x805de910`=4500.0) are the
**feet** target. Race/Time-Attack finish time is `GameData+0x8b8[p]` (1/60 s
ticks, getter `0x800097d0`); Free Run uses its own slot `GameData+0x8cc[p]`
(getter `0x80009fb8`). All compare `time != 0 && time <= threshold` (frames).

**Distance — "race over N feet in 2 minutes"** (`0x8004d454`, gate
`Gm_GetRaceTimeSeconds()*60 == 7200`, i.e. exactly 2:00.00):

| ck | stage | gr | feet |
|----|-------|----|------|
| 0x0b | Checker Knights | 3 | 5500 |
| 0x0c | Magma Flows | 1 | 4800 |
| 0x14 | Fantasy Meadows | 0 | 4500 |
| 0x15 | Celestial Valley | 4 | 6000 |
| 0x16 | Sky Sands | 2 | 4000 |
| 0x17 | Frozen Hillside | 8 | 5300 |
| 0x21 | Machine Passage | 5 | 4500 |
| 0x22 | Beanstalk Park | 7 | 5500 |

**Race lap-finish — "Air Ride: *stage* finish N laps under T"** (`0x8004d248`,
2 cells/stage, no machine gate):

| ck | stage | time | ck | stage | time |
|----|-------|------|----|-------|------|
| 0x23 | Fantasy Meadows 3 laps | 01:03 | 0x19 | Fantasy Meadows 3 laps | 01:20 |
| 0x27 | Magma Flows 2 laps | 02:01 | 0x0d | Magma Flows 2 laps | 02:20 |
| 0x25 | Sky Sands 2 laps | 01:45 | 0x1b | Sky Sands 2 laps | 02:05 |
| 0x2a | Checker Knights 2 laps | 02:40 | 0x13 | Checker Knights 2 laps | 03:05 |
| 0x24 | Celestial Valley 2 laps | 01:56 | 0x1a | Celestial Valley 2 laps | 02:20 |
| 0x29 | Machine Passage 2 laps | 01:48 | 0x12 | Machine Passage 2 laps | 02:10 |
| 0x28 | Beanstalk Park 2 laps | 01:56 | 0x11 | Beanstalk Park 2 laps | 02:18 |
| 0x26 | Frozen Hillside 2 laps | 01:56 | 0x1d | Frozen Hillside 2 laps | 02:20 |

**Time Attack — "Time Attack: *stage* under T"** (`0x8004d5d4`; per stage two
open cells + one machine-gated cell):

| stage | open cells (time) | machine cell (machine, time) |
|-------|-------------------|------------------------------|
| Fantasy Meadows | 0x2b 01:12, 0x2c 01:00 | 0x5d Slick Star 01:05 |
| Magma Flows | 0x33 03:20, 0x34 03:04 | 0x73 Shadow Star 03:15 |
| Sky Sands | 0x2f 03:10, 0x30 02:40 | 0x6c Wagon Star 02:40 |
| Checker Knights | 0x39 04:30, 0x3a 04:00 | 0x77 Warpstar 03:55 |
| Celestial Valley | 0x2d 03:20, 0x2e 02:56 | 0x69 Jet Star 02:58 |
| Machine Passage | 0x37 03:10, 0x38 02:48 | 0x76 Rex Wheelie 02:50 |
| Beanstalk Park | 0x35 03:10, 0x36 02:55 | 0x75 Rocket Star 03:00 |
| Frozen Hillside | 0x31 03:14, 0x32 02:50 | 0x71 Turbo Star 03:10 |

**Free Run — "Free Run: *stage* 1 lap under T"** (`0x8004d8a8`; two open cells +
one machine-gated cell per stage):

| stage | open cells (time) | machine cell (machine, time) |
|-------|-------------------|------------------------------|
| Fantasy Meadows | 0x3b 00:24, 0x3c 00:21 | 0x3d Wagon Star 00:23 |
| Magma Flows | 0x47 01:10, 0x48 01:01 | 0x49 Turbo Star 01:02 |
| Sky Sands | 0x41 01:05, 0x42 00:53 | 0x43 Bulk Star 01:05 |
| Checker Knights | 0x50 01:35, 0x51 01:20 | 0x52 Rocket Star 01:25 |
| Celestial Valley | 0x3e 01:10, 0x3f 00:57 | 0x40 Slick Star 01:02 |
| Machine Passage | 0x4d 01:05, 0x4e 00:56 | 0x4f Swerve Star 00:57 |
| Beanstalk Park | 0x4a 01:07, 0x4b 00:58 | 0x4c Winged Star 00:58 |
| Frozen Hillside | 0x44 01:10, 0x45 00:58 | 0x46 Formula Star 01:10 |

## Top Ride (mode 1)

Top Ride clear bits are the `clear[]` of the **type-1** slot
(`gmGetClearcheckerType1Ptr`, `0x8000772c`, base `0x80536858`; `clear[]` at
`+0x7c` → `0x805368D4`, so `clear_kind = addr − 0x805368D4`). Top Ride is the odd
mode out: its evaluators live in `a2d_gamesession.cpp` (not `plclearcheckerlib`),
and it sets cells with **`ClearChecker_SetNewUnlockSilent`** (`0x80049fcc`) —
sets the clear bit without the unlock fanfare — rather than the loud
`SetNewUnlock`. The City Trial / Air Ride paths never use `SetNewUnlockSilent`,
so `SetNewUnlock` has no mode-1 caller.

Three evaluator functions cover all 120 cells, all gated by
`ClearChecker_GetKindClear` re-set check:

| Evaluator | Addr | Trigger | Owns |
|---|---|---|---|
| `TopRide_CheckPerCourseObjectives` | `0x802b88f4` | per-frame, `TopRide_FielderUpdate` / `TopRide_CheckForNewUnlocks` (`0x802ac850`) | per-course objective cells + Time-Attack cells |
| `TopRide_CheckPerCourseObjectives_B` | `0x802b7dac` | same | per-course "100 laps" + Free-Run lap cells + several cumulative cells |
| `TopRide_CheckSessionFinalizeObjectives` | `0x802b777c` | end of session, `Singleton_GlobalDestructor` | cross-session cumulative counters + the "5 s faster than #2" cells |

`TopRide_CheckSessionFinalizeObjectives` skips all accumulation if any human kirby
did not finish: it walks the `ObjCollect<KirbyHandle>` list (head at `0x805ddab8`) and,
per kirby, checks vtable `+0x34` (is-human) and `+0x2c` (finished); a human-not-finished
sets a local abort flag that guards every counter increment and cell check.

**Stat sources.** `TopRide_GetStats()` (`0x80287040`) returns the persistent TR
stat / records block at **`0x8053694C`** (= `gmGetClearcheckerType1_2Ptr`,
`0x8000773c`; it is the tail of the type-1 clear block, base `+0xF4`). This is
where TR's cumulative counters live cross-session — there is no separate "[1]"
slot as in AR/CT. The per-race session/`GameHistory` struct (passed in,
`= *(*(KirbyMgr+4) + 0xa04)`) supplies per-game values. A 7-entry course
descriptor table at **`0x8048a028`** (stride 0x28) supplies per-course clear_kinds
and thresholds.

**Course index** (`session+0x8`): 0 = Grass, 1 = Sand, 2 = Sky, 3 = Fire,
**4 = Light, 5 = Water, 6 = Metal**. The physical course id swaps Light/Water
relative to the clear_kind ordering (Grass, Sand, Sky, Fire, Water, Light, Metal).

### TR stat / records fields (`TopRide_GetStats()` base `0x8053694C`)

| Offset | Type | Meaning | Drives |
|---|---|---|---|
| `+0x01` | u8 | cumulative goals crossed (all modes) | 0x00 |
| `+0x03` | u8 | multiplayer races played | 0x02 / 0x03 |
| `+0x04` | u8 | Time-Attack goals crossed | 0x05 |
| `+0x06` | u16 | session lap count | 0x01 |
| `+0x08` | u8 | Free-Run lap count | 0x04 |
| `+0x09..+0x0f` | u8[7] | per-course 1st-place count (also drives the "all courses" cell 0x06) | base+3, 0x06 |
| `+0x10..+0x16` | u8[7] | per-course lap-count array | base+5 (100 laps) |
| `+0x1b` | u8 | Spinner items collected | 0x13 |
| `+0x1d` | u8 | Invincible Candy collected | 0x14 |
| `+0x27` | u8 | Walky items collected | 0x15 |
| `+0x2d` | u8 | per-course "1st with no items" bitfield (all 7 = `0x7f`) | 0x09 |
| `+0x2e` | u8 | per-course "1st with no Boost" bitfield (`0x7f`) | 0x0c |
| `+0x31` | u8 | Light grind-rail rides (cumulative) | 0x4d |

Session / `GameHistory` fields: `+0x04` mode (0 course / 1 time-attack / 2 free
run), `+0x06` was-multiplayer, `+0x08` course index, `+0x0c` Sand ant-doom drops
this game, `+0x0e` distinct item types, `+0x10` items collected this session,
`+0x18` best lap time (frames). Per-effect KO counters live on global singleton
managers, reached via RTTI cast: the **EmberMgr** singleton (Fire item) `+0x18`, pointer
at `0x805ddb5c` (r13+0xa7c); the **GrenadeMgr** ("Bomb") singleton `+0x1c`, pointer at
`0x805ddb64` (r13+0xa84). Course-policy
sub-objects hold the gimmick counters (Grass tree-bombs, Sand ant-doom, Fire
eruptions, etc.).

### TR cross-course / cumulative cells

| ck | Objective | Eval | Condition |
|----|-----------|------|-----------|
| 0x00 | cross the goal 20+ times | finalize | `stats+0x1 >= 20` |
| 0x01 | race over 300 laps | per-course-B | `stats+0x6 (u16) >= 300` |
| 0x02 | 10+ multiplayer races | finalize | `stats+0x3 >= 10` |
| 0x03 | 50+ multiplayer races | finalize | `stats+0x3 >= 50` |
| 0x04 | Free Run: 100+ laps | per-course-B | `stats+0x8 >= 100` |
| 0x05 | Time Attack: cross goal 30+ times | finalize | `stats+0x4 >= 30` |
| 0x06 | take 1st on all courses | per-course | all of `stats+0x9..+0xf != 0` |
| 0x07 | one lap without hitting a wall & 1st | per-course | clean-lap flag set |
| 0x08 | 20+ Quick Spins in one lap & 1st | per-course | quick-spin count `>= 20` |
| 0x09 | complete all courses without items | per-course-B | `stats+0x2d == 0x7f` |
| 0x0a | finish 1st on all courses using no items | per-course-B | all 7 per-course "no-items 1st" cells clear |
| 0x0b | collect 500+ items | per-course-B | `session+0x10 (u16) >= 500` |
| 0x0c | finish all courses without Boost | per-course-B | `stats+0x2e == 0x7f` |
| 0x0d | finish 1st on all courses without Boost | per-course-B | all 7 per-course "no-Boost 1st" cells clear |
| 0x0e | get the same item 3× in one race | per-course | item-repeat flag set |
| 0x0f | take 1st while doing a Quick Spin | per-course | result code = Quick Spin |
| 0x10 | take 1st while holding the Hammer | per-course | result code = Hammer |
| 0x11 | finish 1st with a 1-lap gap to #2 | per-course | lap delta to runner-up `<= 1` |
| 0x12 | finish 1st with a 2-lap gap to #2 | per-course | lap delta `>= 2` |
| 0x13 | 20+ Spinner items | per-course-B | `stats+0x1b >= 20` |
| 0x14 | 20+ Invincible Candy | per-course-B | `stats+0x1d >= 20` |
| 0x15 | 20+ Walky items | per-course-B | `stats+0x27 >= 20` |
| 0x16 | torch 3+ rivals with one Fire item | per-course-B | EmberMgr+0x18 `>= 3` |
| 0x17 | 3+ rivals sailing with one Buzz Saw | per-course | Buzz-Saw KO count `>= 3` |
| 0x18 | hit enemies 3× with Bomb items | per-course-B | Bomb-mgr+0x1c `>= 3` |
| 0x19 | 18+ different item types | per-course-B | `session+0xe >= 18` |
| 0x77 | fill 100 checklist blocks (meta) | PU | `Checklist_ProcessUnlock` auto-unlock |

### TR per-course cells

Each course is a contiguous block based at its "1st with no items" cell. Within a
block the offsets are: **+0** no items, **+1** no Boost, **+2** CPU level 5, **+3**
take 1st 10×, **+4** finish N laps under T, **+5** race 100+ laps, then the
course-specific gimmick cell(s), and last the "finish 1st 5 s faster than #2"
cell. Conditions: +0 reads the no-items-used flag (clear); +1 the no-Boost flag;
+2 a CPU at level value 4 (= "level 5") finished; +3 `stats+0x9[course] >= 10`;
+4 player laps == course lap target and finish time `< desc+0x4`; +5
`stats+0x10[course] >= 100`; the "5 s faster" cell tests `2nd_time − 1st_time >=
300` frames.

| course | base | no-items / no-Boost / CPU5 / 10× / laps<T / 100laps | gimmick cells | 5 s faster |
|--------|------|----------------------------------------------------|---------------|------------|
| Grass | 0x1a | 0x1a / 0x1b / 0x1c / 0x1d / 0x1e (7 laps 00:43) / 0x1f | 0x20 hit 5 Dash Panels, 0x21 drop 30 tree bombs | 0x22 |
| Sand | 0x23 | 0x23 / 0x24 / 0x25 / 0x26 / 0x27 (7 laps 00:52) / 0x28 | 0x29 catch worm 3×, 0x2a ant-doom 50×, 0x2b ant-doom 20× one game | 0x2c |
| Sky | 0x2d | 0x2d / 0x2e / 0x2f / 0x30 / 0x31 (6 laps 01:02) / 0x32 | 0x33 hit Isle Knob 5×, 0x34 1st without Jump Plate | 0x35 |
| Fire | 0x36 | 0x36 / 0x37 / 0x38 / 0x39 / 0x3a (6 laps 00:53) / 0x3b | 0x3c huge eruption 3×, 0x3d 1st holding Fire item | 0x3e |
| Water | 0x3f | 0x3f / 0x40 / 0x41 / 0x42 / 0x43 (5 laps 01:02) / 0x44 | 0x45 enter falls 5× | 0x46 |
| Light | 0x47 | 0x47 / 0x48 / 0x49 / 0x4a / 0x4b (6 laps 00:43) / 0x4c | 0x4d grind rail 50×, 0x4e grind rail 5×, 0x4f bust 6 columns | 0x50 |
| Metal | 0x51 | 0x51 / 0x52 / 0x53 / 0x54 / 0x55 (5 laps 00:58) / 0x56 | 0x57 1st without breaking gear walls, 0x58 hit switch 10×, 0x59 break 5 gear walls | 0x5a |

(Of these, the "race 100+ laps" cells `0x1f/0x28/0x32/0x3b/0x44/0x4c/0x56` and the
"5 s faster" cells `0x22/0x2c/0x35/0x3e/0x46/0x50/0x5a` are owned by the other two
evaluators; the rest by `TopRide_CheckPerCourseObjectives`.)

**Time Attack — "Time Attack: *course* finish under T"** (`0x802b88f4`, 2 tiers
per course; `desc+0x10`/`+0x11` give the clear_kinds, `desc+0x8`/`+0xc` the
frame thresholds): Grass 0x5b/0x62, Sand 0x5c/0x63, Sky 0x5e/0x65, Fire
0x60/0x67, Light 0x5d/0x64, Water 0x5f/0x66, Metal 0x61/0x68.

**Free Run — "Free Run: *course* one lap under T"** (`0x802b7dac`, gated session
mode 2; `desc+0x1c`/`+0x1d` clear_kinds, `desc+0x14`/`+0x18` thresholds, best lap
= `session+0x18`): Grass 0x69/0x70, Sand 0x6a/0x71, Sky 0x6c/0x73, Fire 0x6f/0x76,
Light 0x6b/0x72, Water 0x6d/0x74, Metal 0x6e/0x75.

## Evaluator family (named)

| Address | Name | Mode |
|---------|------|------|
| `0x8004db74` | `CityTrial_CheckForNewUnlocks` | CT |
| `0x8004e660` | `CityTrial_CheckFreeRunObjectives` | CT |
| `0x8004e748` | `CityTrial_CheckStadiumPlayedObjectives` | CT |
| `0x8004e810` | `CityTrial_CheckStadiumScoreObjectives` | CT |
| `0x8004e998` | `CityTrial_CheckStadiumResultObjectives` | CT |
| `0x8004f03c` | `CityTrial_CheckStadiumKOObjectives` (DD/Melee KO cells) | CT |
| `0x8004a7f0` | `AirRide_CheckObjectivesPerFrame` (Table B) | AR |
| `0x8004aa58` | `AirRide_CheckRaceFinishObjectives` (Table A + accumulators) | AR |
| `0x8004a90c` / `0x8004a994` | `AirRide_DispatchFreeRunObjectives` / `AirRide_DispatchRaceTimeAttackObjectives` | AR |
| `0x8004d248` | `AirRide_CheckRaceLapObjectives` (`AIRRIDEMODE_RACE` lap cells) | AR |
| `0x8004d454` | `AirRide_CheckRaceDistanceObjectives` | AR |
| `0x8004d5d4` | `AirRide_CheckTimeAttackObjectives` | AR |
| `0x8004d8a8` | `AirRide_CheckFreeRunLapObjectives` | AR |
| `0x800101f4` | `AirRide_OnFinishRace` (per-lap timing; triggers free-run dispatch) | AR |
| `0x802b88f4` | `TopRide_CheckPerCourseObjectives` | TR |
| `0x802b7dac` | `TopRide_CheckPerCourseObjectives_B` | TR |
| `0x802b777c` | `TopRide_CheckSessionFinalizeObjectives` | TR |
| `0x8004a054` | `ClearChecker_SetNewUnlock` (loud setter; AR/CT) | — |
| `0x80049fcc` | `ClearChecker_SetNewUnlockSilent` (silent setter; TR) | — |
| `0x8004a130` | `ClearChecker_GetKindClear` (re-set gate) | — |
| `0x8022ebdc` | `ClearChecker_CheckJustUnlocked_CityTrial_RivalDamage10Sec` | CT |
| `0x8022f648` | `Ply_AddDeath` (unified KO-event recorder) | — |
| `0x80231198` | `Ply_MarkLegendaryMachineAssembled` | — |
| `0x80231340` | `Ply_UnkUpdate` (per-frame distance accumulator) | — |

`ClearChecker_CheckForNewUnlocks` (`0x8004a1a4`) is **not** a per-mode evaluator
dispatcher — it scans the `clear[]` bitfield (20 groups × 6 bytes) for any cell
with a pending "newly unlocked, not yet shown" state, used to drive the unlock
notification, and is mode-agnostic.

## Open questions (Air Ride / Top Ride)

- **TR `TopRideConfig` per-slot layout** — `gmGetTopRideConfigP` returns `0x805366a0` (= `GameData+0xcc8`).
  Per-slot config begins at base `+0x58` with a 9-byte stride (4 slots); within a slot
  `+0` = slot-active flag, `+3` = CPU level (`== 4` ⇒ "level 5"). The other 6 bytes per
  slot are written at scene load but not read by the checklist; their meaning is unknown,
  and pinning it needs a live capture across mixed human/CPU/level slots.
- **TR finalize-abort vtable predicates** — the abort guard dispatches `KirbyHandle`
  vtable `+0x34` (is-human) and `+0x2c` (finished); the functions implementing those two
  slots aren't named yet.

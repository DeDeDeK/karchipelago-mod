# Character Select Screen (CSS) System

The CSS handles player, machine, and color selection before entering gameplay. There are three independent implementations — one per game mode — each running as minor scene callbacks within `MJRKIND_MENU` (major 3). All three share the same shape: per-player input handling, color cycling via L/R, and machine/character icons rendered out of an HSD archive by material animation.

| Mode | Minor | Main Dispatch Function | Data Struct |
|------|-------|----------------------|-------------|
| Air Ride | 8 (`MNRKIND_AIRRIDEPLYSELECT`) | `CSS_airRide_ModeDispatch` (0x8002a1b0) | `airride_select_ply` (GameData+0x108, addressed as GameData+0x10a) |
| City Trial | 10 (`MNRKIND_CITYPLYSELECT`) | `CitySelect_MinorLoad` (0x8003b2c0) | `city_select_ply` (GameData+0x1d0) |
| Top Ride | — | `TopRide_PreGameThink` (0x8002c06c) | `topride_select_ply` (GameData+0x160) |

The hoshi `OnPlayerSelectLoad` callback is hooked at both CSS load points and therefore fires for Air Ride (minor 8, hook at 0x8002a358) and City Trial (minor 10, hook at 0x8003b48c) — not for Top Ride.

## Air Ride CSS (Minor 8)

### Architecture

The Air Ride CSS uses a **two-level dispatch** keyed on the selected Air Ride sub-mode. `Gm_GetAirRideMode()` (0x8003d5f0) returns `GameData[0x35d]`:

| Value | Mode | CSS Function |
|-------|------|-------------|
| 0 | `AIRRIDEMODE_RACE` | `CSS_airRide_RaceUpdate` (0x80028888, 0x1350 bytes) |
| 1 | `AIRRIDEMODE_TIME` | `CSS_airRide_FreeTimeUpdate` (0x80029bd8, 0x5d8 bytes) |
| 2 | `AIRRIDEMODE_FREE` | `CSS_airRide_FreeTimeUpdate` (0x80029bd8, 0x5d8 bytes) |

### Function Table

| Function | Address | Size | Purpose |
|----------|---------|------|---------|
| `CSS_airRide_ModeDispatch` | 0x8002a1b0 | 0x210 | Mode-based dispatch, calls CSS update + `loadMainMenuMusic` |
| `CSS_airRide_RaceUpdate` | 0x80028888 | 0x1350 | Race mode state machine (multi-player, init + per-frame) |
| `CSS_airRide_FreeTimeUpdate` | 0x80029bd8 | 0x5d8 | Free Run / Time Attack state machine (single-player) |
| `CSS_airRide_InitSelectData` | 0x80028754 | 0xe4 | Initializes select struct: colors to {0,1,2,3}, sets init trigger |
| `CSS_airRide_Main` | 0x8002860c | 0x148 | CSS entry point |
| `CSS_airRide_inputGrabber` | 0x80026f20 | 0x1084 | Main input handler (Race) |
| `CSS_airRide_chooseVehicleInputGrabber` | 0x80024d04 | 0xad8 | Vehicle selection input |
| `CSS_airRide_inputGrabberReadyScreen` | 0x80026110 | 0xe10 | Ready screen input (all confirmed) |
| `CSS_airRide_FreeRuInputGrabber` | 0x80027fa4 | 0x668 | Free Run input handler |
| `CSS_airRide_colorChanger` | 0x80021654 | 0x2e0 | L/R color cycling |
| `AirRide_PopulateSelectIcons` | 0x80020a08 | 0xc4c | Builds character icon grid |
| `AirRide_CheckCharacterAvailable` | 0x8002090c | 0xfc | Character availability check |
| `SelIcon_GetCKindLinear` | 0x8000b9a8 | 0x14 | Indexes the linear-strip CKIND table at 0x804957ec |
| `Character_GetDesc` | 0x8000b9dc | 0x18 | Returns `ckind * 3 + 0x80495814` |
| `TitleScreen_CheckMachineUnlocked` | 0x8000c364 | 0x124 | Machine unlock check — **title-screen demo only** |
| `TitleScreen_SelectRandomMachine` | 0x8000daa0 | 0x148 | Random machine assignment — **title-screen demo only** |
| `TitleScreen_SetupDemoMachines` | 0x8000dbe8 | 0x164 | Builds the 4 demo riders/machines (calls `TitleScreen_SelectRandomMachine`) |
| `loadCPU` | 0x80023600 | 0x2d4 | CPU player setup (real in-game CPU machine pick) |
| `Gm_GetAirRideMode` | 0x8003d5f0 | 0x24 | Returns `airride_mode` |
| `Gm_GetAirRidePlayerSlot` | 0x8003d644 | 0x24 | Returns active player slot (Free Run / Time Attack) |

### Init Trigger

`GameData[0x10b]` is the init flag. `CSS_airRide_InitSelectData` sets it to `0xFF`; when the CSS update function sees it non-zero on entry it runs the color init block — `color[0..3] = {0, 1, 2, 3}` plus a per-slot state reset — then clears the flag.

Both `CSS_airRide_RaceUpdate` and `CSS_airRide_FreeTimeUpdate` carry their own copy of that init block, so a mod validating colors against an unlock mask has to hook both convergence points.

### Select Screen Grid

`AirRide_PopulateSelectIcons` (0x80020a08) has two layouts: a 2×10 grid (>= 10 available characters) and a linear strip (< 10). It builds the available list at `gd[0x170]` with the count at `gd[0x16f]`, and sets the layout flag at `gd[0x184]` (1 = grid, 0 = linear).

Grid table, raw CKIND bytes at `0x80495800`:

```
Row 0: DRAGOON FORMULA WINGED WARP    COMPACT FLIGHT SHADOW WAGON   SWERVE HYDRA
       (15)    (3)     (9)    (1)     (0)     (17)   (8)    (6)     (5)    (16)
Row 1: DEDEDE  JET     ROCKET TURBO   BULK    SLICK  WLBIKE WLSCOOT REXWHL METAKN
       (18)    (10)    (11)   (2)     (7)     (4)    (13)   (12)    (14)   (19)
```

Linear strip table at `0x804957ec`, indexed by `SelIcon_GetCKindLinear` (0x8000b9a8 — `lis r4,0x8049; addi r3,r4,0x57ec; lbzx r3,r3,idx`). Raw CKIND bytes:

```
DEDEDE DRAGOON JET ROCKET TURBO BULK FORMULA WINGED WARP COMPACT
FLIGHT SHADOW WAGON SWERVE SLICK WLBIKE WLSCOOT REXWHL HYDRA METAKN
```

(Per `menu.h`, CKIND_SLICK=4 and CKIND_WAGON=6 — Slick Star is the slot-4 entry, Wagon Star slot 6.)

#### Grid mode reordering

In grid mode the function pulls the four "special" characters (CKIND_DRAGOON=15, CKIND_HYDRA=16, CKIND_DEDEDE=18, CKIND_METAKNIGHT=19) out to the edges of each row for visual balance, then interleaves the two rows. The test is `(byte)(ckind - 0xf) < 2 || ckind == 0x12 || ckind == 0x13`.

### CharacterKind to MachineKind Mapping

CharacterKind (20 entries, `menu.h`) maps to MachineKind/VCKIND (26 entries) through the `CharacterDesc` table at `0x80495814`, 3 bytes per entry (`rider_kind`, `is_bike`, `machine_kind`). `Character_GetDesc` (0x8000b9dc) returns `ckind * 3 + 0x80495814`.

Six VCKINDs have no CharacterKind and are city-spawn or transformation only: FREE, STEER, WINGKIRBY, WHEELNORMAL, WHEELKIRBY, WHEELVSDEDEDE.

The `CharacterDesc` table, the grid table at `0x80495800` and the linear strip at `0x804957ec` sit back to back with no slack, so a 21st CharacterKind cannot be appended in place. The `custom_machines` mod relocates all three into wider mod-owned copies by rewriting the `lis`/`addi` pair inside each of the three accessors, and writes the grid's runtime column count into `Icon_GetCKind`'s `mulli r5, r0, cols` at `0x8000b9c4` (10 in vanilla, one more column per two appended characters).

### The packed icon list

Each screen keeps the icons it is offering in its own block of `GameData`, at the same shape off a base of `0x10a` (Air Ride) or `0x1d0` (City Trial): an icon count at `+0x65` and one `CharacterKind` per icon from `+0x66`. Every access is a `+101` / `+102` displacement off that base. Air Ride's block runs `0x10a`..`0x196` (`CSS_airRide_InitSelectData` memsets 0x8d bytes) and City Trial's `0x1d0`..`0x25b` (0x8c bytes).

The byte right after each 20-entry list is live: Air Ride's row-layout flag - 1 when the icons wrap to two rows, read by the input grabbers and the race/free-time updates - and City Trial's debug-grid flag. `+0x7d` is untouched on both screens and inside both memsets, so `custom_machines` relocates each flag there by rewriting the displacement of its nine (Air Ride) and six (City Trial) `lbz`/`stb` sites, freeing the 21st list slot.

`AirRide_PopulateSelectIcons` (0x80020a08) and the tail of `CitySelect_CreateMachineIcons` (from 0x8002f0b8) fill their list, call the layout pass, then create one icon GObj per entry. Both pack into two 10-byte stack rows first and cannot carry an 11th column, so a mod offering 21 characters has to replace them rather than widen a bound.

#### Icon anchors

Both screens position their icons against a strip of 20 sibling `JOBJDesc` in a dedicated model — `ScMenSelplyIpos_scene_models` (`MnSelplyAll.dat`, `0x78740`..`0x78c00` at 0x40 stride) and `ScMenSelplyIposCt_scene_models` (`MnSelplyctAll.dat`) — posed by an animation whose **frame is the icon count**. `AirRideSelect_LayoutIcons` (0x80133f68 → 0x80151258) and `CitySelect_LayoutMachineIcons` (0x801355f4 → 0x8015bd14) set the frame to the count, then call `JOBJ_GetWorldPosition` on each of the 20 anchors into the ipos GObj's userdata.

Each anchor's `AOBJ` carries a TRAX and a TRAY track of CON keys, one per frame. At frame N the first `ceil(N/2)` anchors take the top row and the rest the bottom; anchors from N onward park offscreen at X = -55.799. Counts of 9 and below use a single row at Y 0.700; from 10 up the rows sit at Y 5.212 and Y -0.885 (Air Ride) or 4.300 and -0.700 (City Trial), the bottom offset by half a column so the two interleave. Spacing shrinks as the count grows to keep both rows inside a fixed half-width `H` — 30.62 on Air Ride, 25.20 on City Trial:

```
top = ceil(N/2)            spacing = 2H / max(top - 1, (N - top) - 0.5)
top row    x = -H + i * spacing               y = top row
bottom row x = -H + spacing/2 + i * spacing   y = bottom row
```

At 20 icons that is 10 + 10 at 6.445 apart, at 19 it is 10 + 9 at 6.811, and at 21 it would be 11 + 10 at 6.124. The anchors have no key past frame 20 and there is no 21st anchor to pose, so `custom_machines` replaces the two layout wrappers: up to 20 icons the engine's own pass runs untouched, and past that the same arithmetic is redone in mod code straight into the userdata, with the shared icon scale multiplied by the spacing ratio so the tiles still fit their columns. The icon that has no anchor is kept in the mod rather than in the userdata, where its `Vec3` would land on the trailing fields, and `AirRideSelect_GetIconPos` / `CitySelect_GetIconPos` are replaced to hand it out.

Icon index maps to joint index through a byte table read by the creator: `0x804ab048` for Air Ride and `0x804ab728` for City Trial, both `02 03 ... 15` (joint 1 is the model root).

The ipos userdata is laid out the same way on both screens up to the positions and differently after them: 20 anchor `JObj` pointers at `+0x10`, one `Vec3` per icon at `+0x60`, then Air Ride's shared icon scale at `+0x150` and count at `+0x15c` against City Trial's count at `+0x150` and scale at `+0x154`.

Icon GObjs go into a 20-entry array in `ScMenuCommon` (`0x80558788`): `airride_select.sicon_gobj[20]` at `+0x520` and City Trial's at `+0x80c`. Each is written by one unguarded store and read nowhere, so a 21st icon overwrites the `JOBJSet` pointer that follows (`+0x570` and `+0x85c`); `custom_machines` puts that pointer back right after the store lands.

#### Cursor rows

`AirRideSelect_Cursor1InputThink` (0x80023b68) and `CitySelect_Cursor1InputThink` (0x800312fc) carry the same code. Below 10 icons they take a single-row path that handles only left and right, wrapping at the ends; from 10 up they split the list at `ceil(N/2)` — the same split the anchors use — and add up/down between the rows.

#### Character name plates

The 80x48 name plate is drawn from a TexAnim bank shared in shape by the two select screens and all four results screens, and its frame is *not* simply the CharacterKind. `AirRideSelect_SetSIcon2Character` (0x80151b78) and its siblings compute:

```
frame = (ckind == CKIND_DEDEDE)     ? color + 20
      : (ckind == CKIND_METAKNIGHT) ? color + 30
      : ckind
```

The bank answers with CON keys at frames 0..17 (entries 0..17), 20 (entry 18) and 30 (entry 19), so every King Dedede color resolves to one plate and every Meta Knight color to another. Eight functions carry a copy of that arithmetic - the color and character setters of each select screen, and the `Siconbig` creator of each results screen:

| Function | Address | Screen |
|---|---|---|
| `AirRideSelect_SetSIcon2Color` | 0x80151ab4 | Air Ride select |
| `AirRideSelect_SetSIcon2Character` | 0x80151b78 | Air Ride select |
| `CitySelect_SetSIcon2Color` | 0x8015c574 | City Trial select |
| `CitySelect_SetSIcon2Character` | 0x8015c638 | City Trial select |
| `MnResult_CreateSiconBig` | 0x80167250 | race results, under two humans |
| `MnResult2_CreateSiconBig` | 0x8016aff4 | race results, two humans |
| `MnResult4_CreateSiconBig` | 0x8016e924 | race results, three or four humans |
| `MnResultCt_CreateSiconBig` | 0x80177ae8 | City Trial results |

#### Bike-relative indexing

For non-bikes (`is_bike = 0`), `CharacterDesc.machine_kind` **is** the VCKIND. For bikes (`is_bike = 1`), `machine_kind` is a **bike-relative index** and the real VCKIND is `VCKIND_WHEELNORMAL (19) + machine_kind`. Use `CharacterDesc_GetMachineKind()` (inline in `menu.h`) rather than reading the field directly.

| CharacterKind | is_bike | desc.machine_kind | Actual VCKIND |
|---|---|---|---|
| CKIND_COMPACT (0) | 0 | 1 | VCKIND_COMPACT (1) |
| CKIND_WARP (1) | 0 | 0 | VCKIND_WARP (0) |
| CKIND_TURBO (2) | 0 | 12 | VCKIND_TURBO (12) |
| CKIND_FORMULA (3) | 0 | 7 | VCKIND_FORMULA (7) |
| CKIND_SLICK (4) | 0 | 6 | VCKIND_SLICK (6) |
| CKIND_SWERVE (5) | 0 | 11 | VCKIND_SWERVE (11) |
| CKIND_WAGON (6) | 0 | 9 | VCKIND_WAGON (9) |
| CKIND_BULK (7) | 0 | 5 | VCKIND_BULK (5) |
| CKIND_SHADOW (8) | 0 | 3 | VCKIND_SHADOW (3) |
| CKIND_WINGED (9) | 0 | 2 | VCKIND_WINGED (2) |
| CKIND_JET (10) | 0 | 13 | VCKIND_JET (13) |
| CKIND_ROCKET (11) | 0 | 10 | VCKIND_ROCKET (10) |
| CKIND_WHEELIESCOOTER (12) | 1 | 4 | VCKIND_WHEELIESCOOTER (23) |
| CKIND_WHEELIEBIKE (13) | 1 | 2 | VCKIND_WHEELIEBIKE (21) |
| CKIND_REXWHEELIE (14) | 1 | 3 | VCKIND_REXWHEELIE (22) |
| CKIND_DRAGOON (15) | 0 | 8 | VCKIND_DRAGOON (8) |
| CKIND_HYDRA (16) | 0 | 4 | VCKIND_HYDRA (4) |
| CKIND_FLIGHT (17) | 0 | 14 | VCKIND_FLIGHT (14) |
| CKIND_DEDEDE (18) | 1 | 5 | VCKIND_WHEELDEDEDE (24) |
| CKIND_METAKNIGHT (19) | 0 | 18 | VCKIND_WINGMETAKNIGHT (18) |

### Vanilla Availability Logic

`AirRide_CheckCharacterAvailable` (0x8002090c) switches on CharacterKind:

- `case 0` (CKIND_COMPACT): **always returns 0** — Compact Star never appears in vanilla Air Ride
- `case 1` (CKIND_WARP): **always returns 1** — Warp Star always available
- `case 15, 16, 17` (DRAGOON, HYDRA, FLIGHT): **always returns 0** — City Trial-only
- All others: map to an Air Ride checklist reward index and query it (`Checklist_IsCacheValid` 0x8007b650 → `Checklist_CheckCachedUnlock_AirRide` 0x80007e34, else `ClearChecker_CheckUnlocked` 0x80049e24)

| CharacterKind | Reward Index |
|---|---|
| CKIND_TURBO (2) | 0x1d (29) |
| CKIND_FORMULA (3) | 0x19 (25) |
| CKIND_SLICK (4) | 0x18 (24) |
| CKIND_SWERVE (5) | 0x15 (21) |
| CKIND_WAGON (6) | 0x14 (20) |
| CKIND_BULK (7) | 0x16 (22) |
| CKIND_SHADOW (8) | 0x1a (26) |
| CKIND_WINGED (9) | 0x13 (19) |
| CKIND_JET (10) | 0x1e (30) |
| CKIND_ROCKET (11) | 0x1c (28) |
| CKIND_WHEELIESCOOTER (12) | 0x1b (27) |
| CKIND_WHEELIEBIKE (13) | 0x17 (23) |
| CKIND_REXWHEELIE (14) | 0x1f (31) |
| CKIND_DEDEDE (18) | 0x20 (32) |
| CKIND_METAKNIGHT (19) | 0x21 (33) |

#### Title-screen demo machine pick

`TitleScreen_SelectRandomMachine` (0x8000daa0) loops CharacterKinds 0–17 (excluding CKIND_DEDEDE/CKIND_METAKNIGHT), skips the 4 most recently assigned CKinds (history at `gd[0x1d]`–`gd[0x20]`), lets Warp Star (ckind 1) bypass the unlock check, and gates the rest through `TitleScreen_CheckMachineUnlocked` (0x8000c364; takes `machine_class`/is_bike and `machine_id`, returns 0 for unknown IDs). The only call chain is `TitleScreen_MinorExit` → `TitleScreen_SetupDemoMachines` (0x8000dbe8) → `TitleScreen_SelectRandomMachine` → `TitleScreen_CheckMachineUnlocked`. None of it runs for CPUs in real Air Ride races.

#### In-game CPU machine pick (loadCPU)

The actual Air Ride CPU machine is chosen in `loadCPU` (0x80023600) and the sibling ready-screen / setup paths at 0x80026534 and 0x8002988c. All three run the identical sequence against the per-slot base `GameData + 0x10a + slot`:

```
idx = HSD_Randi(gd[0x16f]);       // count of available characters
icon[slot]        = idx;          // slot base +0x2d  (GameData 0x137)
machine_kind[slot] = gd[0x170 + idx]; // slot base +0x61  (GameData 0x16b)
```

`gd[0x16f]` / `gd[0x170..]` are the count and body of the available-character list built by `AirRide_PopulateSelectIcons`, which is itself filtered by `AirRide_CheckCharacterAvailable`. So vanilla already picks a random **unlocked** machine here — gating the available list is sufficient, no separate CPU hook is needed for the machine.

`CSS_airRide_RaceUpdate` runs the inverse lookup around 0x80029700–0x8002978c: given a slot's `machine_kind` (+0x61) it scans the available list for the matching entry and writes that index back to `icon[]` (+0x2d), falling back to the entry whose value is 1 (Warp Star).

### Air Ride Select Data

The struct starts at `GameData + 0x108` (`airride_select_ply` in `game.h`), but all the CSS code addresses it through a base register holding `GameData + 0x10a`, and adds the player slot to that base. Both offset forms appear below. Note the CSS's fields run past `airride_select_ply`'s nominal end at 0x15f into the bytes `game.h` labels as `topride_select_ply`'s 0x160 header — 0x16b/0x16f/0x170 are Air Ride CSS state (`loadCPU` reads them through the 0x10a base), not Top Ride state.

Per-slot fields (4 entries each, slots 0–3):

| Field | GameData offset | CSS base offset | Purpose |
|-------|-----------------|-----------------|---------|
| `p_kind[4]` | 0x133 | +0x29 | Player kind (human/CPU/none) |
| `icon[4]` | 0x137 | +0x2d | Index into the available-character list (which icon the slot has selected) |
| `color[4]` | 0x15b | +0x51 | **Actual in-game Kirby color** (L/R cycling target) |

Other fields, by offset from the CSS base (`GameData + 0x10a`):

| Offset | GameData | Purpose |
|--------|----------|---------|
| +0x00 | 0x10a | First byte of select struct |
| +0x01 | 0x10b | Init trigger flag (0xFF = run init block) |
| +0x03 | 0x10d | State flag (init vs active path) |
| +0x09 | 0x113 | Rendering state (0 = inactive, 2 = active) |
| +0x25 | 0x12f | Player identity mapping (slot → player) |
| +0x31 | 0x13b | Copy of `icon[]` taken during the CSS update |
| +0x45 | 0x14f | CSS state (0 = active human, 2 = CPU needing random, 3 = inactive) |
| +0x4d | 0x157 | Per-identity state |
| +0x55 | 0x15f | Initialized to 8 |
| +0x59 | 0x163 | Initialized to 8 |
| +0x5d | 0x167 | Initialized to 2 |
| +0x61 | 0x16b | Machine kind (VCKIND) |
| +0x65 | 0x16f | Count of available characters |
| +0x66 | 0x170 | Available-character list (CKIND bytes, `gd[0x16f]` entries) |
| +0x7a | 0x184 | Icon layout flag (1 = grid, 0 = linear strip) |
| +0x7b | 0x185 | When 1, `CSS_airRide_colorChanger` treats every color as available |

**`icon[]` and `color[]` are separate fields.** `color[]` is the value that becomes the in-game Kirby color and is what L/R cycles. `icon[]` is the select-grid index and is never copied into `color[]`.

## City Trial CSS (Minor 10)

`CitySelect_MinorLoad` checks `Gm_GetCityMode()` and dispatches to a sub-loader:

| CityMode | Value | Sub-loader |
|----------|-------|-----------|
| `CITYMODE_TRIAL` | 0 | `CitySelect_LoadCityTrial` (0x80038d6c) |
| `CITYMODE_STADIUM` | 1 | `CitySelect_LoadStadium` (0x80039e20) |
| `CITYMODE_FREERUN` | 2 | `CitySelect_LoadMachineSelect` (0x8003a904) |

### Function Table

| Function | Address | Size | Purpose |
|----------|---------|------|---------|
| `CitySelect_MinorLoad` | 0x8003b2c0 | 0x1e8 | Minor load dispatcher |
| `CitySelect_MinorThink` | 0x8003b4c8 | 0x20 | Per-frame minor think |
| `CitySelect_Init` | 0x80135060 | 0x128 | CSS initialization |
| `CitySelect_Think` | 0x80037a90 | 0x11b0 | Main per-frame update loop |
| `CitySelect_CreatePlayers` | 0x801352b0 | 0x6c | Creates player GObjs |
| `CitySelect_InitPlayerMachines` | 0x8002ddd8 | 0x330 | Initial machine assignments |
| `CitySelect_CreateMachineIcons` | 0x8002e3c4 | 0xe74 | Machine icon grid |
| `CitySelect_InputUpdate` | 0x80032d34 | 0xe48 | Player input processing |
| `CitySelect_PlayerThink` | 0x800348f8 | 0xacc | Per-player think callback |
| `CitySelect_ChangeColor` | 0x8002f238 | 0x350 | L/R color cycling |
| `CitySelect_LoadCityTrial` | 0x80038d6c | 0x10b4 | City Trial game state loader |
| `CitySelect_LoadStadium` | 0x80039e20 | 0xae4 | Stadium mode loader |
| `CitySelect_LoadMachineSelect` | 0x8003a904 | 0x9bc | Machine selection (Free Run) |
| `CitySelect_GetColorAnimFrame` | 0x80009630 | 0x28 | Animation frame for color display |

### City Trial Select Data

Base: `GameData + 0x1d0` (`city_select_ply`).

| Field | Offset | Purpose |
|-------|--------|---------|
| `is_all_ready` | 0x1d5 | All players confirmed, checks for start |
| `ply_is_selecting_bitfield` | 0x1d6 | Flag per player currently selecting |
| `is_ready[4]` | 0x1d9 | Per-player ready flag |
| `player_state[4]` | 0x1f1 | 0=inactive, 1=icon select, 2=handicap/cpu, 3=player element |
| `ply_cursor[4]` | 0x1f5 | Player's cursor position |
| `ply_bar[4]` | 0x1f9 | Bar index hovered over |
| `icon[4]` | 0x1fd | Icon selection |
| `icon_saved[4]` | 0x201 | Preserved icon after player exits |
| `ply_pkind[4]` | 0x21d | Player kind |
| `ply_color[4]` | 0x221 | **Kirby color** |
| `ply_hmn_handicap[4]` | 0x225 | Human handicap setting |
| `ply_cpu_handicap[4]` | 0x229 | CPU handicap setting |
| `ply_cpu_level[4]` | 0x22d | CPU level (0..8, displayed 1..9) |
| `ply_icon_ckind[4]` | 0x231 | CharacterKind of selected icon |
| `machine_select.num` | 0x235 | Number of selectable machines |
| `machine_select.c_kind_arr[20]` | 0x236 | Array of selectable CharacterKind indices |

## Top Ride CSS

Top Ride has a simpler select screen that handles both course and player selection.

| Function | Address | Size | Purpose |
|----------|---------|------|---------|
| `TopRide_PreGameThink` | 0x8002c06c | 0xa14 | Course/character selection |
| `TopRide_OnCourseSelect` | 0x8002cc30 | 0x3a8 | Course selection callback |
| `TopRide_CSS_PanelThink` | 0x8002b8a8 | 0x7c4 | Bottom-panel editor (CPU level, handicap, control type) |
| `CSS_topRide_colorChanger` | 0x8002a400 | 0x230 | L/R color cycling |

Select data base: `GameData + 0x160` (`topride_select_ply`); `color[4]` at 0x1ba, `panel_cpu_level[4]` at 0x1be, `panel_handicap[4]` at 0x1c2, `panel_machine[4]` at 0x1c6. Course selection lives outside the struct in `GameData.topride_selected_course` at 0x374.

## Common Patterns

### Input Grabbers

Each CSS has dedicated input-grabber functions called per player per frame from its Think callback. They handle controller port detection and join/leave, D-pad/stick navigation, A/B confirm/cancel, and delegate L/R to the colorChanger. Air Ride splits them by CSS state: main selection, vehicle choice, ready screen, and Free Run.

### Color Cycling

| Mode | Function | Address |
|------|----------|---------|
| Air Ride | `CSS_airRide_colorChanger` | 0x80021654 |
| Top Ride | `CSS_topRide_colorChanger` | 0x8002a400 |
| City Trial | `CitySelect_ChangeColor` | 0x8002f238 |

Each cycles the slot's `color[]` through 0–7 and then runs an availability check on the candidate. In vanilla, **colors 0–3 are unconditionally available** (`li r3, 1`) and **colors 4–7 are checklist rewards** — reward indices 15, 16, 17, 18 respectively, queried through the same `Checklist_IsCacheValid` / `ClearChecker_CheckUnlocked` pair the machine check uses. All the branches merge at a single convergence point per function (Air Ride 0x8002176c, Top Ride 0x8002a510, City Trial 0x8002f350), which is where the mod's mask override attaches.

### CPU Level / Handicap Bar Widget

The per-player **CPU difficulty** and **handicap** selectors are drawn as a **segmented bar gauge** — a fixed row of discrete "notch" JOBJs sourced from the menu archive — *not* a numeric digit. The level the player reads (City Trial / Air Ride **1–9**, Top Ride **1–5**) is the count of lit notches.

| Mode | Notches | Internal value | Field |
|------|---------|----------------|-------|
| City Trial / Air Ride | **9** | `cpu_level` 0..8 (display 1..9) | `ply_cpu_level` (GameData+0x22d) |
| Top Ride | **5** | `panel_cpu_level` 0..4 (display 1..5) | `panel_cpu_level` (GameData+0x1be) → committed via `TopRide_SetCpuLevel` (0x8000be74) |

#### City Trial / Air Ride render path

`CSS_SetBarLevel` (0x80135694) is the generic entry every City Trial bar caller uses; it forwards to `CSS_SetBarLevel_Impl` (0x8015ca8c). Signature `(slot, bar_kind, level)`:

- The bar GObj is `bar_gobj[slot][bar_kind]` at CT scene struct `+0x7e8` (`slot * 8 + bar_kind * 4`). `bar_kind` `0` = handicap, `1` = cpu-level. The "filled" texture frame per bar kind is byte +2 of a 3-byte-stride table at `0x805d6978` (r13 − 0x6768).
- The worker walks the bar GObj's **fixed array of 9 notch JOBJs** (user-data +0x14 … +0x34, stride 4), lighting notches `0..level` with the filled frame and blanking `level+1..8`.
- The element count is **hardcoded to 9**: `cmpwi r27,8` @ 0x8015cb44 (sibling bounds at 0x8015cbd8 and 0x8015cd3c, plus helper 0x80135f44). The fill loop runs `while r27 <= level`, so **`level > 8` indexes past the 9-JOBJ array → out-of-bounds write (crash/corruption)**, not a cosmetic glitch.

The render is gated on the slot being a CPU (`ply_pkind` == CPU); see the `CSS_SetBarLevel` call sites in `CitySelect_InputUpdate` and `CitySelect_PlayerThink`. Air Ride uses the identical per-player menu struct (`AirRideSelectMenuData`, with `cpu_level_text_j` / `level_bar_pos_j`) and the same 1–9 display, through its own parallel renderer.

#### Top Ride input + render path

Both editing and rendering happen in `TopRide_CSS_PanelThink` (0x8002b8a8), the bottom-panel editor over the `topride_select_ply` fields:

- **Input clamp:** `panel_cpu_level` (+0x1be) increments only while `< 4` — `cmpwi r0,4` @ 0x8002bf8c (`bge` @ 0x8002bf90, store `stb r0,39(r27)` @ 0x8002bf9c). `panel_handicap` (+0x1c2) has a parallel `cmpwi r0,4` @ 0x8002bfcc.
- **Render:** passes `4 − level` (`subfic r0,r0,4` @ 0x8002bfa8) to the 5-notch Top Ride bar renderers (`zz_80134d44_` / `zz_80134d64_`). The constant `4` is baked into the clamp, the render math, *and* the notch count.

#### Raising the maximum

The bar does **not** auto-scale with the cap — the notch models are fixed art in the menu archives (`MnSelplyctAll.dat` / `MnSelplyAll.dat` / `MnSelplym2dAll.dat`). Extending the maximum past 9 (CT/AR) or 5 (TR) needs **three coordinated changes**, per mode:

1. **Input clamp** — raise the `<9` / `<5` guard (Top Ride: the `cmpwi r0,4` @ 0x8002bf8c; the City Trial / Air Ride clamp on `ply_cpu_level` is bounded <= 8 but is not pinned to a specific instruction here). Trivial `REPLACEINSTRUCTION`.
2. **Renderer loop bound(s)** — the hardcoded `8`/`9` (CT/AR: 0x8015cb44 and siblings) and Top Ride's baked `4` (clamp + `4−level` subfic + the TR notch helpers).
3. **New art** — add notch JOBJ nodes to the menu archive and position them along the bar.

Doing only #1 is **worse than nothing** — it is an out-of-bounds write. Steps 1+2 without 3 read past the JOBJ array.

UI and behavior are decoupled: the AI's difficulty is `difficulty_level` (CpuData+0x22, 0..8), set at `Rider_CPUInit` and read by `Rider_CPUDifficultyScale`, and CT/AR `cpu_level` (0..8) maps onto it **1:1**. Making CPUs harder therefore does not require extending the UI bar — remap the existing 1–9 / 1–5 selections onto a steeper internal curve, or drive behavior through the `custom_ai` presets (`cpu-ai-system.md`).

### Archive / Icon System

Select-screen icons are rendered from HSD archive files using material animation:

| Archive | Purpose |
|---------|---------|
| `MnSelplyAll.dat` | Air Ride character icons |
| `MnSelplyctAll.dat` | City Trial character icons |
| `MnSelplym2dAll.dat` | Top Ride icons |
| `SisSelply.dat` | Air Ride system data |
| `SisSelplyCt.dat` | City Trial system data |

Archive data is loaded into `ScMenuSelect` (at `stc_menu_select`, 0x804962b0), which holds per-mode sub-structs with JOBJSet pointers and GObj arrays for every visual element. The loaded `MnSelplyAll` archive pointer itself is cached at `0x805dd7bc` (r13 + 0x6dc, `stc_MnSelplyAll_archive` in `menu.h`).

#### MnSelplyAll archive structure

`MnSelplyAll.dat` has 22 root symbols. The ones the icon system uses:

| Symbol | Data offset | Purpose |
|--------|-------------|---------|
| `ScMenSelplySicon_scene_models` | 0x040908 | Air Ride character icon model + material animation |
| `ScMenSelplySicon2_scene_models` | 0x078250 | Alternate icon model |
| `ScMenSelply_scene_data` | 0x0002a4 | Scene layout/camera data |

The Sicon pointer is resolved during menu init by `AirRideSelect_Index` (0x801515f8), which calls `Archive_GetSymbols(archive, menu_data + 0x51c, "ScMenSelplySicon_scene_models", 0)` — `+0x51c` is `ScMenSelplySicon_scene_models` inside `ScMenuCommon`'s player-select sub-struct.

#### Scene models structure

Each `scene_models` root is an indirect pointer to a 16-byte model group:

```
+0x00: JObjDesc*          — model root
+0x04: AnimJoint**        — skeletal animation banks
+0x08: MatAnimJoint**     — material animation banks (texture swapping)
+0x0C: ShapeAnimJoint**   — shape animation banks
```

For Sicon the group sits at data offset 0x0408f8:

```
+0x00: 0x00035be8  → JObjDesc (model root)
+0x04: 0x00000000  → no skeletal animation
+0x08: 0x000408f0  → MatAnimJoint banks array
+0x0C: 0x00000000  → no shape animation
```

#### Icon animation pipeline

`_JObj_AddSetAnim` (0x80055a30) pulls the animation banks out of the model group:

```c
void _JObj_AddSetAnim(JObj *jobj, int bank, void *scene_models) {
    AnimJoint *anim = scene_models[1] ? scene_models[1][bank] : NULL;
    MatAnimJoint *mat = scene_models[2] ? scene_models[2][bank] : NULL;
    ShapeAnimJoint *shape = scene_models[3] ? scene_models[3][bank] : NULL;
    HSD_JObjAddAnimAll(jobj, anim, mat, shape);
}
```

Per-icon creation in `zz_80151644_` (0x80151644):

1. `MainMenu_CreateGObj(**scene_models_ptr)` (0x80138934) — create GObj from JObjDesc
2. `GObj_AddProc(gobj, zz_801515d8_)` (0x801515d8) — per-frame update callback
3. `MainMenu_AddAnim(ckind_float, ..., jobj, scene_models_ptr)` (0x801389d8) — apply material animation at frame = CharacterKind value

`MainMenu_AddAnim` forwards to `JObj_AddSetAnim_SetFrameAndRate` (0x80138b10):

1. `HSD_JObjRemoveAnimAll()` — clear existing animation
2. `_JObj_AddSetAnim(jobj, 0, scene_models)` — load bank 0 (the MatAnimJoint)
3. `HSD_JObjReqAnimAllFlags()` — request animation update
4. `JObj_SetAllAOBJRateByFlags(frame, ..., jobj, 0xffff)` — set frame to the ckind value
5. `HSD_JObjAnimAll_(jobj)` — apply, selecting the texture for that frame

#### JObj model tree (Sicon)

```
JObj@0x35be8 (root, container, no display)
  └─ JObj@0x35c28 (icon, has display objects)
       ├─ DObj 1: 64×64 CMPR texture (main icon image)
       └─ DObj 2: 32×32 RGB5A3 texture (decoration/frame)
```

#### Icon texture animation

Bank 0's MatAnimJoint tree drives the icon texture; the animation frame value (= CharacterKind) selects which image the material displays.

```
MatAnimJoint root @0x36bd0: child=0x36bdc, next=NULL, matanim=NULL
  └─ MatAnimJoint child @0x36bdc  (matches the child JObj carrying the display objects)
       └─ MatAnim @0x36bb0
            └─ TexAnim @0x36b98 (TEXMAP0, images=20, tluts=20)
                 ├─ AOBJ @0x36908 — end frame 300.0, 2 FOBJ tracks
                 │    ├─ FOBJ @0x368e0 track 1  (image index)
                 │    └─ FOBJ @0x368f4 track 10 (TLUT index)
                 ├─ ImageDesc[20] @0x36af8 — one 64×64 CMPR image per CharacterKind
                 └─ TlutDesc[20]  @0x36b48 — 1-entry TLUT per image
```

There are 20 image slots, one per CharacterKind, and **every slot including index 0 (CKIND_COMPACT) points at distinct, non-blank pixel data** — index 0's image data is at archive data offset 0x36c00. The static (non-animated) TObj on the icon DObj points at the index-1 image (0x35c80). `MnSelplyctAll.dat`'s City Trial equivalent, `ScMenSelplySiconCt_scene_models` (data offset 0x05c008), carries a parallel 20-image TexAnim at 0x52298.

## Mod Hook Points

The mod systems that intercept CSS behavior, each documented in its own file:

**Color gating (`gate-colors.md`)** — L/R cycling hooks on all 3 colorChanger convergence points; `color[]` init validation in both Air Ride CSS functions; a hook on the machine-to-list lookup that writes `icon[]`; `ValidateCityTrialColors` via `OnPlayerSelectLoad` (CT has no init block to hook).

**Machine gating (`gate-machines.md`)** — `AirRide_CheckCharacterAvailable` REPLACEFUNC (which is what makes `loadCPU`'s gated `HSD_Randi` index respect the unlock mask); `TitleScreen_CheckMachineUnlocked` REPLACEFUNC for the title-screen demo only; a CT starting-machine convergence hook at `CitySelect_InitPlayerMachines` (0x8002dea0); CT Free Run hooks at `CitySelect_CreateMachineIcons` for counting (0x8002e5c0) and array-building (0x8002e738); CT Free Run select-list filtering in `OnPlayerSelectLoad`.

**Stage gating (`gate-stages.md`)** — `AirRide_CheckCourseUnlocked` (0x8000c0e0) REPLACEFUNC gates Air Ride stage availability on the map select screen (`gate_airride_stages.c`).

### Main-menu demo rider

The title screen runs a "demo player" setup at `0x8000d300` that configures slot 0's idle rider through a series of `Ply_Set*` calls. Three `li r4, imm` operands choose what is ridden; each is `REPLACEINSTRUCTION`'d at boot to swap the default Kirby-on-Warp-Star for Dedede-on-Wagon:

| Address | Vanilla | Sets |
|---------|---------|------|
| 0x8000d340 | `li r4, 0` | `Ply_SetRiderKind(0, …)` (RDKIND) |
| 0x8000d34c | `li r4, 0` | `Ply_SetIsBike(0, …)` |
| 0x8000d358 | `li r4, 0` | `Ply_SetMachineKind(0, …)` (VCKIND) |

`Ply_SetMachineKind` stores a class-relative index: star-class (`is_bike = 0`) uses the `VCKIND_*` value directly, wheel-class is relative to `VCKIND_WHEELNORMAL`. The demo init calls `MachineStateChange` with hardcoded star-only state ids (82/89), so a wheel-class machine crashes here — keep `is_bike = 0` and pick a star machine.

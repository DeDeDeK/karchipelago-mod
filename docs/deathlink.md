# DeathLink

Deathlink synchronizes deaths between players in a multiworld: dying in Kirby Air Ride sets `ap_data->deathlink_send`, and the client setting `ap_data->deathlink_receive` kills the local player. Implemented in `deathlink.c` / `deathlink.h`, gated on `ap_menu_settings.deathlink_enabled`.

Air Ride / City Trial and Top Ride are two separate implementations. Top Ride has no rider, machine, HP or fall-death system, so it gets its own send hook (the SAND-course sand pit) and its own receive effect (a random damage state), wired through `OnTopRideLoadEnd` because TR loads on minor 19 and never fires `On3DLoadEnd`.

## Sending Deaths

Three hooks detect player deaths, all applied from `DeathLink_OnBoot`:

| Hook addr | Vanilla function | Trigger |
|-----------|------------------|---------|
| `0x801a06d0` | `Rider_CheckToDieOnMachine` | HP death — `Machine_IsDead` returned true (machine HP reached zero) |
| `0x801e6540` | `Machine_SetFallDead` | Fall death — machine went out of bounds |
| `0x80331a94` | per-frame TR-stage function at `0x8033158c` | Top Ride sand pit ejected a swallowed kirby |

All three funnel through `DeathLinkSendAllowed()`, which requires (a) the reentrancy guard `applying_deathlink` to be clear — so the receive path's own `Machine_SetFallDead` / `Ply_SetHP` calls don't echo a death back out — and (b) `ap_menu_settings.deathlink_enabled` to be set. Each hook then applies its own human-vs-CPU filter before setting the flag, because the two engines discriminate differently: the 3D path (`SendDeathLink`) uses `Ply_CheckIfCPU`, the TR path uses `TopRide_GetPlayerKind == TR_PKIND_HMN`.

### `Machine_SetFallDead` hook register preservation

The hook at `0x801e6540` is inside `Machine_SetFallDead`, 0x20 bytes past the function entry at `0x801e6520`. At this point, the function prologue has saved r31 = md (non-volatile), but the argument registers r3 (md), r4 (ground_handle), and r5 (respawn_pos) are still volatile and in active use by later instructions:

- r4 is needed by the clobbered instruction `stw r4, 0x1b48(r31)` (replayed after the hook)
- r5 is needed at `0x801e6548` to copy respawn_pos into the machine data
- r3 is needed at `0x801e6588` where a per-vehicle-kind vtable function is called via `bctrl`

The hook's prologue saves r4/r5 to the stack and the epilogue restores all three (r3 from r31, r4/r5 from stack).

## Receiving Deaths

`DeathLink_PerFrame` runs as a GObj update function created in `DeathLink_On3DLoadEnd`, called from `On3DLoadEnd` for the 3D modes (Air Ride, City Trial, the stadiums). It early-returns until `Gm_GetIntroState() == GMINTRO_END`, then on `ap_data->deathlink_receive == 1` walks all 5 player slots and, for each human rider (`Ply_GetPKind(i) == PKIND_HMN`) currently on a machine (`Rider_IsOnMachine`), calls `KillPlayer(rd, md)` under the `applying_deathlink` reentrancy guard. After killing everyone it enqueues a "Deathlink received!" textbox and clears `deathlink_receive`.

### Kill Mechanism by Mode

`KillPlayer` (deathlink.c) chooses one of two mechanisms based on the current mode/stadium (`Gm_GetCurrentStadiumKind`, `Gm_IsInCity`, `Gm_IsDestructionDerby`):

| Mode / Stadium | Mechanism | How |
|----------------|-----------|-----|
| City Trial (`Gm_IsInCity`) | HP death | `Ply_SetHP(ply, 0)` |
| Destruction Derby (`Gm_IsDestructionDerby`) | HP death | `Ply_SetHP(ply, 0)` |
| Vs. King Dedede (`STKIND_VSKINGDEDEDE`) | HP death | `Ply_SetHP(ply, 0)` |
| Kirby Melee 1 / 2 (`STKIND_MELEE1` / `STKIND_MELEE2`) | HP death | `Ply_SetHP(ply, 0)` |
| Air Ride + all other stadiums | Fall death | `Machine_SetFallDead(md, -1, pos)` |

The HP-death stadiums run **outside** `Gm_IsInCity` but use CT-style HP-based death; routing them through the fall-death path instead would no-op or misbehave, since they have no out-of-bounds spline to respawn to.

**HP death path:** Before zeroing HP, it copies `md->dmg_log` into a local `DmgLog`, clears its `attacker_ply` (so the death is not attributed to any player), and calls `Ply_AddDeath(ply, &dl, md->is_bike, md->kind)` for stat tracking. Then `Ply_SetHP(ply, 0)` triggers the normal death flow.

**Fall death path:** `Machine_SetFallDead(md, -1, pos)` triggers a fall-off-course death; the machine flies off the track and respawns at the last spline checkpoint.

The checkpoint position `pos` is selected by the backup flag at `md->xc37` bit 6: clear = `respawn_pos` (0x8A8, primary checkpoint), set = `backup_respawn_pos` (0x8C0, last-known-good checkpoint saved when the per-frame spline lookup fails). This matches vanilla `Machine_CheckFallDeath`'s OOB-distance path, which also passes ground handle -1 when no dead zone surface is found — the global dead zone system respawns correctly with an invalid handle, and deathlink kills happen mid-track where `Machine_GetGroundHandle` would return -1 anyway.

The mpColl position (md+0x6F8 at +0x8/+0xC/+0x10) is **not** an alternative here: it stores world-space XYZ, and `Machine_SetFallDead` expects spline parameters, so passing it produces incorrect respawns.

## Checkpoint System

`respawn_pos` at md+0x8A8 is **not world-space XYZ** — it stores mpColl spline parameters:
- `[0]` = spline segment index (uint, raw-copied into the float field)
- `[1]` = normalized progress within the spline segment (float 0.0–1.0)
- `[2]` = Y height offset (float)

### Per-Frame Checkpoint Update

**`Machine_UpdateCheckpoint`** (0x801CE268): Runs every frame while the machine is alive (gated by fall-dead flag at 0xC3A bit 1). Flow:
1. Saves current `respawn_pos` → `prev_respawn_pos` (0x8B4)
2. Calls `Machine_GetMpCollPosition` (0x801CE954) to read the machine's position from its mpColl object (md+0x6F8, offsets +0x8/+0xC/+0x10)
3. Calls `Machine_CheckpointLookup` (0x800CEF84) → `GrCourseSpline_ResolveCheckpoint` (0x800E0E04) to find the nearest spline checkpoint
4. If checkpoint found: updates `respawn_pos` (0x8A8). If backup flag was set, copies new checkpoint to `prev_respawn_pos` and clears flag.
5. If checkpoint NOT found: copies `prev_respawn_pos` → `backup_respawn_pos` (0x8C0) and sets flag at 0xC37 bit 6

**`Machine_InitCheckpoint`** (0x801CE364): Called during machine creation. Initializes `respawn_pos` to `{0xFFFFFFFF, 0.0, 0.0}`, clears backup flag, then immediately runs a checkpoint lookup. After the lookup, copies result to `prev_respawn_pos`.

### MachineData Checkpoint Fields

| Offset | Field | Purpose |
|--------|-------|---------|
| 0x8A8 | `respawn_pos[3]` | Primary checkpoint (mpColl spline params) |
| 0x8B4 | `prev_respawn_pos[3]` | Previous frame's respawn_pos |
| 0x8C0 | `backup_respawn_pos[3]` | Last-known-good checkpoint, saved when lookup fails |
| 0xC37 bit 6 | `use_backup_checkpoint` | Set when spline lookup fails; cleared on success |

### Spline Lookup Details

`GrCourseSpline_ResolveCheckpoint` (0x800E0E04) is the core checkpoint resolver. It:
1. Uses `dBodyGetMass` to get the machine's collision body
2. Iterates spline segments near the machine via `zz_800ddd1c_` / `zz_800dd9a8_`
3. Projects the machine's position onto each segment, finding the closest match
4. Computes normalized progress along the segment and within the full course
5. Returns the spline segment index and progress as the checkpoint

The lookup finds the **nearest** spline point by proximity, not by track progress. Near course crossings, overlaps, or the start/finish line, the nearest spline point may be ahead of the player's actual progress.

## Ground System Reference

The game has two death zone systems:

| System | Type | Scope | Data Location |
|--------|------|-------|---------------|
| Local dead zones | Type 0x19 in a collision zone | Per-boundary, specific areas | `GrObj.coll.zone + handle*0x140 + 0x24` |
| Global dead zones | Y-height threshold | Stage-wide | `GrData → +0x20 → +0x24` |

**GrObj fields used here** — the collision zones are `GrObj.coll.zone` (0x140 bytes per zone
record) and `GrObj.coll.zone_num`. Two more sit inside the unmapped tail:
- `+0x454`: `void*` — rail validation data (`rail_all`)
- `+0x458`: `void*` — rail entries table (8 bytes per entry: JObj*, transform*)

**Zone record** (0x140 bytes):
- `+0x24`: `int` — zone type. Lower 25 bits are the type kind. `0x19` = local dead zone.
- `+0x138`: `void*` — pointer to local dead position data

**`Gr_IsValidGroundHandle(handle)`** (0x800D1F3C): Returns 0 if `0 <= handle < max_handles`, else 1.

**`Machine_GetGroundHandle(surface_id)`** (0x80247FAC): Takes an mpColl collision object pointer (md+0x6F8). Searches its entries for ground zones with type 0x19. Returns the ground handle index, or -1.

**`Machine_CheckFallDeath`** (0x801E6464): Per-frame check called from `Machine_EnvCollThink`. Two paths to `Machine_SetFallDead`:
1. Valid ground handle from surface (`Gr_IsValidGroundHandle` returns 0) → immediate fall death with `respawn_pos`
2. `calcDistanceFromOOB(md->pos) < threshold` → fall death, selects checkpoint by flag at md+0xC37 bit 6: clear = `respawn_pos` (0x8A8), set = `backup_respawn_pos` (0x8C0)

## Respawn Flow (Air Ride / Top Ride)

### Data Storage

`Machine_SetFallDead` (0x801E6520) stores:
- md+0x1B48: `ground_handle` (int)
- md+0x1B4C: `respawn_pos[0]` (spline segment index)
- md+0x1B50: `respawn_pos[1]` (progress)
- md+0x1B54: `respawn_pos[2]` (Y offset)
- md+0x1B58: timestamp from game frame counter

This data is preserved through the fall animation (read-only by `Machine_ApplyFallVelocity`).

### Respawn State Machine

1. `Machine_SetFallDead` → machine enters fall-dead state, plays death animation
2. `Respawner_Update` (0x8000FF78) counts down per-player timer: 150 → 0
   - At 90: camera fade effect spawned
   - At 30: respawn triggered — sets flag via `zz_8022cc80_`, checks permadeath
   - At 0: cleanup
3. `AS_DeadWait` / `Rider_DeadHitGround_Anim` → calls `Rider_RespawnEnter` (0x801A1D70)
4. `Rider_RespawnAnim` (0x801A1DEC) → destroys old machine, creates new one via `Machine_Create`
5. `Machine_RespawnDispatch` (0x801EB738) dispatches by respawn type:
   - Type 0: default — `Machine_SetMpCollPosition` restores mpColl from spline data
   - Type 4 (fall dead): `Machine_FallDeadRespawnEntry` (0x801E4EC4) — loads stored 0x1B48 data, enters respawn state
6. `Machine_ApplyRespawnPacket` (0x801CC0C4) restores velocity, stats, and position to the new machine

### Respawn Packet

`Machine_BuildRespawnPacket` (0x801CBE5C) serializes machine state before destruction:
- Packet[0..2]: current velocity
- Packet[4]: respawn type (0=default, 1=ground, 2=rail, 3=special, 4=fall dead)
- Packet[7..9]: current mpColl position (from `Machine_GetMpCollPosition`)
- Packet[0x18+]: for type 4 — stored fall-dead data from 0x1B48 (ground_handle, checkpoint, timestamp)

## Known Limitations

Checkpoint density varies by course. On sparse courses a fall-death respawn can put the player noticeably behind where they died — the same behavior as a natural fall death, since the receive path reuses the vanilla checkpoint system unmodified.

## Top Ride Send

The sand-pit enemy on the SAND course is the death proxy: `DeathLink_OnTopRideSandPit(kirby)` sets `deathlink_send = 1` when the pit spits a swallowed human kirby back out. It checks `applying_deathlink` clear, `deathlink_enabled` set, `TR_PKIND_HMN`, and `round_state == 2`. The pit's eject is a discrete event, not a per-frame tick, so no rising-edge gate is needed.

The hook site is `0x80331a94`, inside the per-frame TR-stage function at `0x8033158c` that loops all 4 kirby slots and dispatches the eject knockback. Kirby is in r31. The clobbered instruction is `lwz r12, 0xd0(r12)` — vt+0xD0 is the `KirbyDoodlebugOut` wrapper; the other vt+0xD0 call site (`0x802e2804`, the Doodlebug item) is deliberately **not** hooked. The epilogue rebuilds r3 (=kirby), r4 (=stack+0x90), r5 (=stack+0x84), r6 (=30), r7 (=60) and r12 from r31 / r1 / immediates so the imminent vtable `bctrl` still has its arguments.

### Why not other TR scenery / damage paths

Several other terrain-driven damage states are excluded:

- **KirbyBurn (lava / fire tiles)** — `BurnAreaTickAll` (`zz_803218dc_`) calls `KirbyBurnMethod` once per kirby per frame, AND KirbyBurn's per-frame tick transitions back to Normal between frames. Neither a vtable-equality gate nor a state-ID gate suppresses the per-frame spam; only a per-kirby frame-counter rising-edge gate would, and the complexity isn't worth it for the lava case alone.
- **KirbySpin / KirbySandSpin / KirbyWhirlpool** (spin-class effector functions at `0x802e7570 / 0x802e7750 / 0x802e79a4`) — none of these fire for the in-game sand pit. The sand-pit ejection uses `KirbyDoodlebugOut` instead.
- **KirbyCrush** (heavy-machine landing on a kirby) is kirby-vs-machine, not pure terrain.
- **KirbyFreeze** is item-derived in TR (the Freeze projectile item, not a stage hazard).
- **KirbyPress** has both terrain-effector and physics-internal entry paths; neither is unambiguously scenery.
- **TR has no fall-death.** Courses are bounded 2D arenas; no off-track or pit hazards exist.

## Top Ride Receive

`DeathLink_TopRidePerFrame` runs as a GObj update function created by `DeathLink_OnTopRideLoadEnd` (called from `main.c::OnTopRideLoadEnd`). On `deathlink_receive == 1` (and once `round_state == 2`), it picks **one** random state from a damage-class pool via `HSD_Randi(DEATHLINK_STATE_COUNT)`, then applies that **same** state to every human kirby — it iterates `mgr->kirbys[0..3]`, filters humans via `TopRide_GetPlayerKind(kirby->player_slot) == TR_PKIND_HMN`, and calls the chosen state wrapper on each. It finishes by enqueuing a "Deathlink received!" textbox and clearing `deathlink_receive`.

This replaces the AR/CT kill path entirely: Top Ride has no rider/machine/HP/fall-death system, so there is nothing to zero or to fall off of. A damage state is the closest analog to "death".

### State Pool

The pool is `deathlink_states[]` in `deathlink.c`, each entry a `static inline` wrapper in `topride.h` that invokes a non-virtual Kirby vtable method (vtable at `0x804d2304`):

| Idx | Wrapper (`topride.h`) | vtable slot | Method | State |
|-----|-----------------------|-------------|--------|-------|
| 0 | `TopRide_KirbyPress`   | `[55]` / +0xDC  | `TopRide_KirbyPressMethod` (0x802d54ec)   | Press (squeezed/flat) |
| 1 | `TopRide_KirbyFreeze`  | `[57]` / +0xE4  | `TopRide_KirbyFreezeMethod` (0x802d56bc)  | Freeze (frozen solid) |
| 2 | `TopRide_KirbyNumb`    | `[64]` / +0x100 | `TopRide_KirbyNumbMethod` (0x802d5b74)    | Numb (paralysis) |
| 3 | `TopRide_KirbyConfuse` | `[66]` / +0x108 | `TopRide_KirbyConfuseMethod` (0x802d5c64) | Confuse (controls scrambled) |

`SpeedDown` is reserved for traplink. `Burn`, `Spin`, `Crush`, `Strike`, `Explode`, and `Elec` are deliberately excluded: their setters either NULL-deref without an originating item or derive their parameters from one. Each wrapper passes zero args, producing a static stun: the state's animation plays in place with no knockback impulse.

### Velocity Neutralization

Around each `apply(kirby)` call the proc zeros the kirby's `ChargeComponent.velocity` (`kirby+0xA0` = inline charge component base `+0x80` plus velocity field `+0x20`) **both before and after**:

- **Pre-zero** pre-empts setters that read `kirby+0xA0` and rescale it (e.g. the AC_TOBASARE / knockback-class setters).
- **Post-zero** overrides setters that ignore `kirby+0xA0` and instead `PSVECNormalize` the (zero) Vec3 arg into NaN and write that back.

The result is no residual launch impulse. See `topride-kirby-states.md` for the per-setter details.

### Round-State Gate

The receive proc gates on `mgr->round_state == 2` (race active; field at `TopRideKirbyMgr+0x4028`). The kirby state machine is not fully wired up before this — the state wrappers dereference `state_handler` and its vtable, which are NULL / partially-initialized during countdown and would crash. A `deathlink_receive` flag arriving early simply persists until the race starts and is consumed on the first qualifying frame.


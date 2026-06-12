# Deathlink

## Overview

Deathlink synchronizes deaths between players in a multiworld. When enabled, dying in Kirby Air Ride sends a death signal to other players, and receiving one kills the local player.

**Files:** `deathlink.c` / `deathlink.h`

**Top Ride uses the sand-pit enemy as the death proxy.** TR has no rider/machine/HP/fall-death system, so the AR/CT send hooks (`Rider_CheckToDieOnMachine`, `Machine_SetFallDead`) do not fire there. Instead, TR send fires when a human kirby gets swallowed by the sand-pit enemy on the SAND course and spit back out. See "Top Ride Send" below.

Receive is wired separately via `DeathLink_OnTopRideLoadEnd` (called from `OnTopRideLoadEnd` since TR uses minor 19, not 18, so `On3DLoadEnd` does not fire). The TR receive path picks one random damage state from {Press, Freeze, Numb, Confuse} and applies that same state to all human kirbys — see "Top Ride Receive" below.

## Sending Deaths

Two hooks detect player deaths:

1. **HP death** — `HOOKCREATE` at `0x801a06d0` in `Rider_CheckToDieOnMachine`. Fires when `Machine_IsDead` returns true (machine HP reaches zero). Sets `ap_data->deathlink_send = 1`.

2. **Fall death** — `HOOKCREATE` at `0x801e6540` inside `Machine_SetFallDead`. Fires when a machine falls out of bounds. At this hook point, r31 = MachineData*, r4 = ground_handle, r5 = respawn_pos. The hook must save/restore r3, r4, r5 (volatile registers still in use by the function) via explicit prologue/epilogue assembly.

Both hooks funnel through the common gate `DeathLinkSendAllowed()`, which requires (a) the reentrancy guard `applying_deathlink` to be clear — avoiding echoing deaths back when the receive path triggers `Machine_SetFallDead` or `Ply_SetHP` — and (b) `ap_menu_settings.deathlink_enabled` to be set. Each hook then applies its own mode-specific human-vs-CPU filter before setting the flag: the 3D path (`SendDeathLink`) skips CPUs via `Ply_CheckIfCPU`; the TR path uses `TopRide_GetPlayerKind == TR_PKIND_HMN`.

### Machine_SetFallDead Hook Register Preservation

The hook at `0x801e6540` is inside `Machine_SetFallDead`, 0x20 bytes past the function entry at `0x801e6520`. At this point, the function prologue has saved r31 = md (non-volatile), but the argument registers r3 (md), r4 (ground_handle), and r5 (respawn_pos) are still volatile and in active use by later instructions:

- r4 is needed by the clobbered instruction `stw r4, 0x1b48(r31)` (replayed after the hook)
- r5 is needed at `0x801e6548` to copy respawn_pos into the machine data
- r3 is needed at `0x801e6588` where a per-vehicle-kind vtable function is called via `bctrl`

The hook's prologue saves r4/r5 to the stack and the epilogue restores all three (r3 from r31, r4/r5 from stack).

## Receiving Deaths

`DeathLink_PerFrame` runs as a GObj update function (created in `DeathLink_On3DLoadEnd`, which is called from `main.c::On3DLoadEnd` for the 3D modes — Air Ride, City Trial, and the stadiums). It early-returns until `Gm_GetIntroState() == GMINTRO_END`, then on `ap_data->deathlink_receive == 1` walks all 5 player slots and, for each human rider (`Ply_GetPKind(i) == PKIND_HMN`) that is currently on a machine (`Rider_IsOnMachine`), calls `KillPlayer(rd, md)` under the `applying_deathlink` reentrancy guard. After killing everyone it enqueues a "Deathlink received!" textbox notification and clears `deathlink_receive`.

Top Ride is **not** handled here — it has no rider/machine and uses minor 19, so neither this proc nor `On3DLoadEnd` ever fires there. See "Top Ride Receive" below.

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

The checkpoint position `pos` is selected by the backup flag at `md->xc37` bit 6: clear = `respawn_pos` (0x8A8, primary checkpoint), set = `backup_respawn_pos` (0x8C0, last-known-good checkpoint saved when the per-frame spline lookup fails). This matches the vanilla `Machine_CheckFallDeath` OOB-distance path behavior.

Ground handle is passed as -1. The vanilla game does this in `Machine_CheckFallDeath`'s OOB-distance branch when no dead zone surface is found. The global dead zone system handles respawn correctly with an invalid handle.

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
| Local dead zones | Type 0x19 in ground entries | Per-boundary, specific areas | `*(GrObj+0x74) + handle*0x140 + 0x24` |
| Global dead zones | Y-height threshold | Stage-wide | `GrData → +0x20 → +0x24` |

**GrObj layout** (fields beyond what's in `stage.h`):
- `+0x74`: `int*` — base pointer to ground entries array (0x140 bytes per entry)
- `+0x78`: `int` — number of ground entries
- `+0x454`: `void*` — rail validation data (`rail_all`)
- `+0x458`: `void*` — rail entries table (8 bytes per entry: JObj*, transform*)

**Ground entry** (0x140 bytes):
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

## Design Notes (Deathlink Receive)

### Checkpoint-Based Respawn

The deathlink receive uses the game's existing checkpoint system for respawn positioning. `respawn_pos` stores spline parameters (not world XYZ), updated per-frame by `Machine_UpdateCheckpoint`. The backup checkpoint is used when the spline lookup has failed (xc37 bit 6 set). This matches vanilla fall-death behavior.

Checkpoint density varies by course — some courses have sparse checkpoints, meaning the player may respawn noticeably behind where they died. This is consistent with natural fall deaths in the vanilla game.

### Why Not mpColl Position

The mpColl position (md+0x6F8 at offsets +0x8/+0xC/+0x10) stores **world-space XYZ coordinates**, not spline parameters. Passing these to `Machine_SetFallDead` (which expects spline params) causes incorrect respawns. The checkpoint spline params in `respawn_pos` are the correct format.

### Why -1 for Ground Handle

Deathlink kills happen mid-track where the machine's collision surface typically has no dead zone entry, so `Machine_GetGroundHandle` would return -1 anyway. The vanilla game passes -1 in `Machine_CheckFallDeath`'s OOB-distance path, and the global dead zone system handles it correctly.

## Top Ride Send

Single hook on the sand-pit enemy's "spit kirby out" path. Sets `deathlink_send = 1` for human kirbys, gated by `deathlink_enabled` and the `applying_deathlink` reentrancy guard.

| Hazard | Hook addr | Vanilla function | Kirby reg | Notes |
|--------|-----------|------------------|-----------|-------|
| Sand-pit enemy (SAND course) | `0x80331a94` | per-frame TR-stage function at `0x8033158c` | r31 | Clobbered: `lwz r12, 0xd0(r12)` (vt+0xD0 = `KirbyDoodlebugOut` wrapper). The function loops over all 4 kirby slots and dispatches the eject-from-pit knockback. The other vt+0xD0 call site (0x802e2804, Doodlebug item) is intentionally NOT hooked. r3 (=kirby), r4 (=stack+0x90), r5 (=stack+0x84), r6 (=30), r7 (=60), and r12 are restored in the epilogue from r31 + r1 + immediates so the imminent vtable bctrl has its args. |

`DeathLink_OnTopRideSandPit(kirby)` checks: not in receive (`applying_deathlink`), `deathlink_enabled` set, `TR_PKIND_HMN`, `round_state == 2`. The function only fires when the pit decides to eject a swallowed kirby (a discrete event, not a per-frame tick), so no state-transition or rising-edge gate is needed.

### Why not other TR scenery / damage paths

Several other terrain-driven damage states were investigated and excluded:

- **KirbyBurn (lava / fire tiles)** — `BurnAreaTickAll` (`zz_803218dc_`) calls `KirbyBurnMethod` once per kirby per frame, AND KirbyBurn's per-frame tick transitions back to Normal between frames. Both a vtable-equality gate and a state-ID gate failed to suppress per-frame spam; only a per-kirby frame-counter rising-edge gate worked, but the complexity wasn't worth it for the lava case alone.
- **KirbySpin / KirbySandSpin / KirbyWhirlpool** (spin-class effector functions at `0x802e7570 / 0x802e7750 / 0x802e79a4`) — all hooked at one point, but none fired for the actual in-game sand pit. The sand-pit ejection uses `KirbyDoodlebugOut` instead.
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

`SpeedDown` is reserved for traplink (see `traplink-send.md`). `Burn`, `Spin`, `Crush`, `Strike`, `Explode`, and `Elec` are deliberately excluded — see `docs/topride-kirby-states.md` "Caveats & Open Items" for the per-state reasoning (NULL-deref hazards in their setters, item-only derivation, etc.). Each wrapper passes zero args, producing a static stun: the state's animation plays in place with no knockback impulse.

### Velocity Neutralization

Around each `apply(kirby)` call the proc zeros the kirby's `ChargeComponent.velocity` (`kirby+0xA0` = inline charge component base `+0x80` plus velocity field `+0x20`) **both before and after**:

- **Pre-zero** pre-empts setters that read `kirby+0xA0` and rescale it (e.g. the AC_TOBASARE / knockback-class setters).
- **Post-zero** overrides setters that ignore `kirby+0xA0` and instead `PSVECNormalize` the (zero) Vec3 arg into NaN and write that back.

The result is no residual launch impulse. See `docs/topride-kirby-states.md` for the per-setter details.

### Round-State Gate

The receive proc gates on `mgr->round_state == 2` (race active; field at `TopRideKirbyMgr+0x4028`). The kirby state machine is not fully wired up before this — the state wrappers dereference `state_handler` and its vtable, which are NULL / partially-initialized during countdown and would crash. A `deathlink_receive` flag arriving early simply persists until the race starts and is consumed on the first qualifying frame.


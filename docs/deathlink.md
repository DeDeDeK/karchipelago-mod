# DeathLink

DeathLink synchronizes deaths between players in a multiworld: dying in Kirby Air Ride sets `ap_data->deathlink_send`, and the client setting `ap_data->deathlink_receive` kills the local player. Implemented in `mods/archipelago/src/deathlink.c`, gated on `ap_menu_settings.deathlink_enabled`.

Air Ride / City Trial and Top Ride are two separate implementations. Top Ride has no rider, machine, HP or fall-death system, so it gets its own send hook (the SAND-course sand pit) and its own receive effect (a random damage state), wired through `OnTopRideLoadEnd` because TR loads on scene minor 19 and never fires `On3DLoadEnd`.

## Sending Deaths

Three hooks detect player deaths, all applied from `DeathLink_OnBoot`:

| Hook addr | Vanilla function | Trigger |
|-----------|------------------|---------|
| `0x801a06d0` | `Rider_CheckToDieOnMachine` (0x801a06a8) | HP death - `Machine_IsDead` returned true (machine HP reached zero) |
| `0x801e6540` | `Machine_SetFallDead` (0x801e6520) | Fall death - machine went out of bounds |
| `0x80331a94` | per-frame TR-stage function at `0x8033158c` | Top Ride sand pit ejected a swallowed kirby |

All three funnel through `DeathLinkSendAllowed(ply)`, which requires `deathlink_enabled` and a clear echo-suppression slot. Each hook then applies its own human-vs-CPU filter, because the two engines discriminate differently: the 3D path (`SendDeathLink`) uses `Ply_CheckIfCPU`, the TR path uses `TopRide_GetPlayerKind == TR_PKIND_HMN`.

### Echo suppression

The receive path kills the local player through the same mechanisms the send hooks watch, so its own kills must not bounce back out as a send. This cannot be a guard around the kill call, because the HP-death path is **asynchronous**:

1. `Ply_SetHP(ply, 0)` (0x8022ca38) writes the HP float and forwards to `MachineGObj_SetHP` (0x801c841c). Neither touches the dead flag.
2. `Machine_IsDead` (0x801c856c) reads `md->is_dead`, set only by `Machine_OnKO` (0x801e568c) once a later machine-think frame observes HP <= 0.
3. `RiderThink_DmgApply` (0x8018fa20) polls `Rider_CheckToDieOnMachine` on a later frame still, and that is where the send hook sits.

So `deathlink_suppress[5]` is a per-player frame countdown (`DEATHLINK_SUPPRESS_FRAMES` = 60), armed by `SuppressSend(ply)` immediately before each `KillPlayer` call, decremented once per frame by `TickSuppress()` inside both receive procs, and consumed (zeroed) by the first send attempt it blocks. `ClearSuppress()` zeroes it in `DeathLink_On3DLoadEnd` / `DeathLink_OnTopRideLoadEnd` so nothing carries across scenes.

60 frames covers the multi-frame HP-death detection chain with margin while staying far short of the 150-frame respawn timer, so a genuine death can never land inside the window - the player is mid-respawn for its entire duration. The fall-death path trips its hook synchronously inside `Machine_SetFallDead` and is covered by the same arm.

### `Machine_SetFallDead` hook register preservation

The hook at `0x801e6540` is 0x20 bytes past the function entry. The prologue has already saved r31 = md (non-volatile), but the argument registers are still volatile and in active use downstream:

- r4 is needed by the clobbered instruction `stw r4, 0x1b48(r31)` (replayed after the hook)
- r5 is needed at `0x801e6548` to copy respawn_pos into the machine data
- r3 is needed at `0x801e6588` where a per-vehicle-kind vtable function is called via `bctrl`

The hook's prologue saves r4/r5 to the stack and the epilogue restores all three (r3 from r31, r4/r5 from stack).

## Receiving Deaths

`DeathLink_PerFrame` runs as a GObj update function created in `DeathLink_On3DLoadEnd`, called from `On3DLoadEnd` for the 3D modes (Air Ride, City Trial, the stadiums). It early-returns until `Gm_GetIntroState() == GMINTRO_END`, then on `deathlink_receive == 1` walks all 5 player slots and, for each human rider (`Ply_GetPKind(i) == PKIND_HMN`) currently on a machine (`Rider_IsOnMachine`), arms that slot's echo suppression and calls `KillPlayer(rd, md)`. After killing everyone it enqueues a "Deathlink received!" textbox and clears `deathlink_receive`.

### Kill mechanism by mode

`KillPlayer` chooses one of two mechanisms from the current mode/stadium (`Gm_GetCurrentStadiumKind`, `Gm_IsInCity`, `Gm_IsDestructionDerby`):

| Mode / Stadium | Mechanism |
|----------------|-----------|
| City Trial (`Gm_IsInCity`) | HP death - `Ply_SetHP(ply, 0)` |
| Destruction Derby (`Gm_IsDestructionDerby`) | HP death |
| Vs. King Dedede (`STKIND_VSKINGDEDEDE`) | HP death |
| Kirby Melee 1 / 2 (`STKIND_MELEE1` / `STKIND_MELEE2`) | HP death |
| Air Ride + all other stadiums | Fall death - `Machine_SetFallDead(md, -1, pos)` |

The HP-death stadiums run **outside** `Gm_IsInCity` but use CT-style HP-based death; routing them through the fall-death path instead would no-op or misbehave, since they have no out-of-bounds spline to respawn to.

Before zeroing HP, `KillPlayer` copies `md->dmg_log` into a local `DmgLog`, clears its `attacker_ply` (so the death is not attributed to any player), and calls `Ply_AddDeath` for stat tracking. `Ply_SetHP(ply, 0)` then triggers the normal death flow.

The fall-death path passes the checkpoint selected by `md->use_backup_checkpoint`: clear = `respawn_pos`, set = `backup_respawn_pos` (the last-known-good checkpoint saved when the per-frame spline lookup fails). This matches vanilla `Machine_CheckFallDeath`'s OOB-distance path, which also passes ground handle -1 when no dead zone surface is found - the global dead zone system respawns correctly with an invalid handle, and deathlink kills happen mid-track where `Machine_GetGroundHandle` would return -1 anyway.

The mpColl position (md+0x6F8 at +0x8/+0xC/+0x10) is **not** an alternative here: it stores world-space XYZ, while `Machine_SetFallDead` expects spline parameters, so passing it produces incorrect respawns.

## Checkpoint System

`respawn_pos` is **not world-space XYZ**. It holds three mpColl spline parameters: the spline segment index (a uint raw-copied into a float field), normalized progress within that segment (0.0-1.0), and a Y height offset. The declarations for it, `prev_respawn_pos`, `backup_respawn_pos` and the `use_backup_checkpoint` bit are in `externals/hoshi/include/machine.h`.

`Machine_UpdateCheckpoint` (0x801ce268) runs every frame while the machine is alive (gated by the fall-dead flag at md+0xC3A bit 1). It saves the current `respawn_pos` into `prev_respawn_pos`, reads the machine's position out of its mpColl object via `Machine_GetMpCollPosition` (0x801ce954), and resolves the nearest spline checkpoint through `Machine_CheckpointLookup` (0x800cef84) -> `GrCourseSpline_ResolveCheckpoint` (0x800e0e04). On success it updates `respawn_pos` and clears the backup flag; on failure it copies `prev_respawn_pos` into `backup_respawn_pos` and sets the flag. `Machine_InitCheckpoint` (0x801ce364) runs at machine creation, seeding `respawn_pos` to `{0xFFFFFFFF, 0.0, 0.0}` and immediately running one lookup.

`GrCourseSpline_ResolveCheckpoint` projects the machine's position onto the spline segments near it and returns the closest one plus normalized progress. It matches by **proximity, not track progress**, so near course crossings, overlaps, or the start/finish line the nearest spline point can be ahead of where the player actually is.

## Ground and Dead Zone Systems

Two death zone systems exist. **Local dead zones** are per-boundary: a collision zone record (0x140 bytes, in `GrObj.coll.zone`) whose type field at +0x24 has kind `0x19`. **Global dead zones** are a stage-wide Y-height threshold reached through `GrData` -> +0x20 -> +0x24.

- `Gr_IsValidGroundHandle(handle)` (0x800d1f3c) returns 0 for `0 <= handle < max_handles`, 1 otherwise.
- `Machine_GetGroundHandle(surface_id)` (0x80247fac) takes an mpColl collision object pointer and searches its entries for a type-0x19 ground zone, returning that handle index or -1.
- `Machine_CheckFallDeath` (0x801e6464), called per frame from `Machine_EnvCollThink`, reaches `Machine_SetFallDead` two ways: a valid ground handle from the surface, or `calcDistanceFromOOB(md->pos)` under threshold. The second path is the one deathlink imitates.

## Respawn Flow

`Machine_SetFallDead` (0x801e6520) stashes the ground handle, the three checkpoint floats and a frame-counter timestamp at md+0x1B48..0x1B58. That block survives the fall animation (`Machine_ApplyFallVelocity` only reads it) and drives the respawn:

1. The machine enters fall-dead state and plays the death animation.
2. `Respawner_Update` (0x8000ff78) counts a per-player timer down from 150: camera fade at 90, respawn triggered (and permadeath checked) at 30, cleanup at 0.
3. `AS_DeadWait` / `Rider_DeadHitGround_Anim` call `Rider_RespawnEnter` (0x801a1d70).
4. `Rider_RespawnAnim` (0x801a1dec) destroys the old machine and creates a new one via `Machine_Create`.
5. `Machine_RespawnDispatch` (0x801eb738) dispatches on respawn type. Type 0 (default) restores the mpColl position from the spline data via `Machine_SetMpCollPosition`; type 4 (fall dead) goes to `Machine_FallDeadRespawnEntry` (0x801e4ec4), which reloads the stored md+0x1B48 block.
6. `Machine_ApplyRespawnPacket` (0x801cc0c4) restores velocity, stats and position onto the new machine.

The packet itself is serialized before destruction by `Machine_BuildRespawnPacket` (0x801cbe5c): current velocity, respawn type (0 default, 1 ground, 2 rail, 3 special, 4 fall dead), current mpColl position, and for type 4 the stored fall-dead block.

Checkpoint density varies by course, so on sparse courses a deathlink fall-death respawn can put the player noticeably behind where they died. That is the same behavior as a natural fall death - the receive path reuses the vanilla checkpoint system unmodified.

## Top Ride Send

The sand-pit enemy on the SAND course is the death proxy: `DeathLink_OnTopRideSandPit(kirby)` sets `deathlink_send = 1` when the pit spits a swallowed human kirby back out. It checks `deathlink_enabled`, a clear echo-suppression slot, `TR_PKIND_HMN`, and `round_state == 2`. The pit's eject is a discrete event, not a per-frame tick, so no rising-edge gate is needed.

The hook site `0x80331a94` is inside the per-frame TR-stage function at `0x8033158c` that loops all 4 kirby slots and dispatches the eject knockback; kirby is in r31. The clobbered instruction is `lwz r12, 0xd0(r12)` - vt+0xD0 is the `KirbyDoodlebugOut` wrapper. The other vt+0xD0 call site (`0x802e2804`, the Doodlebug item) is deliberately **not** hooked. The epilogue rebuilds r3 (=kirby), r4 (=stack+0x90), r5 (=stack+0x84), r6 (=30), r7 (=60) and r12 from r31 / r1 / immediates so the imminent vtable `bctrl` still has its arguments.

### Why no other TR scenery or damage path

- **KirbyBurn (lava / fire tiles)** - `BurnAreaTickAll` (`zz_803218dc_`) calls `KirbyBurnMethod` once per kirby per frame, and KirbyBurn's own per-frame tick transitions back to Normal between frames. Neither a vtable-equality gate nor a state-ID gate suppresses the resulting spam; only a per-kirby frame-counter rising edge would, which is not worth it for lava alone.
- **KirbySpin / KirbySandSpin / KirbyWhirlpool** (spin-class effectors at `0x802e7570 / 0x802e7750 / 0x802e79a4`) - none fire for the in-game sand pit, which ejects via `KirbyDoodlebugOut` instead.
- **KirbyCrush** (heavy machine landing on a kirby) is kirby-vs-machine, not terrain.
- **KirbyFreeze** in TR is item-derived (the Freeze projectile), not a stage hazard.
- **KirbyPress** has both terrain-effector and physics-internal entry paths; neither is unambiguously scenery.
- TR has no fall-death at all: courses are bounded 2D arenas with no off-track or pit hazards.

## Top Ride Receive

`DeathLink_TopRidePerFrame`, created by `DeathLink_OnTopRideLoadEnd`, picks **one** random state from a damage-class pool via `HSD_Randi(DEATHLINK_STATE_COUNT)` and applies that **same** state to every human kirby in `mgr->kirbys[0..3]`, arming each slot's echo suppression first. It then enqueues a "Deathlink received!" textbox and clears `deathlink_receive`.

This replaces the AR/CT kill path entirely: Top Ride has no rider/machine/HP/fall-death system, so there is nothing to zero or to fall off of. A damage state is the closest analog to "death".

### State pool

`deathlink_states[]` holds four `static inline` wrappers from `externals/hoshi/include/topride.h`, each invoking a non-virtual method on the Kirby vtable at `0x804d2304`:

| Wrapper | vtable slot | Method | State |
|---------|-------------|--------|-------|
| `TopRide_KirbyPress`   | `[55]` | 0x802d54ec | Press (squeezed/flat) |
| `TopRide_KirbyFreeze`  | `[57]` | 0x802d56bc | Freeze (frozen solid) |
| `TopRide_KirbyNumb`    | `[64]` | 0x802d5b74 | Numb (paralysis) |
| `TopRide_KirbyConfuse` | `[66]` | 0x802d5c64 | Confuse (controls scrambled) |

`SpeedDown` is reserved for traplink. `Burn`, `Spin`, `Crush`, `Strike`, `Explode` and `Elec` are excluded because their setters either NULL-deref without an originating item or derive their parameters from one. Each wrapper passes zero args, producing a static stun: the animation plays in place with no knockback impulse.

### Velocity neutralization

Around each `apply(kirby)` call the proc zeros the kirby's charge-component velocity (`kirby+0xA0` = inline charge component at +0x80 plus its velocity field at +0x20) **both before and after**. The pre-zero pre-empts setters that read that vector and rescale it (the knockback-class setters); the post-zero overrides setters that ignore it and instead `PSVECNormalize` the zero Vec3 argument into NaN and write that back. The result is no residual launch impulse.

### Round-state gate

The receive proc gates on `mgr->round_state == 2` (race active). The kirby state machine is not fully wired up before this - the state wrappers dereference `state_handler` and its vtable, which are NULL or partially initialized during countdown and would crash. A `deathlink_receive` flag arriving early simply persists until the race starts and is consumed on the first qualifying frame.

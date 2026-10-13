# Ability Gating

Two gating categories cover what Kirby can do. **Copy abilities** (11 bits, `CopyKind`, unlock
IDs 760-770) decide which abilities Kirby can pick up; **base abilities** (3 bits,
`BaseAbilityKind`, IDs 771-773) decide whether the inhale, the quick spin and the machine charge
work at all. Both are ordinary unlock masks in `APSave` (`ability_unlocked_mask`,
`base_ability_unlocked_mask`), pre-filled when their category ships ungated.

## Copy abilities

A locked ability cannot be gained from any source, and the panels and enemies that would offer
it are taken out of the round so the lock is invisible rather than teasing. Kirby gains an
ability two ways, and each is gated at its single commit point.

**Pickups and enemy touch** - a copy panel item, a copy-ability enemy - go through
`RiderGObj_CheckAndGiveAbility` (0x80192650), which checks the rider is Kirby and calls the raw
grant `Rider_GiveAbility` (0x801a81a4). The mod replaces it with the same checks plus the mask:
a locked kind returns 0. The replacement is also where TrapLink's sleep trigger lives - a human
that actually gains Sleep sends a TrapLink - and it tests `Rider_GiveAbility`'s result first,
since the grant fails in some rider states.

**The copy wheel** - the roulette that spins after inhaling an enemy or driving into a ground
copy panel - commits through `randomAbility_giveAbility` (0x801a61d4). The replacement swaps a
locked result for a random unlocked ability, so inhaling stays worthwhile as soon as anything is
unlocked, then runs the vanilla grant sequence. With nothing unlocked there is no substitute,
and it must still move the rider on: both callers reach it from a state whose only exit is the
grant, so returning without one leaves Kirby stuck for the match with no inhale and no quick
spin. It takes the engine's own "nothing to give" exit instead, `Rider_ResolveQueuedAbility`
(0x801a8454), which settles the rider in its idle star state.

The wheel's two callers, `randomAbility_aPress` (0x801ae7f4) and `randomAbility_autoSelect`
(0x801ae890), record the ability the wheel *showed* with `Ply_MarkCopyAbilityObtained`
(0x8022f150) after the grant. Both calls (0x801ae874, 0x801ae910) are redirected to mark the
kind actually granted, and nothing after an empty grant, so the obtained-abilities record matches
what Kirby got.

`Rider_GiveAbility` itself is left alone. AP copy-ability gives and EnergyLink purchases call it
directly, so a received ability applies whether or not its unlock has arrived - receiving it is
the grant.

### Spawn filtering

**Copy panels** come out of City Trial's spawn tables through the shared filter pass: the gate's
predicate maps a panel `ItemKind` to its `CopyKind` and reports it locked when that bit is clear.

**Enemies** whose ability is locked are removed from the stage's enemy spawn data at
`On3DLoadEnd`, by zeroing their weights - the data is reloaded from disc every stage load, so
editing it in place is safe. Zeroing weights rather than rejecting at spawn time keeps the
spawner from repeatedly picking a locked enemy and waiting out its respawn delay, which would
visibly thin the field. The data (`*stc_enemy_spawn_data`) is NULL wherever the stage spawns no
enemies - Top Ride, City Trial Free Run, any stage with enemies off - so the filter needs no mode
test.
`config->mode` picks one of three entry layouts:

| Mode | Where | Filter |
|---|---|---|
| 1 | Air Ride courses | Zero each locked enemy's weight in the entry's id/weight slots |
| 2 | Kirby Melee 1 | Zero locked enemies in every category column, then retire each category left with no positive weight |
| 3 | Kirby Melee 2 | As mode 1, at the mode-3 offsets |

Meta-enemy IDs 0x50-0x5E stand for a sub-table of real enemies; the filter cleans each
sub-table once and zeroes the meta entry when nothing in it survives. In mode 2,
`Enemy_SpawnerDecideMode2` (0x800f0efc) first picks a category by walking ascending thresholds,
so a threshold of 0 is never chosen and zeroing it retires the category; and it indexes a
category's weight column by its meta id minus 0x50, not by its position, which the filter
follows. An enemy's ability follows its archive slot, shared by all three tiers, with the
special Sword Knight the one special enemy that carries one.

### Top Ride

Top Ride has no copy abilities and no `RiderData`, so every hook above is unreachable there. The
mask still matters for the four ability-themed Top Ride items (Freeze Fan, Fire, Bomb, Walky):
each also unlocks with its copy ability while ability gating is on.

## Base abilities

Each bit gates one move in both engines: inhale; the quick spin (all three riders in 3D, the
stick-flick spin in Top Ride); and the machine charge. Until its item arrives the move does
nothing, and the moment it arrives the move works. Only humans are gated (`PKIND_HMN` in 3D,
`TR_PKIND_HMN` in Top Ride): charge is how every machine boosts, and a gated CPU could not race.

Because a move has to come back when its bit is set, every gate is a call-site patch that checks
the mask each time - a `CODEPATCH_REPLACEFUNC` would leave no path to the original. All are
installed at boot:

| Move | Site | In | Gate |
|---|---|---|---|
| Inhale | 0x8019c610 | `Rider_TryStartInhale` (0x8019c5ac) | Skip `bl Rider_StartInhale` |
| Quick spin, Kirby | 0x801b7ec0, 0x801b7e58 | `RiderState_QuickSpinInterrupt` (0x801b7e80), `Rider_TryQuickSpinNeutral` (0x801b7e0c) | Skip `bl RiderState_QuickSpinEnter` |
| Quick spin, Dedede | 0x801c05d4 | `RiderState_DededeQuickSpinInterrupt` (0x801c05a8) | Skip his enter |
| Quick spin, Meta Knight | 0x801c3f6c | `RiderState_MetaKnightQuickSpinInterrupt` (0x801c3f40) | Skip his enter |
| Charge, grounded and wheelie | 0x801ef424, 0x801ef350, 0x801fa1d4, 0x801fa29c | `MachinePhys_Charge` (0x801ef364), `Machine_Star_PushChargeUpdate` (0x801ef338), `Machine_Wheel_PushChargeUpdate` (0x801fa1c8), `fn_VehicleStatTableFuncCallbacks_Wheel_RunPush_3` (0x801fa1e8) | Skip `bl Machine_IncrementCharge` |
| Charge, glide | 0x801efa6c | `fn_VehicleStatTableFuncCallbacks_Star_Fly_3_HandleFlightPhysics` (0x801ef9a0) | Skip `bl Machine_AddCharge` |
| Charge, rail and wheelie push | 0x801eb968, 0x801f5f30 | `Machine_Star_RailPushAddCharge` (0x801eb95c), `Machine_Wheel_PushAddCharge` (0x801f5f24) | Skip `bl Machine_AddChargeEx` |
| Top Ride charge | 0x802e01b4 | `TopRide_ChargeUpdate` (0x802df900) | Conditional hook on the inline store |
| Top Ride quick spin | 0x802d5f90 | `TopRide_KirbyPhysUpdate` (0x802d5ec0) | `TopRide_KirbyHistoryQuery` answers "no flick" |

Notes on the shape:

- **Inhale is gated at the call, not the function.** The hypernova mod calls `Rider_StartInhale`
  directly, and gating the function would break it.
- **Three functions grow the charge** while A is held - `Machine_IncrementCharge`,
  `Machine_AddCharge` and `Machine_AddChargeEx` - across the grounded, wheelie, glide and rail
  phases, so all seven call sites are covered. The explicit-rate wrappers take the rate as a
  named parameter so f1 survives the human check.
- **Top Ride's charge is an inline store** (`stfs f0, 52(r3)` into
  `TopRideChargeComponent.charge_value`), so the hook always takes the alternate exit and does
  the store itself for CPUs and unlocked humans. Only the accumulation stops; the charging
  flags, the decay and the clamp still run.
- **Top Ride's quick spin** is the stick-flick spin attack: `TopRide_KirbyHistoryQuery`
  (0x80312000) reads the stick history and returns a spin direction, and a 0 skips the spin
  block. It is not the `KirbySpin` hazard state, which stays ungated so a locked move does not
  make Kirby immune to spin hazards. The query gets `&kirby->history`, from which the wrapper
  recovers the kirby.
- **Tornado** spins through its own path, gated only as a copy ability.

### Charge and EnergyLink

The charge gate stops the player charging but not the meter: EnergyLink's Auto-Charge writes
`charge_value` directly, and the boost on release is not gated, so a locked human can still
spend received energy on a boost. A locked human holding A generates no charge, so mints no
charge energy either. In Top Ride, Auto-Charge needs the charging and charge-ready flags, both
of which the hook leaves running.

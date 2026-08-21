# TrapLink

TrapLink has two sides. **Send** detects negative events happening to a human player in-game and writes a `TrapLinkKind` value (>0) into `ap_data->traplink_send`; the AP client reads and clears the field, looks up the matching `trap_name` string, and forwards a TrapLink Bounce. **Receive** happens when the client sets `ap_data->traplink_receive = 1`: `TrapLink_PerFrame` dispatches a mode-appropriate trap effect on every human player. The GObj is installed by `TrapLink_On3DLoadEnd` (Air Ride / City Trial) or `TrapLink_OnTopRideLoadEnd` (Top Ride), and only when `ap_menu_settings.traplink_enabled` is set. Source: `mods/archipelago/src/traplink.c`.

Outgoing kinds (`traplink.h`):

| Value | Constant | Trigger |
|-------|----------|---------|
| 0 | `TRAPLINK_KIND_NONE` | No pending send |
| 1 | `TRAPLINK_KIND_BAD_PATCH` | City Trial bad/fake patch pickup |
| 2 | `TRAPLINK_KIND_SLEEP` | Sleep copy ability granted (CT/AR) |
| 3 | `TRAPLINK_KIND_SPEED_DOWN` | Top Ride `TRITEM_SPEED_DOWN` pickup |

The client maps these to `"Bad Patch"`, `"Sleep"` and `"Speed Down"` when building the Bounce.

**Incoming `trap_name` is ignored.** When a Bounce arrives from another world the client just increments `pending_trap_receives`, and the receive path picks a random local trap based on the current scene major. KAR's trap kinds have no clean 1:1 mapping to other worlds' trap names, so "any TrapLink hit applies *some* local trap" is both simpler and better balanced than translating semantics. Senders still include `trap_name` for the benefit of other worlds that filter on it.

**No game-side debounce.** Triggers in quick succession (several bad items from one box burst) all overwrite the same u32; since it is a value and not a queue, a burst naturally collapses to whichever kind was written last before the client polls. The client polls at least every 0.1s (typically 1.0s when idle), forwards one Bounce per non-zero read, and clears the field, so a logical burst is long over before the next poll and only one Bounce goes out.

## Protocol Conformance

The wire contract is a Bounce tagged `"TrapLink"` with `data = {time, source, trap_name}`. KAR conforms, with two deliberate divergences from the SA2/SMW reference implementations:

| Reference behavior | KAR | Rationale |
|---|---|---|
| Map incoming `trap_name` to a known local trap; drop if unknown | Apply a random local trap regardless of `trap_name` | No clean 1:1 mapping exists, and this avoids no-op receives |
| Discard a trap that is not immediately activatable ("close-in-time" guarantee) | Retry next frame until activatable, or until scene exit drops the GObj | The only non-activatable conditions are CT Free Run (dropped immediately anyway) and intro state, which resolves in ~2 seconds |

The kind-to-name strings follow the shared TrapLink name-list convention, so other worlds can tell which names KAR sends and that it accepts all incoming ones.

## Receive-to-Send Recursion Guard

A received trap must not bounce back out as an outgoing one. The City Trial apply path re-fires the bad-patch send hook: `ApplyCityTrialTrap` spawns collectible bad patches that `Machine_OnTouchItem` then processes, re-detecting the trap.

`TrapLink_PerFrame` sets `recv_suppress_frames = TRAPLINK_RECV_GUARD_FRAMES` (120, ~2s at 60Hz) whenever a receive applies. The counter ticks down at the top of `TrapLink_PerFrame`, *before* the receive check so idle frames advance it too, and `TrapLink_Send` no-ops while it is positive. The window is generous to cover spawn -> collision -> hook latency. Both load-end entry points reset it to 0 so it cannot carry stale state across a scene transition.

The other two modes do not actually re-fire their hooks - TR applies its debuff through `TopRide_KirbyApplyItem` with no collectible item spawned, so the absorber-collision hook never sees it, and AR's `ApplyAirRideTrap` calls `Rider_GiveAbility` directly, bypassing the `GateAbilities_CheckAndGiveAbility` replacement that owns the sleep send hook. The guard is armed uniformly after any receive regardless.

## Send Triggers

### Bad patch / stat-cap items (City Trial)

Fires when the player picks up an item the game classifies as bad or fake: SPEEDMIN, CHARGENONE, all `*DOWN` stat patches, all `*FAKE` patches.

`CODEPATCH_HOOKCREATE` at `0x801DB504` in `Machine_OnTouchItem` (0x801db34c), on the branch taken when `CityItem_IsGoodPatch` (0x802540a8) returns 0. r20 = `MachineData*` there. `TrapLink_OnBadPatch` looks the player up with `Machine_GetRiderPly`, drops CPUs via `Ply_CheckIfCPU`, and calls `TrapLink_Send(TRAPLINK_KIND_BAD_PATCH)`.

### Sleep copy ability (City Trial / Air Ride)

Fires when the player receives COPYSLEEP from a sleep-granting enemy or a copy ability item. This one is not a code patch but an inline check inside `GateAbilities_CheckAndGiveAbility` in `gate_abilities.c`, the REPLACEFUNC that stands in for `Rider_CheckAndGiveAbility` (0x80192650). The replacement returns 0 early if the ability is locked, otherwise calls `Rider_GiveAbility` and captures the result; a successful grant of `COPYKIND_SLEEP` to a human sends. Checking the result matters: `Rider_GiveAbility` returns 0 when the rider is in an unable state, and without the check that produces phantom traps.

### Bad Top Ride item pickup

Fires when a human Top Ride Kirby collects a bad TR item - currently only `TRITEM_SPEED_DOWN`.

`CODEPATCH_HOOKCREATE` at `0x8034C7DC`, inside `TopRideItem_Update` (0x8034c130), at the point where the per-absorber collision test has just succeeded. The clobbered instruction is `lbz r4, 104(r31)`, reading the item kind out of the item list node (`node+0x68`, equivalent to `item+0x60` because list nodes prefix the embedded item by 8 bytes). The prologue marshals `item_kind` into r3 and `absorber_pos` into r4 from non-volatile r26, where vanilla stashed the absorber's position earlier from its vtable[2] call.

`TrapLink_OnTopRideItemPickup` checks the kind against `tr_trap_items[]`, then finds the picker by comparing each `kirby->charge.position` against the absorber position and taking the nearest, and sends only if that kirby is human. The absorber's position coincides with the Kirby's in-world position while both are in the same coordinate frame. The comparison must use `kirby->charge.position` (offset 0x88, inside the inline charge component); `kirby->position` at 0x4C is the spawn default and is never updated during play.

**Why the TR pool is only SPEED_DOWN.** In the `TopRide_KirbyApplyItem` dispatcher (0x802d8cb4), `TRITEM_SPEED_DOWN` is the only kind that installs a self-debuff state on the picker (`KirbySpeedDown`, state ID 18, vtable `0x804DBAC8`). Every other TR item either arms the picker with an attack (Hammer, Bomb, Cracker, Spear, Mike, Missile and friends install attack-handler vtables on top of `KirbyNormal`, leaving full control), buffs the picker (Grow, SpeedUp, Invincible, Lantern), or targets *others* (Who?Paint and Chickie call state wrappers on opposing kirbys). `TRITEM_PARTY_BALL` is not usable through this path either: its dispatcher (0x802d9188) only resets to `KirbyNormal` and swaps in the `KirbyUshiroyurerun` vtable, while the velocity-flip half of the vanilla effect comes from the absorber-pickup setup that `TopRide_KirbyApplyItem` skips - applied this way it produces the smoke-trail visual and no slowdown. Expanding the pool would need a different mechanism, such as forcing a state install, rather than reusing the apply path.

## Receive

`TrapLink_PerFrame` runs every frame on the GObj installed at scene load. It returns immediately unless `traplink_receive` is set, and also defers while `Gm_GetIntroState() != GMINTRO_END` - which only bites in 3D, since Top Ride has no intro sequence and `Gm_GetIntroState` reads `GMINTRO_END` there. Once a receive is pending it dispatches on `Scene_GetCurrentMajor()`:

| Mode | Effect |
|------|--------|
| City Trial - open city | `ApplyCityTrialTrap`: builds a candidate list from `trap_items[]` minus any whose event is locked (`IsTrapItemLocked`), Fisher-Yates shuffles it, then tries each candidate through `APItems_HandleItem` **in one tick** until one applies. Trying every eligible trap in a single frame avoids the slow path where one random pick keeps failing and the receive flag lingers for frames. |
| City Trial - Free Run (`Gm_GetCityMode() == CITYMODE_FREERUN`) | Dropped, but treated as handled so the flag clears. Item data tables are not loaded, and CT trap effects would crash. |
| City Trial - stadium (`CityTrial_IsInStadium()`) | Falls back to the Air Ride sleep trap. Stadium riders are always mounted, so the sleep trap's on-machine requirement always holds. |
| Air Ride | `ApplyAirRideTrap`: calls `Rider_GiveAbility(rd, COPYKIND_SLEEP)` directly on every human Kirby rider that is on a machine. Off-vehicle riders crash in the sleep anim's MObj callback, which calls `Rider_CopyInputToMachine` and derefs a null machine GObj. Using the raw rider API rather than `Rider_CheckAndGiveAbility` keeps the gate and the sleep send hook from re-triggering. |
| Top Ride | `ApplyTopRideTrap`: picks a random kind from `tr_trap_items[]` and calls `GateTopRideItems_GiveItem`, which - gated on `round_state == 2` - calls `TopRide_KirbyApplyItem` directly on each human Kirby, installing the self-debuff state with no collectible item spawned. |

Most City Trial trap items (`AP_ITKIND_*`, `AP_EVENT_*`) require `Gm_IsInCity`, which is why AR and TR need mode-specific effects instead of the shared `trap_items` list. The CT pool also carries two synthetic traps: `AP_ITEM_1_HP_TRAP`, which damages each human machine down to 1 HP, and `AP_ITEM_DROP_PATCHES_TRAP`, whose handler in `ap_item_handler.c` gates on `Gm_IsInCity` and calls `Patch_DropTrap()` in `patch_item.c` to eject each human rider's equipped stat patches behind the machine.

When a receive applies, `TrapLink_PerFrame` enqueues a "Trap received!" textbox, clears `traplink_receive`, and arms the recursion guard. If every eligible handler returned 0 this frame - no human rider or Kirby present, or every event-active CT effect refused - the flag stays set and the GObj retries next frame.

## Key Addresses

| Function | Address | Purpose |
|----------|---------|---------|
| `Machine_OnTouchItem` | 0x801DB34C | Master item effect handler (0x728 bytes) |
| Bad patch branch | 0x801DB504 | Entry to bad patch processing (hook point) |
| `CityItem_IsGoodPatch` | 0x802540A8 | Returns 1 for good patches, 0 for bad/fake |
| `Rider_CheckAndGiveAbility` | 0x80192650 | Copy ability grant (replaced by `gate_abilities.c`) |
| `Rider_GiveAbility` | 0x801A81A4 | Raw grant; returns 0 if the rider is in an unable state |
| `TopRideItem_Update` | 0x8034C130 | TR item per-frame update; iterates absorber collisions |
| TR item pickup | 0x8034C7DC | Pickup confirmed inside `TopRideItem_Update` (hook point) |
| `TopRide_KirbyApplyItem` | 0x802D8CB4 | TR item-apply dispatcher; installs a state directly on a Kirby |

## Implementation Notes

- `TrapLink_Send(kind)` is the single entry point for every send trigger. It no-ops unless `traplink_enabled`, on `TRAPLINK_KIND_NONE`, and while the receive guard window is active, then writes the kind into `ap_data->traplink_send`. It has no mode gate - hooks only fire in their applicable gameplay contexts.
- Human-vs-CPU filtering stays at each call site, since the two engines discriminate differently: the 3D hooks use `Ply_CheckIfCPU`, the Top Ride hook uses `TopRide_GetPlayerKind(kirby->player_slot) == TR_PKIND_HMN`.
- Because the toggle is checked inside `TrapLink_Send`, individual hooks do not re-check it. The toggle also gates GObj installation, so the receive proc is not even created while TrapLink is off.
- `TrapLink_OnBoot()`, called from `main.c`'s `OnBoot`, applies both code patches.

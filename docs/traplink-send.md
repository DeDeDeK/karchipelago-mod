# Traplink

## Overview

TrapLink has two sides:

- **Send**: Detects when negative events happen to a human player in-game and writes a `TrapLinkKind` enum value (>0) into `ap_data->traplink_send`. The AP client reads and clears this field, looks up the corresponding `trap_name` string, and forwards a TrapLink Bounce to the AP server.
- **Receive**: When the AP client sets `ap_data->traplink_receive = 1`, `TrapLink_PerFrame` dispatches a mode-appropriate trap effect on every human player. The GOBJ is installed by `TrapLink_On3DLoadEnd` (Air Ride / City Trial) and `TrapLink_OnTopRideLoadEnd` (Top Ride).

**Outgoing kind enum (see `traplink.h`):**

| Value | Constant                  | Trigger                                              |
|-------|---------------------------|------------------------------------------------------|
| 0     | `TRAPLINK_KIND_NONE`      | No pending send                                      |
| 1     | `TRAPLINK_KIND_BAD_PATCH` | City Trial bad/fake patch pickup                     |
| 2     | `TRAPLINK_KIND_SLEEP`     | Sleep copy ability granted (CT/AR)                   |
| 3     | `TRAPLINK_KIND_SPEED_DOWN`| Top Ride `TRITEM_SPEED_DOWN` pickup                  |

The client maps these to `"Bad Patch"`, `"Sleep"`, and `"Speed Down"` respectively when building the outgoing Bounce.

**Incoming `trap_name` is ignored.** When a TrapLink Bounce arrives from another world, the client increments `pending_trap_receives` regardless of the source world's `trap_name`. The receive path then picks a random local trap from KAR's own pool based on the current scene major (see "Receive" below). This is intentional — KAR's trap kinds don't have a clean 1:1 mapping to other worlds' trap names, and the gameplay-balanced approach is "any TrapLink hit applies *some* local trap" rather than trying to translate semantics across worlds. Senders include `trap_name` for the benefit of *other* worlds (e.g., SMW filters traps by name); receivers in KAR don't need it.

**Receive→send recursion guard.** The protocol explicitly requires that received traps not bounce back as outgoing traps. The CT apply path re-fires the bad-patch send hook: `ApplyCityTrialTrap` spawns collectible bad patches that `Machine_OnTouchItem` re-processes, which would re-detect the trap and bounce it back. The guard prevents that. `TrapLink_PerFrame` sets `recv_suppress_frames = 120` (≈2 seconds at 60Hz) whenever a receive applies; the counter ticks down each frame (in `TrapLink_PerFrame` itself, before the receive check) and `TrapLink_Send` no-ops while it's positive. The window is generous to cover spawn→collision→hook latency. It is reset to 0 on every `TrapLink_On3DLoadEnd` / `TrapLink_OnTopRideLoadEnd` so the counter can't carry stale state across scene transitions. The other two modes don't actually re-fire their hooks — TR applies its debuff directly via `TopRide_KirbyApplyItem` (no collectible item is spawned, so the absorber-collision pickup hook at `0x8034C7DC` never sees it), and AR's `ApplyAirRideTrap` calls `Rider_GiveAbility` directly, bypassing the `GateAbilities_CheckAndGiveAbility` replacement that owns the sleep send hook — but the guard is armed uniformly after any receive regardless of mode.

**No game-side debounce:** Multiple triggers in quick succession (e.g., picking up several bad items from one box burst) all overwrite the same kind value. Since it's a single u32 (not a queue), rapid-fire triggers are naturally collapsed — the last kind written before the client polls wins. The client polls every ≥0.1s (typically 1.0s when idle), forwards a single Bounce per non-zero read, and clears the field. No client-side debounce is required: by the time the next poll lands, a logical burst is long over and only one Bounce ever goes out.

## Protocol Conformance

The TrapLink protocol (Bounce tag `"TrapLink"`, `data = {time, source, trap_name}`) is described in the central TrapLink design doc. KAR conforms to the wire contract, with two deliberate behavioral divergences from the SA2/SMW reference implementations:

| Spec behavior | KAR | Rationale |
|---|---|---|
| Map incoming `trap_name` to a known local trap; drop if unknown | Apply a random local trap regardless of `trap_name` | KAR's trap pool doesn't have a clean 1:1 mapping to other worlds' trap names. "Any TrapLink hit applies some local trap" is gameplay-balanced and avoids no-op receives. |
| Discard Priority Trap if not immediately activatable ("close-in-time" guarantee) | Retry next frame until activatable (or scene exit drops the GObj) | The only non-activatable conditions in KAR are CT Free Run (we drop the receive immediately) and intro state (resolves in ~2 seconds). Retrying is benign in practice and avoids silently dropping traps mid-intro. |

The kind-to-name strings (`"Bad Patch"`, `"Sleep"`, `"Speed Down"`) follow the centralized TrapLink name spreadsheet convention; KAR should be added there so other worlds know which names we send and that we accept all incoming.

## Implemented Send Triggers

### 1. Bad Patch / Stat-Cap Items (City Trial)

**What:** Player picks up an item the game classifies as bad or fake (SPEEDMIN, CHARGENONE, all `*DOWN` stat patches, all `*FAKE` patches).

**Hook:** `HOOKCREATE` at `0x801DB504` in `Machine_OnTouchItem`, on the branch taken when `CityItem_IsGoodPatch` (0x802540A8) returns 0. At this point r20 = MachineData*.

**File:** `traplink.c` — `TrapLink_OnBadPatch()`

**Flow:**
1. `Machine_OnTouchItem` processes an item pickup
2. Calls `CityItem_IsGoodPatch` — returns 0 for items whose item-attr classifier is non-good
3. Branches to 0x801DB504 (bad/fake patch path)
4. Hook fires: looks up the player via `Machine_GetRiderPly`, drops if `Ply_CheckIfCPU`
5. Calls `TrapLink_Send(TRAPLINK_KIND_BAD_PATCH)` which gates on `traplink_enabled`

### 2. Sleep Copy Ability (City Trial)

**What:** Player receives COPYSLEEP from inhaling/touching a Noddy or other sleep-granting enemy, or from a copy ability item.

**Hook:** Inline check inside `GateAbilities_CheckAndGiveAbility` (REPLACEFUNC for `Rider_CheckAndGiveAbility` at 0x80192650).

**File:** `gate_abilities.c`

**Flow:**
1. `Rider_CheckAndGiveAbility` is called (enemy contact or item pickup)
2. Replacement returns 0 early if the ability is locked
3. Otherwise calls `Rider_GiveAbility` and captures the result
4. If the grant succeeded AND `kind == COPYKIND_SLEEP` AND the player is human, calls `TrapLink_Send(TRAPLINK_KIND_SLEEP)`. The success check avoids phantom traps when the rider is in an unable state and `Rider_GiveAbility` returns 0.

### 3. Bad Top Ride Item Pickup

**What:** A human Top Ride Kirby collects a bad TR item (currently only `TRITEM_SPEED_DOWN`).

**Why the pool is locked at SPEED_DOWN:** Walking the entire `TopRide_KirbyApplyItem` dispatcher (`0x802D8CB4`), only `TRITEM_SPEED_DOWN` (kind 3) installs a self-debuff state on the picker (`KirbySpeedDown`, state ID 18, vtable `0x804DBAC8`). Every other TR item either (a) arms the picker with an attack — Hammer/Bomb/Cracker/Spear/Mike/Missile/etc. install attack-handler vtables on top of `KirbyNormal`, picker keeps full control; (b) buffs the picker — Grow/SpeedUp/Invincible/Lantern; or (c) installs a *targets-others* effect — Who?Paint and Chickie call state wrappers on opposing kirbys, not on the user. `TRITEM_PARTY_BALL` was tried and ruled out: its dispatcher (`0x802D9188`) only resets to `KirbyNormal` and swaps in the `KirbyUshiroyurerun` vtable; the velocity-flip portion of the vanilla effect comes from the absorber-pickup setup that `TopRide_KirbyApplyItem` skips, so applied via this path it produces only the smoke-trail visual without slowing the picker. Expanding the pool would require a different mechanism (e.g. forcing a state install) rather than reusing the apply path.

**Hook:** `CODEPATCH_HOOKCREATE` at `0x8034C7DC`, inside `TopRideItem_Update` (0x8034c130), at the moment the per-absorber collision test has just succeeded. The clobbered instruction is `lbz r4, 104(r31)` which reads the item kind out of the item list node (`node+0x68`, equivalent to `item+0x60` because list nodes prefix the embedded item by 8 bytes).

**File:** `traplink.c` — `TrapLink_OnTopRideItemPickup()`

**Flow:**
1. Item orchestrator iterates active TR items.
2. For each item, iterates the `ObjCollect<ItemMgr_Absorber>` singleton (one absorber per Kirby).
3. Collision test: distance from absorber position to item position is under threshold.
4. Pickup confirmed — about to execute `lbz r4, 104(r31)` to stash the item kind.
5. Hook fires: prologue marshals `item_kind` (r3) and `absorber_pos` (r4, taken from non-volatile r26 where vanilla stashed it earlier from the absorber's vtable[2] call).
6. Handler checks the kind against `tr_trap_items[]`, finds the nearest `TopRideKirby` by comparing `kirby->charge.position` to the absorber position, and only sends if that kirby is human (`TopRide_GetPlayerKind(kirby->player_slot) == TR_PKIND_HMN`).
7. Framework re-executes the clobbered `lbz` and returns to the original flow.

**Notes on the nearest-Kirby match:** the absorber's position is fetched via a vtable[2] call on the sub-object at `absorber+0x18`, which coincides with the Kirby's in-world position while both are in the same coordinate frame. The comparison uses `kirby->charge.position` (the per-frame in-world position at offset 0x88, inside the inline charge component); `kirby->position` at 0x4C is the spawn/default and is not updated during play.

## Receive

`TrapLink_PerFrame` runs every frame on the GObj installed at scene load. It returns immediately unless `ap_data->traplink_receive` is set, and (for 3D modes) also defers while `Gm_GetIntroState() != GMINTRO_END` — Top Ride has no intro sequence so `Gm_GetIntroState` defaults to `GMINTRO_END` there. Once a receive is pending and the intro is over, it dispatches on `Scene_GetCurrentMajor()`:

| Mode | Effect |
|------|--------|
| City Trial — normal | `ApplyCityTrialTrap`: builds a candidate list from `trap_items[]` minus any whose corresponding event is locked (`IsTrapItemLocked`), Fisher–Yates shuffles it, then tries each candidate via `APItems_HandleItem` **in one tick** until one applies. (Trying all eligible traps in a single frame avoids the slow path where a single random pick keeps failing and the receive flag lingers for many frames.) |
| City Trial — Free Run | Dropped — treated as handled so the flag clears (item data tables aren't loaded; CT trap effects would crash). |
| City Trial — stadium | Falls back to the Air Ride sleep trap (stadiums still have rider GOBJs). |
| Air Ride | `ApplyAirRideTrap`: calls `Rider_GiveAbility(rd, COPYKIND_SLEEP)` directly on every human Kirby rider that is on a machine (off-vehicle riders crash in the sleep anim's MObj callback). Uses the raw rider API, not `Rider_CheckAndGiveAbility`, so the gate + sleep-send hook in `gate_abilities.c` does not re-trigger. |
| Top Ride | `ApplyTopRideTrap`: picks a random kind from `tr_trap_items[]` (currently only `TRITEM_SPEED_DOWN`) and calls `GateTopRideItems_GiveItem`, which — gated on `kirby_mgr->round_state == 2` — calls `TopRide_KirbyApplyItem(k, kind)` directly on each human Kirby. That dispatcher installs the self-debuff state on the Kirby itself; no collectible item is spawned. |

Most City Trial trap items (`AP_ITKIND_*`, `AP_EVENT_*`) require `Gm_IsInCity`, which is why Air Ride and Top Ride use mode-specific effects instead of the shared `trap_items` list. The CT pool also includes two synthetic traps: `AP_ITEM_1_HP_TRAP` and `AP_ITEM_DROP_PATCHES_TRAP` — the latter dispatches (via `APItems_HandleItem`) to `Patch_DropTrap()` (`patch_item.c`), which ejects each human rider's equipped stat patches behind the machine; it is itself CT-open-city-only (`Gm_IsInCity`). See `patch-drop-system.md`.

When a receive applies, `TrapLink_PerFrame` enqueues a "Trap received!" textbox, clears `traplink_receive`, and arms the recursion guard. If every eligible handler returned 0 this frame (e.g. no human rider/Kirby is present, or every event-active CT effect refused), `traplink_receive` stays set and the GObj retries next frame.

## Key Functions

| Function | File | Purpose |
|----------|------|---------|
| `TrapLink_Send(kind)` | `traplink.c` | Shared send helper. No-ops unless `ap_menu_settings.traplink_enabled`, and also no-ops on `TRAPLINK_KIND_NONE` and while the receive guard window is active. Writes the `TrapLinkKind` value into `ap_data->traplink_send`. |
| `TrapLink_OnBadPatch()` | `traplink.c` | Hook callback for bad patch detection. Checks human player, calls `TrapLink_Send(TRAPLINK_KIND_BAD_PATCH)`. |
| `TrapLink_OnTopRideItemPickup()` | `traplink.c` | Hook callback for Top Ride item pickup. Checks kind against `tr_trap_items[]`, matches nearest human Kirby, calls `TrapLink_Send(TRAPLINK_KIND_SPEED_DOWN)`. |
| `TrapLink_OnBoot()` | `traplink.c` | Applies the 0x801DB504 and 0x8034C7DC hooks. Called from `main.c` `OnBoot()`. |

## Key Addresses

| Function | Address | Purpose |
|----------|---------|---------|
| `Machine_OnTouchItem` | 0x801DB34C | Master item effect handler (0x728 bytes) |
| `CityItem_IsGoodPatch` | 0x802540A8 | Returns 1 for good patches, 0 for bad/fake |
| Bad patch branch | 0x801DB504 | Entry to bad patch processing (our hook point) |
| `Rider_CheckAndGiveAbility` | 0x80192650 | Copy ability grant (replaced by gate_abilities) |
| `TopRideItem_Update` | 0x8034C130 | Top Ride item per-frame update; iterates absorber collisions |
| Top Ride item pickup | 0x8034C7DC | Pickup confirmed inside `TopRideItem_Update` (our hook point) |
| `TopRide_KirbyApplyItem` | 0x802D8CB4 | TR item-apply dispatcher; installs the self-debuff state directly on a Kirby. Called by `GateTopRideItems_GiveItem` on the TR receive path (no collectible item is spawned). |

## Implementation Notes

- `TrapLink_Send(kind)` is the single entry point for all send triggers. It has no mode gate — hooks only fire in their applicable gameplay contexts. Each call site passes its own `TrapLinkKind` so the client can label the outgoing Bounce.
- All triggers check `Ply_CheckIfCPU` to avoid sending traps for CPU players.
- The `ap_menu_settings.traplink_enabled` menu toggle is checked in `TrapLink_Send(kind)`, so individual hooks don't need to check it. The toggle also gates GObj installation: `main.c`'s `On3DLoadEnd`/`OnTopRideLoadEnd` only call `TrapLink_On3DLoadEnd`/`TrapLink_OnTopRideLoadEnd` when it is set, so the receive proc isn't even created while TrapLink is off.
- `IsTrapItemLocked` filters the candidate trap pool in `TrapLink_PerFrame` before random selection. If a trap item's corresponding gate (currently only events) is locked in the current session, it is excluded from the candidate list so the receive path never tries to apply an inaccessible effect.

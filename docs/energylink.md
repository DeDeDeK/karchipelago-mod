# EnergyLink

EnergyLink is the Archipelago link mechanic that lets players generate a shared "energy" currency from in-game activity and spend it on items via an in-game shop. The mod owns local accumulation; the AP client owns the actual pool total.

Source: `mods/archipelago/src/energylink.c`, `mods/archipelago/src/energylink_spend.c`. Toggle: `ap_menu_settings.energylink_enabled`, under Settings -> Energy Link.

## Protocol

Two shared fields in `APData`:

| Field | Direction | Semantics |
|-------|-----------|-----------|
| `energy_balance` (s64) | Client -> Game | Current AP pool total in raw MJ. Read for purchase validation and Auto-Charge; the game also locally subtracts on spend for immediate UI feedback, and the client's next write replaces it. s64 so multiworld pools exceeding u64 joules still fit at MJ scale. |
| `energy_sent_total` (s64) | Game -> Client | **Cumulative net** MJ emitted to the pool this session: deposits add, spends and Auto-Charge subtract. **Single-writer** - the game only ever adds/subtracts, the client only reads-and-diffs. Resets to 0 on mod boot; persists across scene loads. |

The cumulative-counter model is what makes the whole thing lock-free. The game writes the running total as often as it likes; the client reads it once per ~1s poll, computes `delta = current - last_seen`, forwards that delta to the server, and advances `last_seen`. Any number of game-side writes between two polls collapse into one net delta, so there is **no flush, no slot handshake, and no per-frame polling**. Sub-MJ generation accumulates in a float carry (`energy_frac_accumulator`); `EnergyLink_Emit` commits only whole MJ and rolls the remainder forward. The cast in `EnergyLink_Emit` goes through s32 on purpose: PPC has hardware float->s32 (`fctiwz`) but not float->s64, and the libgcc soft routines are not linked.

The counter is also chosen over a consume-once mailbox because a 64-bit field cannot be read atomically on PPC32. A torn read skews one poll's delta, but the client sets `last_seen` to whatever it read, so the next poll's diff compensates exactly.

The mod stores raw MJ; the AP server stores integer Joules (`ENERGY_LINK_EXCHANGE_RATE = 1_000_000`), and the client scales between them.

Energy arriving from other players or from an energylink trade lands in `energy_balance` via the client's `set_notify` push. **No in-game notification is shown** when the balance rises - receiving is silent by design, and only spends surface a TextBox.

## Generation

Three sources, all positive deltas only, tracked per player in `EnergyLink_PerFrame`:

1. **Objects destroyed** - `stc_playerdata[ply].stat_record.objects_destroyed_num`. Effectively City-Trial-only; AR never increments this.
2. **Patches collected** - `rd->stats.values[i]` delta across all 9 `PatchKind` slots. Each +1 stat = 1 MJ.
3. **Charge gained** - `md->charge_value` delta scaled by `CHARGE_ENERGY_SCALE` (5.0), so a full 0->1 charge is worth 5 MJ.

Per-player snapshots (`prev_obj_destroyed`, `prev_stats`, `prev_charge_value`) keep multi-human City Trial from cross-contaminating.

`needs_baseline[ply]` defers the first frame's snapshot until `Gm_GetIntroState() == GMINTRO_END`, so round-start permanent-patch application is captured *as the baseline* rather than as a +N energy gain. This depends on ordering: `main.c`'s `On3DLoadEnd` calls `PermanentPatch_On3DLoadEnd` before `EnergyLink_On3DLoadEnd`, and the standalone perm-patch GObj runs before the per-rider EnergyLink proc on the intro-end frame.

## Auto-Charge

Opt-in toggle under Settings -> Energy Link -> Auto-Charge. Each frame it tops up the machine's charge meter by spending energy, but only by a **capped per-frame amount** so the meter rises steadily and *assists* the player's own charging (holding A, gliding) instead of snapping to full whenever energy is available.

`AutoCharge_Gain` returns `min(1.0 - charge_value, cap)` where `cap` comes from `AUTOCHARGE_RATES[]` indexed by the Auto-Charge Rate setting:

| Setting | Per-frame gain | Frames to fill 0->1 | Time @60fps |
|---------|----------------|---------------------|-------------|
| Slow | `0.00555` | ~180 | ~3.0s |
| Medium (default) | `0.01111` | ~90 | ~1.5s |
| Fast | `0.02222` | ~45 | ~0.75s |

The total cost to fill the meter is unchanged (`1.0 * CHARGE_ENERGY_SCALE` = 5 MJ); the cap only spreads that spend across frames. Because the per-frame cost (`gain * SCALE`, at most ~0.11) stays well under one MJ, any positive integer balance can pay for a step - so the affordability check collapses to `balance > 0`, with no s64->float partial-affordability math. The small deltas also shrink the torn-read window on the 64-bit fields.

After injecting, the proc re-snaps `prev_charge_value[ply]`. That single line is the only thing preventing a feedback loop: without it the injected charge reads back as a positive charge delta next frame and mints the energy it just cost.

`EnergyLink_Withdraw` emits the negative delta into the counter **and** decrements `ap_data->energy_balance` immediately (whole MJ, with a fractional carry for sub-1-MJ spends). The immediate decrement is what makes the affordability gate self-limiting. The gate reads the local balance, which only refreshes on the client's ~1s `set_notify` push; if withdrawals waited for that push, Auto-Charge would re-read the same stale positive balance every frame and approve spends for up to a second, committing far more than the pool holds. The client's push *replaces* rather than subtracts, so the local decrement is never double-counted.

### Passive vs active fill

In Air Ride and City Trial the 3D engine does not bleed off an idle charge, so the per-frame injection accumulates on its own even when A is not held, and energy becomes a topped-up boost the player can release at will. Top Ride behaves the opposite way and is covered below.

### Meta Knight exclusion

Auto-Charge is **skipped entirely** for `md->kind == VCKIND_WINGMETAKNIGHT`. His Wing machine has no chargeable boost meter - `charge_value` acts as a raw speed term, so injecting it every frame pins him at a constant max-speed buff instead of assisting a charge cycle. The guard is on the injection block only; his charge-gain *generation* still mints energy like any other character. King Dedede has a normal meter and is **not** excluded, and Top Ride has no machine kinds so the exclusion does not apply there.

## Spending

`energylink_spend.c` holds a static tree of `SpendEntry { APItemId item_id; s64 cost }` (integer raw MJ), exported as `MenuDesc energylink_spend_menu` and plugged into the Settings menu. Each leaf is built by the `BUY(item, cost, label)` macro, which wires `Buy` as the `OptionDesc.on_action` and stores the `SpendEntry` in `user_data`.

| Category | Items | Cost (raw MJ) |
|----------|-------|---------------|
| Stat Patches | 10 | 250 each (All Up 2000) |
| Permanent Patches | 10 | 3500 each (Perm All Up 25000) |
| Copy Abilities | 11 | 600 each |
| Food | 12 | 200-1000 |
| Special Items | 5 | 1000-2500 |
| Legendary Pieces | 15 | 5000 each Hydra/Dragoon part, 2500 each AP Star sphere (full machine 17500) |
| City Trial Items | 7 | 800-3200 |
| City Trial Events | 16 | 2500 each |
| Top Ride Items | 22 | 400-800 |
| Checkbox Fillers | 3 | 50000 each |
| Cosmetic | 2 | 500 each (Big / Small Kirby) |

Prices are deliberately high. EnergyLink mints fast in City Trial (5 MJ per full machine charge, 1 MJ per destroyed object, 1 MJ per +1 stat patch), so a single strong round can generate roughly 1-2k MJ. The shop is scaled to stay a meaningful sink rather than a buy-everything button. Checkbox Fillers are an extreme premium because they complete a checklist location of the player's choice and advance the "fill N blocks" goal.

**No purchasable progression.** Every code sold maps to a filler/useful/trap item - the Copy Abilities, City Trial Events and Top Ride Items use the `*_GIVE` codes, not the progression `*_UNLOCK` codes. There is no Upgrades category: Patch Cap Increase is AP progression (it gates logic and can itself be the City Trial goal), so energy must never buy it, and Spawn Rate Up is excluded alongside it so no persistent run-altering upgrade is energy-buyable.

`Buy` rejects in this order, each with its own TextBox: Energy Link toggled off; a City Trial Event give item whose `EventKind` bit is unset in `ap_save->event_unlocked_mask` (the same mapping the give path in `ap_item_handler.c` uses, so energy cannot fire an event the seed has not granted); `balance < cost`; and a full unprocessed queue (`ap_save->unprocessed_count >= MAX_RECEIVED_ITEMS`, 512).

On success it pushes `item_id` onto `ap_save->unprocessed_items[]` so `APItems_PerFrame` applies it on a later frame under the usual scene/intro gate - the same path as items received from AP - then subtracts `cost` from **both** `energy_sent_total` and `energy_balance`. Both are inline `s64 -= s64` on PPC32, no float round-trip and no `__floatdisf`. The integer cost lands on the counter exactly and immediately, which is required: no gameplay frame runs while the menu is open to drive a per-frame flush, so the withdrawal has to already be on the counter for the next poll to see it. Auto-Charge's fractional spends still go through `EnergyLink_Withdraw`; purchases take the direct integer path so an exact cost never mixes into the fractional generation carry.

On its next poll the client sees `energy_sent_total` decrease, computes the negative delta and forwards it as a tagged `Set` with `want_reply: true`, matching the `SetReply` against `pending_withdrawals[tag]` to detect under-subtraction. The mod's local balance is corrected by the next `set_notify` push regardless of whether the server clamped.

### Under-subtraction

The client logs `[EnergyLink] withdrawal under-subtracted by N J ...` when the server pool could not cover a queued withdrawal. In a multiworld the pool is shared and each client only resyncs every ~1s, so two players can both see the same balance and both spend it before either withdrawal reaches the server; the server clamps at 0 and one client under-subtracts. This cannot be eliminated mod-side without server-authoritative ask-before-spend, which the polling protocol does not provide. Occasional lines under concurrent play are informational, not a fault.

## Received-Patch Feedback Prevention

When AP delivers a stat patch, it goes through `Patch_GiveItem` / `Patch_AllUp_GiveItem` in `patch_item.c`. Without masking, stats go up, the next EnergyLink frame sees a positive stat diff, and energy is minted - partially refunding the cost. The two delivery paths differ:

- **Direct** (`Machine_GivePatch` / `Machine_GiveAllUp`) - used in Air Ride and for any negative delta. Stats change immediately, and the call is followed by `EnergyLink_RebaseStats(ply)`, which re-snaps `prev_stats` so the change is invisible to the next send delta. Fully masked.
- **Spawn-pickup** (`SpawnItemPlayer`) - used in City Trial for positive deltas, to preserve the "+1 stat" pickup visual. `SpawnItemPlayer` (in `externals/hoshi/include/inline.h`) calls `Machine_OnTouchItem` immediately after `Item_Create` for non-`*FAKE` kinds, so the stat lands the same frame, and the spawn loop does **not** rebase. The next EnergyLink frame therefore refunds 1 MJ per stat patch (9 per all-up). Cost far exceeds the refund so it is not a duping vector, but it does leak energy back into the pool. Adding `EnergyLink_RebaseStats(i)` after the spawn loop closes it if it ever matters.

## Top Ride

Top Ride has no `RiderData`/`MachineData`, so the per-rider hook cannot fire. `EnergyLink_OnTopRideLoadEnd` creates a standalone GObj that walks `TopRideKirbyMgr.kirbys[0..3]` and tracks each human Kirby's `charge.charge_value`. Charge is the *only* energy source in Top Ride - there are no patches and no breakable objects. Human filtering uses `TopRide_GetPlayerKind(kirby->player_slot) == TR_PKIND_HMN`, since iterating `mgr->kirbys[i]` alone would also pick up CPU kirbys. `needs_baseline` is forced to 0 on TR entry: there is no intro or perm-patch sequence to skip.

Auto-Charge runs the same deficit/withdraw math, but is gated on **`kirby->charge.is_charging && kirby->charge.charge_ready`** - only while the player is actively holding A and not in the post-release boost lockout. That is exactly the window in which `TopRide_ChargeUpdate` (0x802df900) accumulates charge; on every other frame it runs its depletion branch, which subtracts roughly `1.0 / charge_tier_count` (~0.3) per frame and dwarfs the ~0.02 inject cap. Injection outside the window is wiped before it can accumulate, so the gate is what makes TR Auto-Charge work at all, and passive fill is impossible in TR.

Neither omission costs the player anything. During post-release depletion (`charge_ready == 0`) the boost speed was already computed from `charge_at_release` and the engine drains charge to 0, so injecting would fight the depletion math or stall the next cycle. While idle (`is_charging == 0`) a TR boost only fires on A-release, so a topped-up meter would do nothing even if it survived.

## Lifecycle

`On3DLoadEnd` calls `EnergyLink_On3DLoadEnd()` when the toggle is on: `ResetTracking(1)`, then a per-rider proc on each human at priority `RDPRI_HITCOLL + 1`. `OnTopRideLoadEnd` calls `EnergyLink_OnTopRideLoadEnd()`: `ResetTracking(0)`, then the standalone TR proc. The `int` argument is the `needs_baseline` seed - 1 for AR/CT (defer past intro), 0 for Top Ride.

`ResetTracking` zeros all per-player snapshots. It does **not** touch `energy_sent_total` or `energy_frac_accumulator` - those are the session-cumulative send channel and persist across scene loads, resetting only on a fresh mod boot via the `OnBoot` `memset`. It does clear `withdraw_balance_remainder`, which is only local display rounding that `set_notify` overwrites anyway. Procs die with their host GObj at scene exit; nothing is detached manually.

`energylink.h` exports only `EnergyLink_On3DLoadEnd`, `EnergyLink_OnTopRideLoadEnd`, `EnergyLink_Deposit` (a debug-only local balance bump that queues no server send) and `EnergyLink_RebaseStats`. `EnergyLink_Emit`, `EnergyLink_Withdraw`, `AutoCharge_Gain`, `ResetTracking` and the two per-frame procs are static to `energylink.c`, so the send channel has exactly one writer by construction.

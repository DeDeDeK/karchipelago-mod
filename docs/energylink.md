# EnergyLink

EnergyLink is the Archipelago link mechanic that lets players generate a shared "energy" currency from in-game activity and spend it on items via an in-game shop. The mod owns local accumulation, the AP client owns the actual pool total.

Source: `mods/archipelago/src/energylink.c`, `mods/archipelago/src/energylink_spend.c`. Toggle: `ap_menu_settings.energylink_enabled`.

## Protocol

Two shared fields in `APData` (`docs/client-game-protocol.md` is canonical):

| Field | Direction | Semantics |
|-------|-----------|-----------|
| `energy_balance` (s64) | Client → Game | Current AP pool total in raw MJ units (1 MJ = 1,000,000 J on the AP pool). Game reads for purchase validation and Auto-Charge; the game also locally subtracts on spend for immediate UI feedback (eventually overwritten by client). Widened to s64 so multiworld pools that exceed u64 joules still fit at MJ scale. |
| `energy_sent_total` (s64, signed) | Game → Client | **Cumulative net** energy emitted to the pool this session, raw MJ. Positive contributions = deposits (generation), negative = withdrawals (spends / Auto-Charge). **Single-writer:** the game only ever adds/subtracts; the client only **reads-and-diffs** it (never writes). Resets to 0 on mod boot; persists across scene loads. |

Cumulative-counter model: the game maintains a running net total and writes it freely — generation adds, spends/Auto-Charge subtract — as many times per frame as it likes. The client reads the counter once per ~1s poll, computes `delta = current − last_seen`, forwards that delta to the server, and advances `last_seen`. Any number of game-side writes between two polls collapse into one net delta, so there is **no flush, no slot handshake, and no per-frame polling**. Sub-MJ generation accumulates in a `float` carry (`energy_frac_accumulator`); only whole MJ are committed to the counter, and the fractional remainder rolls forward.

64-bit fields are not atomic on PPC32, so a client poll mid-write could see a torn value. With the counter this is **self-correcting**: a torn read skews one poll's delta, but the client sets `last_seen` to whatever it read and the next poll's diff compensates exactly. The client re-seeds `last_seen` on a fresh `game_ready` transition so the boot reset (counter → 0) is never misread as a giant withdrawal. See `docs/client-game-protocol.md` "APData Layout" / "EnergyLink" for the full contract.

The mod side stores raw MJ; the AP server stores integer Joules (`ENERGY_LINK_EXCHANGE_RATE = 1_000_000`). The client scales between them. See `docs/client-game-protocol.md` "EnergyLink" for the conversion contract.

## Received energy

Energy arriving from the AP server (other players' generated energy, or a credit from an energylink trade) lands in `ap_data->energy_balance` via the client's `set_notify` push. The game reads the balance for purchase validation and Auto-Charge; **no in-game notification is shown** when the balance rises. Receiving energy is silent by design — only spends (the buy menu) surface a TextBox.

## Generation

Three sources, all positive deltas only:

1. **Objects destroyed** — `stc_playerdata[ply].objects_destroyed_num`. Effectively City-Trial-only; AR never increments this.
2. **Patches collected** — `rd->stats.values[i]` delta across all 9 `PatchKind` slots. Each +1 stat = 1 energy.
3. **Charge gained** — `md->charge_value` delta, scaled by `CHARGE_ENERGY_SCALE` (5.0). A full 0→1 charge = 5 energy.

Per-player snapshots (`prev_obj_destroyed`, `prev_stats`, `prev_charge_value`) are kept so multi-human-CT doesn't cross-contaminate.

### Baseline gating

`needs_baseline[ply]` defers the first frame's snapshot until `Gm_GetIntroState() == GMINTRO_END`. This is so that round-start permanent-patch application (which happens after intro ends) is captured *as the baseline* rather than as a +N energy gain.

This works because `main.c` calls `PermanentPatch_On3DLoadEnd` before `EnergyLink_On3DLoadEnd`, and the standalone perm-patch GOBJ (`PermanentPatch_PerFrame`) runs before the per-rider EnergyLink proc on the intro-end frame.

## Auto-Charge

Opt-in toggle under Settings → Energy Link → Auto-Charge. Each frame, tops up the machine's charge meter by spending energy — but only by a **capped per-frame amount** so the meter rises steadily and *assists* the player's own charging (holding A / gliding) instead of snapping straight to full whenever energy is available.

The cap is a fixed per-frame charge gain chosen by the **Auto-Charge Rate** menu setting (Settings → Energy Link → Auto-Charge Rate: Slow / Medium / Fast), indexing `AUTOCHARGE_RATES[]` in `energylink.c`:

| Setting | Per-frame gain | ≈ frames to fill 0→1 | ≈ time @60fps |
|---------|----------------|----------------------|---------------|
| Slow    | `0.00555`      | ~180                 | ~3.0s         |
| Medium (default) | `0.01111` | ~90              | ~1.5s         |
| Fast    | `0.02222`      | ~45                  | ~0.75s        |

The total energy cost to fill the meter is unchanged (`1.0 * SCALE = 5` per full charge) — the cap only spreads that spend across frames rather than committing it in one.

```
// AutoCharge_Gain(charge_value):
//   cap = AUTOCHARGE_RATES[autocharge_rate]   // 0.00555 / 0.01111 / 0.02222
//   gain = min(1.0 - charge_value, cap)       // bounded by rate cap AND remaining deficit
//   if balance <= 0: gain = 0

charge_gain = AutoCharge_Gain(md->charge_value)
md->charge_value += charge_gain
EnergyLink_Withdraw(charge_gain * SCALE)         // emit -delta to the counter + decrement local balance
prev_charge_value[ply] = md->charge_value        // mask injection from next-frame send delta
```

Because the per-frame cap keeps the cost (`gain * SCALE`, max ≈ 0.11) well under one energy unit, any positive integer `balance` can pay for a full step — so the affordability check collapses to `balance > 0`, with no `s64→float` partial-affordability math (`balance / SCALE`) needed. The small per-frame deltas also shrink the torn-read window on the 64-bit fields.

### Passive vs. active charging — mode difference (limitation)

Auto-Charge behaves differently between modes, and this is a **known limitation**, not a bug:

| Mode | Builds charge passively? | Behavior |
|------|--------------------------|----------|
| **Air Ride / City Trial** | **Yes** | The meter fills from the energy pool even when you are *not* holding A. The 3D engine doesn't aggressively bleed off an idle charge, so the small per-frame injection accumulates on its own — energy steadily becomes a topped-up boost you can release at will. |
| **Top Ride** | **No** | Auto-Charge only *assists while you are actively holding A to charge*. It cannot build the meter passively. |

The Top Ride restriction is forced by the engine: `TopRide_ChargeUpdate` **decays `charge_value` toward 0 on every frame A isn't held**, at roughly `1.0/charge_tier_count` (~0.3) per frame — over 10× the fastest injection cap (0.02222). Any energy injected while idle is wiped the next frame before it can accumulate, so passive fill is physically impossible without fighting (and visibly stuttering against) the engine's depletion. Auto-Charge therefore gates on `is_charging` in TR and only tops up the meter *faster* while the player charges, rather than charging *for* them. This is also moot for play: in Top Ride a boost only fires on A-release, so a passively-filled idle meter would do nothing anyway. See the "Top Ride" section below and `docs/topride-system.md` for the engine details.

### Meta Knight exclusion

Auto-Charge is **skipped entirely for Meta Knight** (`md->kind == VCKIND_WINGMETAKNIGHT`). His Wing machine has no chargeable boost meter — `charge_value` acts as a raw speed term, so injecting it every frame pins him at a constant max-speed buff instead of assisting a charge cycle. The guard is on the injection block in `EnergyLink_PerFrame` only; his charge-gain *generation* (holding A for a charge dash) is left intact and mints energy like any other character. King Dedede has a normal charge meter and is **not** excluded. Top Ride has no machine kinds, so the exclusion doesn't apply there.

`EnergyLink_Withdraw` decrements `ap_data->energy_balance` immediately (by whole MJ, with a fractional carry for sub-1-MJ Auto-Charge spends), so the affordability gate self-limits in real time. This is essential: the gate reads the local balance, which is only refreshed by the client's `set_notify` push at its ~1s poll rate. If withdrawals waited for that push, Auto-Charge would re-read the same stale positive balance every frame and keep approving spends for up to a second — committing far more energy than the shared pool holds. The client then reports each over-commit as `[EnergyLink] withdrawal under-subtracted ... pool was lower than the mod expected`. Decrementing locally closes that window; `set_notify` still overwrites the balance with the authoritative value (it *replaces* rather than subtracts, so the local decrement is never double-counted). See "Under-subtraction" below.

The `prev_charge_value` re-snap is the only thing preventing a feedback loop on charge.

## Spending (`energylink_spend.c`)

A static tree of `SpendEntry { APItemId item_id; s64 cost }` (cost in integer raw MJ), exported as `MenuDesc energylink_spend_menu` (`energylink_spend.h`) and plugged into the Settings menu. Each leaf is built by the `BUY(item, cost, label)` macro, which wires `Buy` as the `OptionDesc.on_action` and stores the `SpendEntry` in `user_data`. Top-level categories:

| Category | Items | Cost (raw MJ) |
|----------|-------|---------------|
| Stat Patches | 10 | 250 each (All Up 2000) |
| Permanent Patches | 10 | 3500 each (Perm All Up 25000) |
| Copy Abilities | 11 | 600 each |
| Food | 12 | 200–1000 |
| Special Items | 5 | 1000–2500 |
| Legendary Pieces | 8 | 5000 each (full Dragoon/Hydra 17500) |
| City Trial Items | 7 | 800–3200 |
| City Trial Events | 16 | 2500 each |
| Top Ride Items | 22 | 400–800 |
| Checkbox Fillers | 3 | 50000 each |
| Cosmetic | 2 | 500 each (Big / Small Kirby) |

Prices are deliberately high: EnergyLink mints fast in City Trial (a full machine charge is worth ~5 MJ via `CHARGE_ENERGY_SCALE`, plus 1 MJ per destroyed object and 1 MJ per +1 stat patch), so a single strong round can generate very roughly 1–2k MJ and several hundred charges per round are possible. The shop is scaled to stay a meaningful sink rather than a buy-everything button. Checkbox Fillers are an extreme premium (50000) because they can complete the player's own checklist locations (a square of the player's choice) and advance the "fill N blocks" goal.

**No purchasable progression.** Every code sold maps to a filler/useful/trap item — the Copy Abilities, City Trial Events, and Top Ride Items use the `*_GIVE` codes, not the progression `*_UNLOCK` codes. There is **no Upgrades category**: Patch Cap Increase is AP progression (it gates logic and can be the City Trial goal itself) so energy must never buy it, and Spawn Rate Up is excluded alongside it so no persistent run-altering upgrade can be energy-bought.

**Event purchases are unlock-gated.** A City Trial Event give item can only be bought if the player has unlocked that event — `Buy` checks `ap_save->event_unlocked_mask & (1 << (item_id - AP_EVENT_BASE))` (same `EventKind` mapping as the give path in `ap_item_handler.c`). Without the bit the purchase is rejected (TextBox "Event not unlocked"), so energy can't fire an event the seed hasn't granted, keeping events in logic.

Purchase flow (`Buy`):

1. Reject if the item is a City Trial Event give whose `EventKind` bit is unset in `ap_save->event_unlocked_mask` (TextBox "Event not unlocked"). Non-event items skip this gate.
2. Reject if `balance < cost` (TextBox "Not enough energy" notification).
3. Reject if the unprocessed queue is full (`ap_save->unprocessed_count >= MAX_RECEIVED_ITEMS`, 512) — TextBox "Queue full".
4. Push `item_id` onto `ap_save->unprocessed_items[]` so `APItems_PerFrame` applies it on a later frame, gated by scene/intro — same path as items received from AP.
5. Subtract `cost` directly from both `ap_data->energy_sent_total` (the cumulative send counter — the client diffs it and forwards `-cost` to the server) and `ap_data->energy_balance` (instant UI feedback + keeps the step-1 gate honest). Both are `s64 -= s64`, inline on PPC32 — no float round-trip, no `__floatdisf`. The integer cost lands on the counter **exactly and immediately**, so the withdrawal reaches the server on the next poll regardless of scene — this is required because no gameplay frame runs in the menu to drive a per-frame flush. Auto-Charge's fractional spends still go through `EnergyLink_Withdraw` (counter via `EnergyLink_Emit` + balance carry); purchases take the direct integer path to avoid mixing an exact integer cost into the fractional generation carry.

On its next poll the AP client sees `energy_sent_total` decrease, computes the negative delta, and forwards it to the server as a tagged `Set` with `want_reply: true`, matching the `SetReply` against `pending_withdrawals[tag]` to log any under-subtraction (pool was lower than the mod thought). The mod's local balance is corrected on the next `set_notify` push regardless of whether the server clamped.

### Under-subtraction

The client logs `[EnergyLink] withdrawal under-subtracted by N J (asked X, got Y). Pool was lower than the mod expected.` whenever the server pool couldn't cover a withdrawal the mod queued. Two distinct causes:

- **Stale-balance over-commit.** If `EnergyLink_Withdraw` didn't decrement the local balance, Auto-Charge would keep spending against a stale positive `energy_balance` every frame within the client's ~1s poll window — committing several MJ more than the pool held, often a full meter-fill (`asked 5000000, got 0`), and producing *repeated* log spam (especially visible in Top Ride where boosts, hence meter-refills, come fast). `EnergyLink_Withdraw` decrements the local balance immediately, so the affordability gate stops once the locally-known pool is exhausted.
- **Shared-pool race (inherent, benign).** In a multiworld the pool is shared and each client only resyncs every ~1s, so two players can both see the same balance and both spend it before either withdrawal reaches the server. The server clamps at 0 and one client under-subtracts. This can't be eliminated mod-side without server-authoritative ask-before-spend, which the polling protocol doesn't provide. Expect occasional lines of this kind under concurrent play; they're informational, not a fault.

## Received-patch feedback prevention

When AP delivers a stat patch (e.g. another player buys you "HP Patch" via energylink trade), the patch goes through `Patch_GiveItem` / `Patch_AllUp_GiveItem` in `patch_item.c`. Without masking, stats go up → next EnergyLink frame sees a positive `stat_diff` → energy is generated, partially refunding the cost.

`Patch_GiveItem` has two delivery paths:

- **Direct (`Machine_GivePatch` / `Machine_GiveAllUp`)** — used in Air Ride, and for any negative-num delta. Stats change immediately. After the call, `EnergyLink_RebaseStats(ply)` re-snaps `prev_stats` so the new stats are invisible to the next send delta. **Fully masked.**
- **Spawn-pickup (`SpawnItemPlayer`)** — used in City Trial for positive deltas, to preserve the "+1 stat" pickup visual. `SpawnItemPlayer` (in `externals/hoshi/include/inline.h`) calls `Machine_OnTouchItem(md, id)` immediately after `Item_Create` for non-`*FAKE` item kinds, so the stat change lands the same frame. `Patch_GiveItem` does **not** call `EnergyLink_RebaseStats(i)` after the spawn loop, so the next EnergyLink frame still sees the delta and refunds **1 energy per stat patch (9 per all-up)**. Cost is much greater than the refund, so it is not a duping vector — but it does leak energy back into the pool.

If the leak ever matters (cross-world energy farming concerns), add `EnergyLink_RebaseStats(i)` after the spawn loop in `Patch_GiveItem` / `Patch_AllUp_GiveItem` — same fix as the direct path.

## Top Ride

Top Ride has no `RiderData`/`MachineData`, so the per-rider hook can't fire. `EnergyLink_OnTopRideLoadEnd` creates a standalone GOBJ that walks `TopRideKirbyMgr.kirbys[0..3]` and tracks each human Kirby's `charge.charge_value` (offset 0xB4 — see `docs/topride-system.md`). Charge is the *only* energy source in Top Ride; there are no patches and no breakable objects.

Human filtering uses `TopRide_GetPlayerKind(kirby->player_slot) == TR_PKIND_HMN` (per-slot config block at `GameData[slot*9 + 0xD20]`). Iterating `mgr->kirbys[i]` directly would also pick up CPU kirbys; `kirby->start_position` (Kirby+0x0E) is a per-round shuffled grid index, **not** a CPU level. See `docs/topride-system.md` "Player Kind" section.

`needs_baseline` is forced to 0 on Top Ride entry — there is no intro/perm-patch sequence to skip.

Auto-Charge is supported in TR with the same deficit/withdraw math as AR/CT, but gated on **`kirby->charge.is_charging && kirby->charge.charge_ready`** — i.e. only while the player is *actively charging* (A held, and not in the post-release boost lockout).

This is stricter than AR/CT, and necessarily so. Unlike 3D mode, the Top Ride engine (`TopRide_ChargeUpdate`, 0x802df900) **decays `charge_value` toward 0 every frame that A is not held**: it takes its depletion branch whenever `(!A_held || charge_ready == 0)` and subtracts roughly `1.0 / charge_tier_count` per frame (~0.3), which dwarfs our per-frame inject cap (~0.02). So a `charge_ready`-only gate (the original) let injection fire during *idle* too — where the engine wipes it the very next frame and the meter never fills. That was the "received energy doesn't charge up in Top Ride" bug.

The engine only *accumulates* charge in its other branch, which runs exactly when `A_held && charge_ready == 1`. Gating Auto-Charge on `is_charging` (A held, set/cleared by the engine) plus `charge_ready` matches that window: there, our injection sticks and stacks on top of the player's own charging, so they reach a bigger boost with less hold time — the "assist your charging" semantics of AR/CT. Outside that window we inject nothing:

- **Post-release depletion** (`charge_ready == 0`): the boost mechanic — `boost_speed` is computed from `charge_at_release` snapped at the moment A is released, and the engine subtracts charge each frame until it hits 0. Injecting here would fight the depletion math or stall the next charge cycle.
- **Idle** (`is_charging == 0`): the engine is decaying charge to 0 by design; there is no charge to maintain, and in TR you cannot boost without holding then releasing A, so a topped-up idle meter would do nothing anyway.

## Lifecycle

| Hook | Action |
|------|--------|
| `On3DLoadEnd` | `EnergyLink_On3DLoadEnd()` if toggle on — `ResetTracking(1)` then add per-rider procs at `RDPRI_HITCOLL+1`. |
| `OnTopRideLoadEnd` | `EnergyLink_OnTopRideLoadEnd()` if toggle on — `ResetTracking(0)` then create standalone TR proc. |

`ResetTracking` zeros all per-player snapshots. It does **not** touch `energy_sent_total` or the `energy_frac_accumulator` carry — those are the session-cumulative send channel and persist across scene loads (the channel only resets on a fresh mod boot, via the `OnBoot` `memset`). It still clears `withdraw_balance_remainder` (local display rounding for `energy_balance`, which `set_notify` overwrites anyway). Its `int` argument is the `needs_baseline` value to seed — `1` for AR/CT (defer past intro), `0` for Top Ride. Procs auto-die with their host GObj at scene exit; nothing manual to detach.

## Public API

```c
void EnergyLink_On3DLoadEnd(void);
void EnergyLink_OnTopRideLoadEnd(void);
void EnergyLink_Withdraw(float amount);    // emit -amount into energy_sent_total (via the float carry) AND decrement ap_data->energy_balance by whole MJ (fractional carry). Auto-Charge path; purchases subtract from the counter/balance directly.
void EnergyLink_Deposit(float amount);     // local-only balance bump (debug). Does NOT queue a server send.
void EnergyLink_RebaseStats(int ply);      // re-snap prev_stats[ply] to current rider stats
```

`EnergyLink_PerFrame` and `EnergyLink_TopRidePerFrame` are static — they're only registered as GObj procs from inside the file.

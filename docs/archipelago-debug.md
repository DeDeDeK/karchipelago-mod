# Archipelago Debug Mod

`archipelago_debug` is a test harness for everything the `archipelago` mod gates, grants and
checks. It ships no game content of its own: it drives the other mods through their exported
APIs, so a build without it behaves identically. Two surfaces - a settings-menu tree of unlock
gates, item gives and slot-option overrides, and a set of controller-1 pad bindings for actions
that only make sense inside a live round.

The settings menu is a main-menu scene: it is built on `MnSettings`, over `Gm_GetMenuData`'s
canvas, and there is no in-round overlay. That is the whole reason the split exists. Anything
that has to happen while a round is running is a pad binding; anything that arms state the next
round reads, or that the AP receipt queue drains on its own, is a menu row.

The mod imports two APIs from `OnSaveLoaded`, which hoshi runs after every mod has registered,
so the exports are guaranteed to exist by then:

| Import | Absent means |
|--------|--------------|
| `archipelago` | Every menu row and pad binding no-ops; `OnFrameStart` returns immediately |
| `custom_events` | D-Pad Up (Scale Change) is disabled; everything else works |

Text feedback goes through `ArchipelagoAPI.Textbox`, so the mod never imports `textbox` itself.

## Pad bindings

All ten read controller port 1 (`stc_engine_pads[0]`) on the rising edge, from the
`OnFrameStart` hook that runs every tick in every scene.

Nine of them are gated on a live round: minor 18 (`MNRKIND_3D`, Air Ride and City Trial) or
minor 19 (Top Ride gameplay), under major `MJRKIND_CITY` / `MJRKIND_AIR` / `MJRKIND_TOP`. That
gate exists because the hoshi settings menu and the vanilla checklist grid are both D-Pad
navigated off the same controller - without it, changing any option value or moving the
checklist cursor would also fire a debug action. The major test doubles as the auto-demo guard:
the title screen's attract demo runs a real round, but under `MJRKIND_TITLE`.

| Binding | Action | Gated |
|---------|--------|-------|
| D-Pad Left | Grant one checklist reward the seed placed remotely (`GetShuffledReward == 0xFFFF`), picked uniformly by reservoir across all three modes | round |
| L + D-Pad Left | End the round now | round |
| D-Pad Right | Queue one random AP unlock or persistent progression item | round |
| D-Pad Down | Drop one AP Box in front of the target player | round |
| L + D-Pad Down | Set the `deathlink_receive` side-channel flag | round |
| R + D-Pad Down | Drop the next Archipelago Star sphere, cycling one per press | round |
| D-Pad Up | Fire the Scale Change custom event | round |
| L + D-Pad Up | Queue one random in-game item appropriate to the current major mode | round |
| R + D-Pad Up | Set the `traplink_receive` side-channel flag | round |
| Z | Unlock the checklist cell under the cursor | checklist screen |

The two drops read the **Target Player** row on the root menu rather than always using slot 0,
so per-player behavior - which is where three shipped bugs came from - can be exercised against
any of the four slots.

The sphere cycle index resets at `On3DLoadEnd`, so each City Trial round starts at the Rose
sphere. A locked sphere has no `ItemKind` and is skipped rather than stalling the cycle; the AP
Box likewise reports failure while `ap_patches` is 0, because the drop-ins are held out of the
item registry until a round loads with a nonzero count.

### Ending the round

Each mode's branch of `Game_Think` falls through to an unsigned `seconds_passed >= time_seconds`
comparison (`Game_Think+0xa48`) once that mode's own end condition has not fired, where
`time_seconds` is `GameData` 0xa9c, the configured limit rather than the elapsed clock. Zeroing
it therefore ends the round through the engine's own timeout path, so results, stadium selection
and every round-end check and goal evaluate exactly as they would at a natural finish. A
lap-limited Air Ride race ends on its lap count instead and is unaffected. `time_seconds` is
re-seeded when the next round is set up, so the write does not persist.

This is what makes the round-end goals testable at all: `GOAL_HYDRA_AND_DRAGOON` and
`GOAL_BEAT_KING_DEDEDE` only flip their clear kind when the vanilla results screen runs.

### The Z unlock

`GetHoveredCell` is the whole gate: it returns 0 unless a checklist screen is up with the cursor
on a grid cell, so Z outside that screen does nothing. Z itself is unused by vanilla checklist
navigation.

The handler calls `ClearChecker_SetNewUnlock` (0x8004a054), which `archipelago` has
`REPLACEFUNC`'d - so the AP check fires and goal evaluation runs exactly as a real completion
would. The replacement then bails on `Checklist_IsCacheValid`, which is always true in menus, so
`RecordCheck` ran but no `clear[]` bits were written; the handler writes `is_new`,
`is_unlocked` and `is_visible` itself to reach the same end state.

`ResolveCell` reports which `(source_mode, reward_index)` the cell holds, accounting for
cross-mode shuffling, and the console line names the reward type. With `Auto-Grant on Z Unlock`
enabled the handler also grants that reward and writes the memory card - off by default, because
a connected client delivers the reward itself and it would otherwise be granted and announced
twice.

### The random pools

D-Pad Right draws uniformly from every AP unlock category (stadiums, events, copy abilities,
base abilities, patch types, CT items, machines, boxes, AR stages, colors, TR stages, TR items,
AP Star spheres) plus the persistent progression items - permanent patches, Perm All Up, Patch
Cap Increase, Spawn Rate Up.

The machine block covers the 22 vanilla kinds with a player-facing unlock plus the Archipelago
Star's 856. `VCKIND_WINGKIRBY`, `WHEELNORMAL` and `WHEELKIRBY` are copy-ability and enemy forms
and `WHEELVSDEDEDE` is the Vs. King Dedede stadium's CPU-only machine; the AP world ships no item
for any of them.

L + D-Pad Up picks from whichever pool suits the running mode: in City Trial, one uniform draw
across the event range, the full `ITKIND` range and the seven standalone gives in the 1-99 block;
in Air Ride, copy abilities only, since every other `ITKIND` no-ops behind the `Gm_IsInCity`
gate; in Top Ride, the TR item gives, which apply their item to each human Kirby directly.

## The menu tree

Root option "Archipelago Debug": nine submenus, a Target Player row, and two actions.

| Page | Covers |
|------|--------|
| Unlock Gates | The thirteen per-category gate masks |
| Give Items | Twelve submenus of direct item gives |
| Checks | Sent-check bitmask, goal stickies, checklist reveal, AP Patches |
| Goals | The goal of each of the four checklist rows |
| Slot Options | Category gating, patch cap range, spawn rate floor, re-apply |
| Check Progress | The three cross-session AP checklist counters |
| Messages | Canned client text lines, one per `APTextKind` |
| Links | Arm a DeathLink or TrapLink receive |
| EnergyLink | Set, add to or drain the energy balance |
| Target Player | Which slot the pad drops act on |
| Report State | Log every mask, counter and goal |
| Reset Progression | Roll received-item progression back to pre-connect |

The menu shows five rows at a time, which is why the thirteen gate pages sit under one entry
rather than at the root.

### Unlock Gates

Thirteen pages - Machines, Copy Abilities, Base Abilities, Events, Patch Types, CT Items, Box
Types, AP Star Spheres, AR Stages, TR Stages, TR Items, Colors, Stadiums. Each opens with
`Unlock All` / `Lock All` / `Give Random` and then one Disabled/Enabled row per bit of that
category's AP unlock mask.

`Give Random` queues one unlock item drawn uniformly from that category's own id runs, which is
the narrow form of what D-Pad Right does across every category at once. Both draw from the same
table, so the machine gaps and the custom-slot clamp described below apply identically.

Every gate row is `no_save`. Its value is re-derived from the mask at save load and at every
scene change, so a memory-card copy would only be overwritten - and the machine rows are renamed
at runtime, which would move their save hashes anyway.

### How a toggle writes back

The rows share one cached `int` array per category, which is what the menu renders and what a
toggle's `on_change` reads. Rebuilding the whole mask from that array would be wrong: any bit
the mask gained elsewhere during the session - an AP delivery, a Give action, a granted reward -
is not in the array, and an unrelated toggle would silently revoke it.

So each category also keeps the mask value it last synced. A toggle treats only the bits that
differ from that snapshot as its own; every other bit keeps whatever the live mask holds:

```
owned = built ^ synced
mask  = (live & ~owned) | (built & owned)
```

The array is then re-derived from the result, so a row that changed outside the menu starts
displaying correctly as soon as anything in its page is touched. `OnSceneChange` re-derives all
thirteen categories outright, which covers the common case of receiving items between rounds and
then opening the menu.

The Machines page drives all 27 bits of `machine_unlocked_mask`: the vanilla `MachineKind`s, and
bit 26, the Archipelago Star, as its last row. No other registered machine has a bit, so none
has a row.

### Give Items

Twelve submenus, each row queuing one AP item id through `QueueItem` so it takes the same receipt
path a client delivery would: Stat Patches, Permanent Patches, Copy Abilities, Base Ability
Unlocks, AP Star Spheres, Food, Special Items, Legendary Pieces, Top Ride Items, CT Events,
Traps, Upgrades.

Two rows named "All Up" cover the two distinct items: Stat Patches spawns the `ITKIND` pickup,
Upgrades grants the AP-band item to each rider. The AP Star Spheres page grants sphere *unlock*
items, while Legendary Pieces' sphere rows collect the sphere directly - that path is gated only
on a live City Trial round, not on the sphere's unlock bit.

### Checks

- **Auto-Grant on Z Unlock** - the mod's only persisted option, since nothing else re-derives it.
- **Clear All sent_checks**, **Force-Mark All**, **Trigger goal_complete** - direct manipulation
  of the sent-check bitmask and the sticky goal bits. Force-Mark sets every backed bit,
  `goal_complete` and `goal_announced`; it deliberately leaves the AP tab's unbacked cells clear.
- **Reveal Checklists** - "All Checklists" plus one row per checklist-mode row. Visual only:
  it sets `is_visible` and leaves `is_unlocked` to the normal completion path.
- **Simulate Location Data**, **Clear All Checklist Data** - fill the location arrays with a
  random shuffle, or wipe every checkbox flag, sent-check bit and shuffle entry.
- **AP Patches** (Off / 8 / 64 / 512), **Collect AP Patch** and **Clear Collected AP Patches**.
  The clear wipes the save bits, the wire mirror and the client's pending backfill together -
  leaving the backfill would let the client's next push restore every bit and the patch would
  stay unclaimable.

The check-state commands write the memory card immediately; the reveal commands and the AP Patch
rows do not.

### The boot replay rule

hoshi replays every `OPTKIND_VALUE` `on_change` once at boot, right after every mod's
`OnSaveLoaded`, recursing into submenus. `no_save` does not exempt a row. So any row here that
writes AP state would, at every launch, push the menu's idea of the world back over the seed's.

Every such row therefore latches. `DebugMenu_RefreshState` runs first, in `OnSaveLoaded`, and
installs the live value into both the row and a paired `synced` variable; the callback returns
early when the value it is handed equals that snapshot, so only a real move does anything. The
same refresh runs at every scene change, which keeps the rows honest after the client delivers
something between rounds.

AP Patches is the row that made this necessary. `DebugSetApPatchCount` rewrites the seed's own
`options.ap_patches` and trims every collected bit past the new ceiling, and the row shows the
nearest offered size at or below the live count - so a seed sitting between rows (the apworld's
default is 20) was being rewritten down to 8 on every launch, discarding collected patches 8
through 19.

### Goals

One row per checklist-mode row, cycling the nine `APGoalKind` values, plus a shared **Squares
for N** threshold the count rows compare against - and an **Apply** action that commits all five
at once.

The rows only select, and Apply is what writes. That split is not cosmetic. hoshi fires a value
row's `on_change` on every D-pad tick, auto-repeat included, and goal evaluation is over the
whole set rather than one row: victory needs one non-`GOAL_NONE` row and every row satisfied. So
committing each value a row passes through would let a scroll stop momentarily on a kind that
happens to be satisfied - with three rows on `GOAL_NONE`, which is vacuously satisfied, that is
one row away - and latch `goal_complete`, which is sticky and which the client reports to the
server. Applying the set as a unit means only what is on screen is ever evaluated.

Apply also passes the threshold only to the rows actually on `N Squares`, so setting an unrelated
row's goal does not rewrite its square count, and it evaluates and writes the card once rather
than once per row.

Two caveats the rows cannot express:

- `goal_complete` is sticky. A new goal on a save that already goaled shows nothing until
  **Clear All sent_checks** on the Checks page resets it.
- `GOAL_MAX_STATS_CT` is the one goal armed at round load rather than evaluated continuously:
  `GoalMaxStatsCT_On3DLoadEnd` attaches its per-rider proc only when the City Trial row already
  holds that goal, so it takes effect from the next round rather than this one.

### Slot Options

The options the seed ships and the client normally owns, none of which any other surface can
change without re-rolling a seed:

- **Category Gating** - twelve toggles, one per gateable category, mirroring the
  `*_gating_enabled` flags. `AP_UNLOCK_AP_STAR_PIECE` has no flag of its own; the spheres follow
  the CT item category.
- **Patch Cap Min / Max** - the per-stat cap a City Trial run starts at and the ceiling Patch Cap
  Increase items raise it to. A stored 0 means options were never received, which the mod reads
  as the `PATCH_STAT_MAX` ceiling, so the rows show 127 in that state. Each row writes only its
  own bound: the rows offer fixed steps and display the nearest at or below the live value, so a
  row that wrote both would round the one nobody touched down to its displayed bucket.
- **Spawn Rate Floor** - the item spawn rate before any Spawn Rate Up.
- **Re-apply** - zero every unlock mask, then re-run the connect-time reveal and ungated pre-fill.

Re-apply exists because the gating flags are read once, at connect, and almost nowhere else -
`GateTopRideItems_ApplyMask` is the only gate that reads one at runtime. Everything else reads
its own mask, so a flag changed on its own is invisible. The pre-fill also only ever *sets* bits,
so turning a category's gating back on would leave the all-ones mask its ungated pass installed;
clearing the masks first is what makes this a fresh connect rather than an addition to the last
one. `received_checklist_rewards` is deliberately not cleared - it also holds rewards the player
genuinely received, and wiping those is Clear All Checklist Data's job.

Two things Re-apply will do that are easy to walk into. On a save that has never connected every
gating flag reads 0, which the pre-fill treats as ungated, so it unlocks the entire seed; the
console says so before it runs. And on a connected save the leading mask clear discards unlocks
the player legitimately received in any category that *is* gated - the item queue is consumed,
not replayed, so nothing brings them back.

### Check Progress

The three AP checklist objectives whose predicate counts across sessions, so that verifying them
does not mean grinding: All Ups collected (5 completes `APCK_ALLUPS_5`), SINGLE RACE 1 wins as
Purple Kirby (3 completes `APCK_SR1_PURPLE_3X`), and the per-colour race mask (all eight
completes `APCK_AIRRIDE_ALL_COLORS`). Set a counter one short of its target and the next real
event in a round completes the check. None of the three writes the memory card as it is moved -
that would be a blocking card write per D-pad tick - so they ride in the save block to the game's
own next save point.

The colour row is a mask rather than a count, so it offers None / Partial / All: Partial writes
seven of the eight bits, and any live mask that is neither empty nor complete displays as
Partial.

Lowering a counter past a check already recorded this session does not un-record it: the
objectives latch in `ap_check_detect.c`'s boot-scoped observed set, which has no reset.

### Messages

Seven rows, six of them one per `APTextKind`, that post a canned client-authored line into the
`APData` text mailbox exactly as the Python client does - whole record first, pending flag last.
That exercises the render path, the per-kind Messages filter, the colour palette and the
`IsReady` hold across a scene load, none of which has any local trigger otherwise. Each canned
line uses the wording and colours the client actually composes, so a rendering difference is a
real one.

The seventh row, **Overlong**, fills all eight colored runs and runs past the three lines any
font size can show, which is the only way to see the text box's wrap and its trailing `..`
truncation without a long player or item name arriving from a real seed.

A row reports failure when an earlier message has not been rendered yet - the mailbox is a single
slot and the client's own precondition is the same.

### Links

**Arm DeathLink** and **Arm TrapLink** set the side-channel flag the receive path polls. Both
flags are held rather than consumed when they cannot be applied, so arming one from the menu
lands it at the next round rather than dropping it. Which trap a TrapLink produces is chosen by
the receiving mode, not by the caller - the incoming name is shown but never acted on.

Both receive procs are installed at scene load only when their Settings toggle was on at that
moment, so a flag armed while the link is off sits until a round that has the proc.

### EnergyLink

**Balance** sets the pool to an exact value, and the offered rows bracket the affordability
check: the cheapest purchase in the spend menu is 200 MJ and the dearest 50000, and the check is
a strict `<`, so 199/200 and 49999/50000 are the two edges. **Add 1000** and **Drain to Zero**
are the coarse forms.

The override is a pure balance store. `energy_deposit_total` and `energy_withdraw_total` are
rising counters the client read-and-diffs, so lowering either would decode as a ~4.29e9 delta;
nothing here touches them. With a client attached the balance is overwritten on its next poll,
about once a second, so an exact value only holds with none connected. A drain to 0 can still go
slightly negative later: the withdraw path carries under 1 MJ of fractional remainder that the
next Auto-Charge frame applies.

Spending against a debug-inflated balance does reach the server as a genuine withdrawal, which
the pool's `max: 0` clamp floors at zero and the client logs as under-subtracted. Draining is the
safe direction.

### Report State and Reset Progression

**Report State** logs the whole picture in one block: every unlock mask in binary with its gating
flag, the effective and received patch cap, the spawn rate floor, permanent patches per stat,
each row's goal and completed-square count, the sticky goal bits, AP Patch collected and
remaining, the energy balance and its two counters, and the three check-progress counters.

**Reset Progression** rolls back everything AP receipts accumulate - patch cap count, spawn rate
level, permanent patches, check progress, `max_stats_ct_achieved`, the received-item count and
the unprocessed queue. Unlock masks are left alone; Slot Options' Re-apply is what rebuilds
those, and the slot options themselves are left as received: clearing `options_received` while
`options` still held a seed's values would leave the next boot skipping the checklist reveal it
asked for. Permanent patches already applied to a rider stay on it for the rest of the round,
since that apply latches per scene.

---
name: kar-review
description: >
    Full review of one mod under `mods/` that applies every fix it finds -
    correctness, asm patches, per-frame placement, dead and duplicated code,
    hand-written copies of hoshi or game helpers, API and client-protocol
    drift, comments, logging and docs - then verifies with a build. Use when
    the user asks to review, audit or clean up a mod or its recent additions.
    Covers the whole mod, not a diff; use `/code-review` for a diff-scoped
    bug hunt.
---

# kar-review Skill

A whole-mod review pass over `mods/<mod>/`, applying what it finds. The rules in
`.claude/CLAUDE.md` (Comments, Docs, Code Style, Debug Output, Reverse
Engineering Workflow) are the standard the code is held to. This skill adds what
to look for beyond them and how to run the pass.

## Invocation

`/kar-review <mod>` where `<mod>` is a folder name under `mods/`. If the user
names no mod, ask which one - do not guess from the working tree. More than one
mod named means one full pass per mod, in the order given.

The review covers the mod's entire `src/` and `include/`, not just changed
lines. Findings outside the named mod (in `externals/hoshi/`, another mod, or
the apworld in the sibling `../KARchipelago` checkout) get reported, not applied,
unless the user asked for those too.

## Step 1 - Baseline

Before reading any source:

```bash
git status --short -- mods/<mod> docs externals/hoshi
uv run python scripts/kar.py check > <scratchpad>/check-before.txt
uv run python scripts/review_lint.py <mod>
grep -rn "Hoshi_ImportMod" mods/<mod>/src
wc -l mods/<mod>/src/* mods/<mod>/include/*
git log --oneline -15 -- mods/<mod>
```

- Files already dirty are earlier work, not the pass's. Note them so the report
  can tell the two apart.
- The lint's hits are candidates for the audit to confirm or dismiss, not
  findings yet.
- The imported mods, followed transitively through their own imports, are the
  build set for step 5.
- The line count sizes the fan-out in step 2.

Then read the `docs/*.md` for every system the mod touches. Find them by
filename; a mod's systems usually map one-to-one onto doc names
(`gate_boxes.c` -> `docs/gate-boxes.md`). Read `docs/frame-loop.md` when the mod
has frame hooks or procs, and `docs/mod-settings-menu.md` when it has options.
Read the docs before the code: most of what looks like a bug is a documented
engine constraint, and most stale comments are stale because the doc moved on.

## Step 2 - Audit

The axes below fall into four groups, A to D. How to run them depends on size:

- **Under ~2,000 lines** (custom_ai, custom_items, custom_checklist, textbox,
  custom_events, hypernova): audit inline. Read every file and work through all
  four groups yourself. Agents would cost more than they save.
- **~2,000 to ~6,000 lines** (archipelago_debug, ap_star, custom_machines): read
  every source file in the main context, then start one agent per group with
  `subagent_type: "fork"`. A fork inherits what you read and the prompt cache,
  so nothing gets read twice. The prompt only names the group: "Run group B of
  the kar-review skill over the mod loaded above. Do not edit."
- **Over ~6,000 lines** (custom_weather, archipelago): too big to load whole.
  Split by subsystem - file-name prefixes (`gate_*`, `ap_check*`, `ap_text*`,
  the links, `ap_star`/patches, `main`/settings) - and give each
  `general-purpose` agent its file list, the docs to read, and all four groups.
  Add one agent for what crosses subsystems: groups B and C2-C3 over the
  headers and `main.c`.

Agents must not edit. Applying is serial in step 4, so two fixes cannot collide
in one file. Each agent returns one line per finding:

```
<file>:<line> | <axis> | confirmed|likely | <what is wrong> -> <what to do> | <evidence>
```

The evidence is the disasm line, grep hit, caller list or doc sentence the claim
rests on. A claim about a game address is checked with `uv run python
scripts/kar.py disasm`/`sym`/`decomp`, never inferred from a comment - the
comment is what is under review.

## Step 3 - Triage

Merge the agents' lists and the lint hits:

- Fold duplicates - the same line reported under two axes is one finding.
- Drop what a doc or the code contradicts.
- Re-check every `likely` finding that changes behavior yourself (read the
  caller, disassemble the site) before it is applied. One you cannot confirm is
  not applied; list it in the report as unconfirmed.
- Mark the stop-and-ask findings (step 4) as deferred.

## Step 4 - Apply

Apply every surviving finding, in this order, so later passes see the final
code:

1. Behavior fixes (group A, protocol).
2. Structural changes (ownership moves, deduplication, swaps to hoshi/game
   helpers, deletions).
3. Comment and OSReport cleanup.
4. Doc updates in `docs/` for anything the code changes invalidated.

Deleting is the default remedy for anything stale, vestigial, or unused - there
is no deprecation path here and no backwards compatibility to keep.

Stop and ask before: changing `APData`/`APSlotOptions`/`APItemId` (it needs a
paired apworld change), or moving functionality between mods (it changes an API
surface). Report those and keep going with the rest.

## Step 5 - Verify

```bash
make package INCLUDE_MODS=<mod>,<its transitive imports>
uv run python scripts/kar.py check | diff <scratchpad>/check-before.txt -
uv run python scripts/review_lint.py <mod>
```

A successful `make package` is sufficient - do not grep its output. `kar.py
check` must be no worse than the baseline. Every lint hit still standing is
either a deliberate keep, with the reason going in the report, or a missed fix.

## Step 6 - Report

If the `ReportFindings` tool is available, report every finding through it in
one call, most severe first, with `outcome` set: `fixed` for applied,
`skipped` for deferred or unconfirmed (say which in the summary),
`no_change_needed` for dismissed lint hits worth recording. Otherwise, one list
grouped by axis, one line per entry with a `file:line`.

Then, in text:

- Findings deferred to the user, and findings outside the mod.
- Files that were dirty before the pass.
- What to check in Dolphin for every behavior change - the build proves only
  that it compiles. Call out timing changes too, such as a handler that now
  freezes with the pause where it used to keep running.

---

# Review axes

## Group A - Runtime correctness

### A1. Correctness

The defects any C review looks for, in this codebase's shapes:

- An index from a value the code does not control - a save field, a game enum
  the table does not cover, a custom kind past the vanilla range - used without
  a bounds check. Off-by-one against a `_NUM` count.
- Integer width: a `u8`/`u16` counter or mask that can overflow, a signed and
  unsigned comparison, `1 << n` with `n` reaching the type's width (64-bit
  masks need `1ULL`).
- NULL: a game lookup that can come back empty - a GObj by kind, an archive
  public, a rider's machine while on foot, an import - dereferenced unchecked.
- Text: `sprintf`/`strcpy` into a fixed buffer with no bound on the input, or a
  `memcpy` of text that drops the terminator.
- A table that must cover an enum, with no `_Static_assert` saying so.
- Stack or `HSD_MemAlloc` memory read before it is written (only statics start
  zeroed).
- Division by a value that can be 0; exact equality on floats.

### A2. Asm patches

For every `CODEPATCH_*` site, disassemble the address before trusting anything
written about it.

Execution order for `CODEPATCH_HOOKCREATE` is prologue, `bl` to the C function,
epilogue, then **the clobbered instruction from the patch site**, then a branch
to `_exit_addr` (defaulting to `_dol_addr + 4`). Check:

- Prologue registers against what is actually live at that address. A `mr 3,31`
  is only right if r31 holds what the comment says at that exact instruction.
- The `bl` destroys r0, r3-r12, f0-f13, LR and CR. Anything the clobbered
  instruction or the code at `_exit_addr` still needs must be saved in the
  prologue and restored in the epilogue.
- The clobbered instruction is relocated into the hook and re-executed, so it
  must not be something whose meaning depends on its address beyond a branch
  (branches are re-based by hoshi; nothing else is).
- `_exit_addr` when it is not the default: it must be an instruction boundary
  reachable by an unconditional branch, and the code there must tolerate the
  register state the epilogue leaves.
- Keep prologue and epilogue short. Hoshi flushes 32 bytes from the hook's
  start, while the words it rewrites sit past the epilogue.

For `CODEPATCH_HOOKCONDITIONALCREATE`, the C function returns 0 to fall through
to `_exit_addr` and nonzero to branch to `_exit_addr_alt`. On the alt path the
clobbered instruction **does not execute** - if it had a side effect the alt
path needs, that is a bug.

For `CODEPATCH_REPLACECALL` / `REPLACEFUNC`:

- The C signature must match the target exactly, including float arguments and
  return type.
- `REPLACEFUNC` redirects every call site in the game; `REPLACECALL` redirects
  one. Prefer the call site when only one path should change, and when another
  mod already owns the function - two mods must never patch the same function
  entry.
- A `REPLACEFUNC` replacement never runs the original prologue, so it owns its
  own stack frame and every side effect the original had.

`CODEPATCH_REPLACEINSTRUCTION` needs the encoded instruction spelled out in a
comment, and the address must not be a branch target from elsewhere in the
function.

Every patch site's containing function needs a named symbol in `GKYE01.map` and
`link.ld`, and a comment beside the patch naming that function and its address.

### A3. Lifetime and allocation

- `HSD_MemAlloc` in `OnBoot` persists for the whole run. Anywhere else it is
  freed at the next scene change. Allocate persistently only when the data
  genuinely outlives a scene - a per-round buffer allocated at boot is a
  finding.
- Handles resolved per scene - custom `ItemKind`s, machine ids, registry
  lookups - are valid for that scene only and must be re-fetched at
  `On3DLoadEnd` every round.
- State that should reset between rounds but lives in a file-scope static, and
  state that should persist but lives in a scene-lifetime allocation.
- A scene teardown runs no destructors, so anything outside the heap - a
  playing sound, a slot in a fixed pool - is released before the scene exits.
- Memory cost: large arrays on the stack (hooks run deep inside game call
  chains), statics sized for a case that cannot happen, and allocations repeated
  every frame or every spawn.

### A4. Per-frame work

`OnFrameStart` and `OnFrameEnd` run in every scene for the whole game, and
`OnFrameEnd` keeps running through the match pause and the hitstops
(`docs/frame-loop.md`). Every handler on either hook has to justify running
there. The ones that do:

- Reading what the AP client writes into memory. It must work in every scene,
  menus included.
- Work that must happen outside every GObj proc. Work that only has to run off
  the caller's stack does not qualify: a machine swap asked for from inside the
  machine's own collision is a one-shot proc on the rider's GObj that frees
  itself with `GObj_FreeProc(*stc_gobjproc_cur)`, as the engine's own swap runs
  from the rider's procs.
- Mirroring a game check that itself runs every frame in every scene.
- Dev-only debug hotkeys.

Anything else moves to where it is needed, preferring in order:

1. A hook in the game function that causes the event, when one exists and is
   simpler than polling. Polling a state the game reaches with no clean hook
   (the round's GO in `intro_state`) from a proc that already runs is fine.
2. A proc the mod already runs in that scope - the per-rider check procs, a
   round's Think.
3. A GObj with its own proc, created at `On3DLoadEnd` or when first needed,
   and only in the mode and with the options that use it.
4. A callback slot in a scene the mod owns
   (`MinorSceneDesc.cb_ThinkPostGObjProc2` on a custom minor).

Typical finds: a frame hook whose first line returns outside one mode, or that
resets state every frame outside it; a manual `PAUSEKIND_GAME` check; a
stale-pointer guard on a scene object; a flag set in a proc and consumed by a
hook to detect a new tick (compare `stc_hsd_update->engine_frames` instead); a
GObj recreated on every `OnSceneChange` only to imitate `OnFrameStart`.

A proc's p_link decides which pauses freeze it. Anything that moves the world -
items, machines, props, riders - belongs on `GAMEPLINK_1`; HUD that must update
during the match pause belongs on p_link 0. A write that must win over physics
and collision goes in a proc at priority 23 rather than a hook per physics
routine.

Moving a handler between hooks, or onto a proc, changes which pauses it runs
through - report it as a behavior change to test.

### A5. Player scope and the auto demo

- The title screen's attract demo runs a real City Trial round inside
  `MJRKIND_TITLE`, with a CPU in every slot. Anything that claims a location,
  grants an item, or advances save state needs `Gm_IsAutoDemo()` in its gate.
- Per-player versus global: a check that should fire once per round must not be
  evaluated per player, and a per-player grant must not read player 0's state.
- Human versus CPU: gates that read controller slots must not treat a CPU as a
  human, and behavior meant for every rider must not skip CPUs.

### A6. Options and save

Save layout. hoshi matches a mod's card block by name hash and size only, so
every mod with a save struct opens it with a stamp (`APSAVE_STAMP`,
`CC_SAVE_STAMP`) and reinitializes on a mismatch
(`docs/memcard-save-system.md`). Any layout change - a field added, removed,
reordered or retyped, or a hash or ordering the save keys on - bumps the stamp;
one that leaves the stamp alone is a bug. A save field derivable from another
field is one field too many.

Options:

- `OptionDesc` names are free to change. A rename drops the saved value back to
  its default - the same discarded-old-save behavior a stamp change gives, and
  not a reason to keep a bad name.
- `OPTKIND_VALUE` options that change gameplay need an `on_change` callback
  that logs the new value.
- Option descriptions must describe what the option actually does now.
- Two options whose combined states include a meaningless or unreachable
  combination are one option too many.
- A clamp or default branch on an option index is dead: hoshi rejects an
  out-of-range saved index, and the menu wraps within range.

Options change only in the main menu, plus one `on_change` replay at boot
(`docs/mod-settings-menu.md`), so every option holds still for a whole round.
Delete what assumes otherwise:

- An `on_change` that tears down live round state.
- Re-reading and re-combining options every frame "so a menu change lands
  immediately", and re-planning or re-baselining for a value that changed or a
  toggle that came back on.
- A per-frame enabled check at the top of a proc that could just not be
  created - gate the creation at `On3DLoadEnd`. A plain option global read
  every frame inside a computation is already the simplest form and is not a
  finding.
- "Applies next round" or "applies immediately" wording in comments, docs, and
  the descriptions players see.

What can change mid-round, and keeps its handling:

- The first client connect to a new save (`SettingsMenu_SeedFromSlotOptions`,
  once per save) can switch the link and auto modes off or on.
- Archipelago deliveries open gates through the mod APIs.
- A City Trial event swaps the sky preset and restores it
  (`Sky_TransitionGlobal`/`Sky_RestoreGlobal`), so weather re-schedules on a
  preset change.
- State that survives into the menus, like the textbox stack, legitimately
  reacts in `on_change`.

Confirm which of these applies before deleting code that handles a change.

## Group B - Interfaces

### B1. API surface

- Mods boot in FST (alphabetical) order, so `Hoshi_ImportMod` in the
  importer's `OnBoot` resolves only when the exporter sorts earlier. Import in
  `OnSaveLoaded` or later - the first point past every mod's `OnBoot`.
- Every import must handle a NULL return, and the mod must degrade rather than
  crash when an optional dependency is absent.
- `<mod>_API_MAJOR` bumps on any breaking change to the exported struct;
  `_MINOR` on additions. A struct change with no bump is a finding.
- Public headers under `mods/<mod>/include/` carry only what other mods need.
  Anything with no external caller moves into `src/`.
- A public `include/` dir must be in the Makefile's explicit `INCLUDES` list.

### B2. Client protocol

Only for mods whose memory the Python client reads or writes. `APData` and
`APSlotOptions` in `mods/archipelago/src/main.h` are a wire contract with
the apworld in `../KARchipelago` (`docs/client-game-protocol.md`):

- The client restates every `APData` offset by hand. A new, resized or
  reordered field moves its offset table too.
- `APItemId` in `archipelago_api.h`, the checklist reward-index ordering, and
  `docs/checklist-mappings.csv` must agree with the apworld's IDs.
- Everything the client writes is untrusted: every index, id, count and length
  is range-checked before use, and a bad value is rejected and logged once
  rather than indexing past a table.
- Mailboxes keep their handshake: the writer fills the payload before setting
  the flag that publishes it, and the reader consumes the payload before
  clearing the flag.
- 64-bit fields are not atomic on PPC32. A new one needs either a
  read-and-diff design that tolerates a torn read, or a validity flag.
- Client-side `slot_data.get(key, default)` fallbacks for keys the current
  apworld always writes are backwards compatibility (C1).
- Report protocol findings; do not apply them without the paired apworld
  change.

### B3. RE bookkeeping

Any address the mod newly depends on is recorded, not just used, per CLAUDE.md's
Reverse Engineering Workflow: named with `kar.py rename`, prototyped in the
right hoshi header with a `// 0xADDR` comment, pushed with
`scripts/ghidra/sync.py`, and `kar.py check` clean.

An address written where a name already works is a finding:

- A game function called through a cast address instead of its `link.ld`
  symbol. If `kar.py sym 0xADDR` shows it still `zz_`, rename and prototype it
  rather than casting.
- `CODEPATCH_REPLACEFUNC` targets a function entry, so it takes the name,
  never the hex.
- A global reached by casting an address in mod code instead of the
  `static TYPE *name = (TYPE*)0xADDR;` declaration in a hoshi header.

A literal address is correct only where the site has no symbol of its own: the
mid-function `CODEPATCH_REPLACECALL` and `REPLACEINSTRUCTION` targets, hook exit
addresses, and `CODEPATCH_HOOKCREATE`/`HOOKAPPLY`, whose macro pastes the
address into an asm label and so cannot take a name.

A raw hex offset into a game struct is the same kind of finding. Name the member
in the hoshi struct and access it by name; when the struct is only partly
mapped, extend it with padding up to the field rather than casting a `u8 *`.
When the base is not a known struct at all, the offset gets a named `#define`
and a comment naming the containing function and its address. Offsets that stay
literal are the ones the surrounding code proves are an array stride or index.

A struct, enum, or global that describes the *game* belongs in
`externals/hoshi/include/`, even if only one mod reads it today. What stays in
the mod is what describes the mod: its save struct, its options, its own
registries. A game type declared mod-locally, or a second mod redeclaring one
hoshi already has, is a finding.

## Group C - Code shape

### C1. Stale, vestigial, and backwards-compatible code

- Functions, statics, enum members, `#define`s and struct fields with no
  reader. Check the whole package - a public API member may only be read by
  another mod. The lint finds the unreferenced functions, statics, defines and
  wholly unread enums; struct fields and single enum members need a grep. An
  enum member that holds its siblings' values in place, or mirrors a wire ID,
  stays.
- Code behind a condition that can no longer be true, and guards for a state
  the caller already established.
- `_Static_assert`s that pin a struct's size or member offsets, wire formats
  included. Asserts that a count fits its storage or that a table covers an
  enum stay.
- Comments describing behavior the code no longer has. These are findings even
  when the code is right.
- Backwards compatibility of any kind - there is none by policy, in the mods or
  the apworld; a player starts a new save for every seed and every version.
  Save migration, version-mismatch branches, code that reads a field two ways,
  "kept for older clients" fields, and defaults whose only job is to make an
  absent field behave like the previous release all go.

### C2. Structure and ownership

- One responsibility per file. A `gate_*.c` owns one gate; a helper used by two
  files belongs in neither and moves to the mod's shared header or `main.c`.
- The same logic written twice inside the mod is a finding as much as across
  mods. Two mods must not implement the same behavior. The generic mods
  (`custom_items`, `custom_machines`, `custom_checklist`, `custom_events`,
  `textbox`, `custom_weather`, `custom_ai`) stay AP-agnostic - anything
  Archipelago-aware that has drifted into them is a finding.
- Helper functions justify themselves by removing duplication or naming a
  non-obvious condition. A one-call-site wrapper that just renames a field
  access does not.
- YAGNI: registration hooks, callback slots and descriptor fields with exactly
  one user and no second caller in sight.
- CLAUDE.md's Code Style applies as written - Allman braces, include guards,
  the `Gm_`/`Ply_`/`Rider_`/`Machine_` hierarchy plus the file's subsystem
  prefix, `GXColor` for colors. A function emitting a varying alpha takes
  `GXColor rgb, u8 a`, and replacing a color's RGB while keeping its alpha is
  `c = (GXColor){src.r, src.g, src.b, c.a}`.

### C3. Reinvented helpers and hand-typed values

Code the mod writes by hand that a header already names: a hoshi helper, a game
function, a C library call, an enum count or a constant. Grep
`externals/hoshi/include/inline.h`, the game headers in
`externals/hoshi/include/`, and other mods' `include/*_api.h` for what the
hand-written code does, and read the provider before calling it a duplicate.
The usual categories, with the provider in brackets:

- RNG: a private PRNG or hash, a collect-then-pick over a bitmask
  (`RandomBitInField`), a weighted-roll loop (`Gm_Roll`), a Fisher-Yates loop
  (`RandomShuffle`), a `base + HSD_Randi(range)` expression (`RandomInRange`).
- Bits: set-bit counting (`Popcount64`), mask printing (`MaskBits`), a
  lowest-set-bit loop (`__builtin_ctz`).
- Math: a lerp written out (`lerp`), squared distance or magnitude
  (`VECSquareDistance`, `VECSquareMag`), matrix copy, cross-normalize, degree
  conversion (`MTXDegToRad`).
- Memory and strings: a hand-written zero, fill, copy or format loop (`memset`,
  `memcpy`, `strlen`), and `sizeof(a) / sizeof(a[0])` (`GetElementsIn`).
- Engine objects: JObj tree walking and indexing, GObj creation, item spawns,
  GX debug drawing, and a raw read of what a getter already returns
  (`stc_playerdata[i].rider_gobj` for `Ply_GetRiderGObj(i)`,
  `gd->is_replay` for `Gm_IsReplay()`).
- Constants: a literal `5` for `PLY_NUM`, a literal p_link for a `GAMEPLINK_*`,
  a pad mask for its `PAD_*` names, a mask or count an enum's `_NUM` already
  gives (`0x7F` for `(1 << TOPRIDE_NUM) - 1`), a hand-typed float for a
  `<math.h>` or `<float.h>` name (`1.5708f` for `M_PI_2`, `1e30f` for
  `FLT_MAX`).
- The mod's own helper written out again elsewhere in the mod.
- A one-line wrapper that only forwards to another function with the same
  signature - hook or call the target directly, or name the target in
  `mod_desc`.

Check a hand-typed float by its bit pattern, not by eye. Where a comment says a
literal matches a vanilla constant, read the constant from the game image
(`kar.py read 0xADDR 4 -f words`) and compare: a rounded `0.0174533f` sits four
ulps off the game's `0x3c8efa35`, which is exactly `MTXDegToRad`'s factor, so
the named form is the one that matches.

The swap must be exact, or the difference must not matter at that site. Check
0 and sentinel handling (a hash can return 0 - keep a guard where 0 means empty
or not-found), signedness, clamping, whether the replacement draws from the RNG
a different number of times, and masks with bits above the range the original
loop walked. A changed hash or ordering that a save keys on needs the stamp
bumped (A6). Where the provider differs - `lerp` clamps t, `Gm_Roll` still
draws on a zero total, `FLT_MAX` and `INFINITY` compare differently against an
empty slot - find out whether the original relied on the difference. An input
range the original assumed is often wrong (a stat ratio taken as [0,1] that is
really [-1,1]); that is a bug the swap exposes, so fix it and report it rather
than preserving it.

Prefer the game RNG to a private one: `main` seeds `HSD_Randi` from `OSGetTick`
at boot, and vanilla uses it for the same jobs. Work the frame loop already
does every frame (`docs/frame-loop.md`), such as invalidating the GX caches, is
the same kind of finding.

Mods link against the game's own C library through `link.ld`, not newlib, and
`-ffreestanding` keeps every call a call. `memcpy`, `memset`, `memcmp`,
`strlen`, `strcmp`, `strncpy`, `strchr`, `sprintf`, `sqrtf`, `sinf` and `cosf`
resolve; `fabsf`, `floorf`, `fmodf`, `snprintf`, `strlcpy`, `toupper`, `atoi`
and `__builtin_popcount` do not, and the pack step fails on them - use `_fabs`
and `Popcount64`. Never include `<ctype.h>`: its macros read a table that is not
linked. `M_PI`, `M_PI_2` and `M_SQRT3` come through `os.h`; `FLT_MAX` and
`INT_MAX` need `<float.h>` and `<limits.h>`.

Hoshi has no clamp/min/max, no 2D vector helpers, and no sort - local versions
of those are not findings. It does have `RandomShuffle`, `smoothstep`, `lerp`,
`JObj_ForEachJoint`, and the immediate-mode GX pieces `GX_BeginXlu`,
`GX_BillboardVert`, `COBJ_GetViewAxes` and `COBJ_GetViewEye`, so a local copy of
any of those is a finding. The same helper written in two or more mods with no
hoshi home is a finding outside the mod: report it as a candidate for
`inline.h` or the relevant hoshi header.

## Group D - Text and assets

### D1. Comments and OSReport

Enforce CLAUDE.md's Comments and Debug Output sections literally, and reduce
hard. The lint catches the mechanical failures (non-ASCII, banners, file
pointers, narration keywords, OSReport prefixes and their uniqueness, trailing
periods, hex masks). Read for the rest:

- Comments that restate the code, and RE narration the lint's keywords miss.
- Prose that outgrew a few lines. Move the substance into the system's
  `docs/*.md` and leave the one grounded fact (function name + address) in the
  code.
- Struct-member comments running past one short line.
- OSReport on a per-frame or per-tick path, on a retried path without a latch,
  once per player instead of once with a count, or more than one "Hooks
  installed" line per component.

What earns its place in a comment: the address and name of the function a
patch sits in, which register holds what in a prologue, and any engine
constraint the code shape depends on that the code cannot show.

### D2. Docs

For every system the pass changed, update its `docs/*.md` to CLAUDE.md's Docs
rules. A system with no doc gets one. Write for a human reader: explain the
mechanism and why it is shaped that way, not a line-by-line transcription of the
source. Engine facts a finding relied on that no doc records go into the doc
for that engine system, so the next review reads them instead of re-deriving
them.

### D3. Assets

- Nothing vanilla ships verbatim. `art/` holds the human-authored source and a
  script in `scripts/authoring/` writes the `.dat` into `mods/<mod>/assets/`.
- A staged asset with no authoring script, an authoring script whose output
  path no longer exists, and an `art/` source no script reads are findings.
- `mods/<mod>/assets/` is the disc staging folder - anything the game does not
  load does not belong there.
- `.gitignore` keeps generated assets out of the repo.

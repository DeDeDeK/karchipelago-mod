---
name: kar-review
description: >
    Use the kar-review skill to run a full code review of one mod under
    `mods/` and apply every fix it finds. Activate when the user asks to:
    - Code review a mod, or "review our recent additions" to one
    - Clean up comments, stale/vestigial code, or duplication in a mod
    - Check asm patch correctness or the API surface between mods
    - Review the mod side of the client protocol for drift against the apworld
    Reviews the whole named mod's source, not a diff. Applies behavior fixes,
    structural cleanups, comment cleanups and doc updates directly, then
    verifies with a build. Use `/code-review` instead for a generic
    diff-scoped bug hunt.
---

# kar-review Skill

A whole-mod review pass over `mods/<mod>/`, applying what it finds.

## Invocation

`/kar-review <mod>` where `<mod>` is a folder name under `mods/`. If the user
names no mod, ask which one - do not guess from the working tree. More than one
mod named means one full pass per mod, in the order given.

The review covers the mod's entire `src/` and `include/`, not just changed
lines. Findings outside the named mod (in `externals/hoshi/`, another mod, or
the apworld at `/home/dylan/code/KARchipelago`) get reported, not applied,
unless the user asked for those too.

## Step 1 - Load context

Before auditing anything:

- Read every file in `mods/<mod>/src/` and `mods/<mod>/include/`.
- Read the `docs/*.md` covering the systems the mod touches. Find them by
  filename; a mod's systems usually map one-to-one onto doc names
  (`gate_boxes.c` -> `docs/gate-boxes.md`, `deathlink.c` -> `docs/deathlink.md`).
- `git log --oneline -- mods/<mod>` for what the recent intent was.

Read the docs first. Most of what looks like a bug is a documented engine
constraint, and most stale comments are stale because the doc moved on.

## Step 2 - Audit

Fan out read-only agents, one per axis group below, each returning findings as
`file:line - what is wrong - what to do`. Agents must not edit; applying is
serial in step 3 so two fixes cannot collide in one file.

Every finding needs evidence. A claim about a game address is checked with
`uv run python scripts/kar.py disasm`/`sym`/`decomp`, not inferred from a
comment - the comment is what is under review.

## Step 3 - Apply

Apply every finding, in this order, so later passes see the final code:

1. Behavior fixes (correctness, asm patches, lifetime, protocol).
2. Structural changes (ownership moves, deduplication, deletions).
3. Comment and OSReport cleanup.
4. Doc updates in `docs/` for anything the code changes invalidated.

Deleting is the default remedy for anything stale, vestigial, or unused - there
is no deprecation path here and no backwards compatibility to keep.

Stop and ask before: changing `APData`/`APSlotOptions`/`APItemId` (it needs a
paired apworld change), or moving functionality between mods (it changes an API
surface). Report those and keep going with the rest.

## Step 4 - Verify

```bash
make package INCLUDE_MODS=<mod plus every mod it imports>
uv run python scripts/kar.py check
```

Import edges (`Hoshi_ImportMod` callers): `ap_star` -> custom_items,
custom_machines. `archipelago` -> ap_star, custom_checklist, custom_items,
custom_machines, textbox. `archipelago_debug` -> archipelago (and its deps),
custom_events, custom_machines. `hypernova` -> custom_items. The rest import
nothing.

A successful `make package` is sufficient - do not grep its output. `kar.py
check` must be as clean as it was before the pass.

## Step 5 - Report

One list of what changed, grouped by axis, each entry one line with a
`file:line`. Then, separately: findings deferred to the user, and findings
outside the mod.

---

# Review axes

## 1. Backwards compatibility

There is none, by policy. Delete on sight:

- Save-data migration, version-mismatch branches, or code that reads a field
  two ways.
- "Kept for older clients / older seeds / old apworld" fields and comments.
- Sentinels or defaults whose only job is to make an absent field behave like
  the previous release.

The one thing that is not backwards compatibility: `ModDesc.version.major` must
be bumped whenever the mod's save struct changes. That exists so hoshi
*discards* the stale memory-card block instead of reinterpreting it. A save
struct change with no major bump is a bug.

## 2. Stale, vestigial, and incorrect code

- Functions, statics, enum members, `#define`s and struct fields with no
  reader. Check the whole package, not just the mod - a public API header
  member may only be read by another mod.
- Code behind a condition that can no longer be true, and guards for a state
  the caller already established.
- Comments describing behavior the code no longer has. These are findings even
  when the code is right.
- Fields in `APSave` derivable from another field. A mask plus a sticky bool
  summarizing the same mask is one field too many.
- Assets under `mods/<mod>/assets/` that nothing on disc loads, and `art/`
  sources with no authoring script left in `scripts/authoring/`.

## 3. Comments

Enforce the comment rules in `.claude/CLAUDE.md` literally, and reduce hard.
The recurring failures here:

- Restating the code. Delete.
- RE narration - how the address was found, what was tried first, "verified
  live", "used to be". Delete.
- Prose that outgrew a few lines. Move the substance into the system's
  `docs/*.md` and leave the one grounded fact (function name + address) in the
  code.
- Pointers at other files - `see docs/foo.md`, a script path, another source
  file. Delete the pointer and state the fact inline.
- Non-ASCII: arrows, dashes, ellipses. Rewrite as `->`, `-`, `...`.
- Struct-member comments running past one short line.

What earns its place: the address and name of the function a patch sits in,
which register holds what in a prologue, and any engine constraint the code
shape depends on that the code cannot show.

## 4. Structure and ownership

- One responsibility per file. A `gate_*.c` owns one gate; a helper used by two
  gates belongs in neither and moves to the mod's shared header or `main.c`.
- Two mods must not implement the same behavior. The generic mods
  (`custom_items`, `custom_machines`, `custom_checklist`, `custom_events`,
  `textbox`, `custom_weather`) stay AP-agnostic - anything Archipelago-aware
  that has drifted into them is a finding.
- Helper functions justify themselves by removing duplication or naming a
  non-obvious condition. A one-call-site wrapper that just renames a field
  access does not.
- Naming follows the object hierarchy (`Gm_`/`Ply_`/`Rider_`/`Machine_`), and a
  mod's own functions carry the file's subsystem prefix.
- YAGNI: registration hooks, callback slots and descriptor fields with exactly
  one user and no second caller in sight.

## 5. Asm patches

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

Every patch address needs a named symbol in `GKYE01.map` and `link.ld`, and a
comment naming the containing function and its address.

## 6. API surface

- `Hoshi_ImportMod` must not be called from the importing mod's own `OnBoot` -
  load order follows FST order, so the export may not exist yet. Deferring to
  the first `OnSceneChange` or a later lifecycle hook is the pattern.
- Every import must handle a NULL return, and the mod must degrade rather than
  crash when an optional dependency is absent.
- `<mod>_API_MAJOR` bumps on any breaking change to the exported struct;
  `_MINOR` on additions. A struct change with no bump is a finding.
- Public headers under `mods/<mod>/include/` carry only what other mods need.
  Anything with no external caller moves into `src/`.
- New public `include/` dirs must be added to the Makefile's `INCLUDES` list -
  that list is explicit, unlike source discovery.

## 7. Client protocol parity

Only for mods whose data the Python client reads. `APData` and `APSlotOptions`
in `mods/archipelago/src/main.h` are a wire contract with
`/home/dylan/code/KARchipelago`:

- Every offset the client reads is pinned by an
  `_Static_assert(offsetof(APData, ...))` in `main.c`. A new or reordered field
  invalidates the asserts and the client's offset table together.
- `APItemId` in `archipelago_api.h`, the checklist reward-index ordering, and
  `docs/checklist-mappings.csv` must agree with the apworld's IDs.
- 64-bit fields are not atomic on PPC32. A new one needs either a
  read-and-diff design that tolerates a torn read, or a validity flag.
- Report protocol findings; do not apply them without the paired apworld
  change.

## 8. Lifetime and allocation

- `HSD_MemAlloc` in `OnBoot` persists for the whole run. Anywhere else it is
  freed at the next scene change. Allocate persistently only when the data
  genuinely outlives a scene - a per-round buffer allocated at boot is a
  finding.
- Handles resolved per scene - custom `ItemKind`s, machine ids, registry
  lookups - are valid for that scene only and must be re-fetched at
  `On3DLoadEnd` every round.
- State that should reset between rounds but lives in a file-scope static, and
  state that should persist but lives in a scene-lifetime allocation.

## 9. Player scope and the auto demo

Three shipped bugs came from this axis, so check it explicitly:

- The title screen's attract demo runs a real City Trial round inside
  `MJRKIND_TITLE`, with a CPU in every slot. Anything that claims a location,
  grants an item, or advances save state needs `Gm_IsAutoDemo()` in its gate.
- Per-player versus global: a check that should fire once per round must not be
  evaluated per player, and a per-player grant must not read player 0's state.
- Human versus CPU: gates that read controller slots must not treat a CPU as a
  human, and behavior meant for every rider must not skip CPUs.

## 10. Save and settings surface

- `OptionDesc` names are free to change. They are hashed by name, so a rename
  drops the saved value and the option comes back at its default - that is the
  same discarded-old-save behavior a major bump gives, and is not a reason to
  keep a bad name.
- `OPTKIND_VALUE` options that change gameplay need an `on_change` callback
  that logs the new value.
- Option descriptions must describe what the option actually does now.
- Two options whose combined states include a meaningless or unreachable
  combination are one option too many.

## 11. OSReport

- A `[Component]` prefix on every call, unique across the whole package.
- No per-frame or per-tick logging. A message on a path the engine retries must
  latch so it prints once.
- Per-player work reports one line with a count.
- Bitmasks print through `MaskBits(val, bits)` from `inline.h`, in binary, not
  hex. The buffer holds 32 bits.
- One consolidated "Hooks installed" line per component at boot.
- No trailing periods, no ellipses, past tense for things that happened.

## 12. RE bookkeeping

Any address the mod newly depends on must be recorded, not just used:

- Named in `GKYE01.map` and `link.ld` via
  `uv run python scripts/kar.py rename 0xADDR Name`.
- Prototyped in the right `externals/hoshi/include/` header with a `// 0xADDR`
  comment; data globals declared as `static TYPE *name = (TYPE*)0xADDR;` rather
  than added to `link.ld`.
- Pushed to Ghidra with `uv run python scripts/ghidra/sync.py`.
- `uv run python scripts/kar.py check` clean.

A raw hex offset into a game struct is a finding of the same kind - an address
the mod depends on that nothing records. Name the member in the hoshi struct
and access it by name. When the containing struct is only partly mapped, extend
it with padding up to the field rather than casting a `u8 *` and indexing; when
the base is not a known struct at all, the offset gets a named `#define` and a
comment naming the containing function and its address. Offsets that stay
literal are the ones the surrounding code proves are an array stride or an
index, not a member.

Ownership: a struct, enum, or global that describes the *game* belongs in
`externals/hoshi/include/`, even if only one mod reads it today - hoshi is
where the next mod will look for it. What stays in the mod is what describes
the mod: its save struct, its options, its own registries. A game type declared
mod-locally, or a second mod redeclaring one hoshi already has, is a finding.

## 13. Docs

For every system the pass changed, update its `docs/*.md`:

- Current state only. No "how we got here", no RE process, no dated notes.
- Self-contained. No pointers to other docs; restate the fact instead.
- Grounded in code - function name plus address where relevant.
- Written for a human reader: explain the mechanism and why it is shaped that
  way, not a line-by-line transcription of the source.

## 14. Assets

- Nothing vanilla ships verbatim. `art/` holds the human-authored source and a
  script in `scripts/authoring/` writes the `.dat` into `mods/<mod>/assets/`.
- A shipped `.dat` with no authoring script, or an authoring script whose
  output path no longer exists, is a finding.
- `mods/<mod>/assets/` is the disc staging folder - anything the game does not
  load does not belong there.
- Confirm `.gitignore` still keeps generated assets out of the repo.

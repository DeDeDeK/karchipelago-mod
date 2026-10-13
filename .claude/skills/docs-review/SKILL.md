---
name: docs-review
description: >
    Review and rewrite the reference docs under `docs/`, applying every fix -
    accuracy against the code and game image, human readability,
    self-containment, staleness, length, structure and folder layout. Use when
    the user asks to review, audit, clean up, reorganize or write docs, or after
    a change that invalidated a doc. Covers one doc, a folder, or all of
    `docs/`; `kar-review` covers a mod's code.
---

# docs-review Skill

The docs are a reference for human modders: how Kirby Air Ride's systems work,
and how each mod builds on them. A reader opens one doc to understand one
system or feature, so every doc has to be correct, stand on its own, and be
short enough to read. This skill is the standard they are held to and the way
to run a pass over them.

## Invocation

`/docs-review [<doc or folder> ...]`. With no argument the pass covers all of
`docs/`. A mod name (`archipelago`) means `docs/mods/<mod>/`.

## Layout

```
docs/
  game-systems/   vanilla engine: what the game does on its own
  hoshi/          the framework mods build on: loading, settings menu, save
  mods/<mod>/     one folder per mod, named exactly as under mods/
  data/           CSV tables: lookup data too bulky for a doc
```

- A doc is found by its path, so name it for the system or feature it covers:
  lowercase, hyphenated, no suffix or prefix the folder already says
  (`game-systems/sound.md`, not `sound-system.md`; `mods/ap_star/shot.md`, not
  `ap-star-shot.md`).
- `game-systems/` describes vanilla behavior. It may say where a mod can hook
  in, but what a particular mod does lives in that mod's folder.
- `mods/<mod>/` describes what the mod does and how. A one-doc mod still gets
  its folder.
- `data/` holds CSVs with a header row, named after the system they belong to.

## The rules

### 1. Accurate

Every claim has to be true of the code and game as they are now. The code wins
any disagreement - fix the doc, and report the code if the code looks wrong.

- Mod behavior: check it against the mod's source.
- Game facts: check names, addresses and layouts with `uv run python
  scripts/kar.py sym|disasm|decomp|read`, and hoshi's headers.
- A claim you cannot confirm is cut if it does not matter, and checked harder
  if it does. Never keep a guess as fact.

### 2. Written for a human

- Open with a short paragraph: what the system or feature is, and why a modder
  would care.
- Explain the mechanism and why it is shaped that way, in prose. Use tables for
  lookups (fields, kinds, functions), code blocks only for a struct, a layout
  or a short formula.
- Define a term before using it. Prefer the game's own names.

### 3. Self-contained

- No pointers to other docs: no "see `foo.md`", no section names, no "the X
  doc", no links. If a reader needs a fact from another system, state it in a
  sentence. Short overlap between docs is fine; a dependency is not.
- No pointers to data files either. A CSV is found by its name in `data/`.

### 4. Nothing that goes stale quietly

- **No repo file names or paths.** No `.c`/`.h`/`.py` names, hoshi header
  names, `mods/...` or `scripts/...` paths, line numbers, or "**File:**"
  lines. Name the function, struct or API instead. Game source names from
  assert strings (`plclearcheckerlib.c`) and disc files (`GrCity1.dat`) are
  game facts and fine.
- **Game facts are stable.** A vanilla function name with its address
  (`CityEvent_Decide` (0x800edcf8)) is the grounding convention, and struct
  offsets in game structs never move. Keep the ones a reader needs, not all of
  them.
- **Mod internals drift.** Name only what a reader would search for or what
  forms a contract: the public API, the hook sites and what each does, and the
  data that crosses a boundary (save layout, the client wire, descriptor
  structs). Leave out static helpers, local variables and internal call chains;
  describe what they do instead.
- **No counts or lists the code grows.** "Eleven hooks", "the current mods are
  A, B and C", every option a menu has - all wrong after the next change.
  Counts the game fixes (16 `EventKind`s) are fine.
- **Current state only.** No RE process, "verified live", dates, "used to",
  failed approaches, or changelogs. No "RE gap", "needs confirming" or TODO
  framing. A planned or unbuilt feature may be described as a plan.

### 5. Concise

Cover the idea, the implementation shape, and the key components and data
structures. Cut:

- Walkthroughs of mod code line by line or function by function.
- Patch detail beyond the site, what it does, and any register contract that
  is not obvious.
- Exhaustive function or address lists, and "Key addresses" sections that
  repeat the body.
- Logging details, debug-menu minutiae, and restatements.

Most docs should land between 50 and 200 lines. One over 300 needs a reason -
a reference table a modder truly uses - and is a candidate to split or to move
its table to `data/`.

### 6. Structured

One H1 (the system's name), the intro paragraph, then H2 sections in the
order a reader needs them. Typical shapes, to adapt rather than fill in:

- Game system: overview, data structures, flow or lifecycle, key functions
  (name, address, role), notes for modders (hook points, constraints).
- Mod feature: what it does, how it works, the engine seams it hooks, state
  (save, wire, API), settings and debug, limitations.

Skip a section with nothing to say. No "Files" sections.

### 7. Plain text

ASCII only - `->`, `-`, `...`, `>=`, never typographic glyphs. No banners or
decorative rules. The CLAUDE.md comment rules apply to docs too.

## Merging and splitting

- **Merge** two docs that describe the same system from two angles, a doc too
  small to stand alone (under ~40 lines) into the feature it belongs to, and
  docs a reader would always need together.
- **Split** a doc that covers two things a reader would look up separately -
  most often vanilla engine detail mixed with one mod's use of it (the engine
  part goes to `game-systems/`, the mod part to `mods/<mod>/`, each restating
  what it needs of the other) - and a doc over ~400 lines with separable
  parts.
- **Move to `data/`** a table of dozens of rows that a reader searches rather
  than reads (per-cell, per-course, per-kind tables).

## Process

1. **Baseline.** `git status --short docs` (dirty files are earlier work) and
   `uv run python scripts/docs_lint.py <paths>`. Lint hits are candidates, not
   verdicts: `stale-ident` also flags asset-archive names and `addr-name` can
   flag a deliberate mid-function site.
2. **Read** each doc in full, then the code it describes - the mod source, the
   hoshi headers, `kar.py` for game addresses. Read before judging: what looks
   wrong in a doc is sometimes a real engine quirk.
3. **Plan** merges, splits, renames and moves for the whole scope before
   rewriting, so content lands once.
4. **Rewrite** to the rules. When a doc is far off, rewrite it from what is
   true rather than patching sentence by sentence.
5. **Verify.** Rerun the lint; every remaining hit should be one you can
   justify. When docs moved or were renamed, update every reference to their
   paths in `.claude/CLAUDE.md` and `.claude/skills/*/SKILL.md`.
6. **Report** per doc: what happened (kept, rewritten, merged into, split
   into, moved, deleted) with line counts before and after, every factual error
   found and fixed, and anything left unverified.

## Running in parallel

Past ~10 docs, split the scope into groups of related docs and give each group
to one `general-purpose` agent (at most six), each owning a disjoint set of
files and destination names.

- Do every move and rename before starting them, with `git mv`, so each agent
  starts from its own paths.
- Agents use plain `mv` and `rm` and never touch the git index (`git mv`,
  `add`, `rm`, `stash`, `checkout`) - parallel index writes collide. The
  coordinator stages at the end.
- Agents edit only their own docs and never source code; a code bug they find
  goes in their report.
- The coordinator then reruns the lint over everything, checks the agents'
  file lists against the tree, and fixes the path references in `.claude/`.

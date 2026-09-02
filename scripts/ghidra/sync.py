#!/usr/bin/env python3
"""Push hoshi's header knowledge into the Ghidra kar.dol program.

Everything hoshi documents about the game - struct and enum layouts, the
function signatures annotated with `// 0xADDR`, and the globals pinned to a
literal address - is mirrored into Ghidra so decompiles read as typed C instead
of `undefined *` pointer math. The sync is one-way: the headers are the source
of truth, the Ghidra database is the copy.

    uv run python scripts/ghidra/sync.py              # all three phases, then save
    uv run python scripts/ghidra/sync.py --dry-run    # report only, touch nothing
    uv run python scripts/ghidra/sync.py protos       # just re-apply signatures

Phases, in dependency order:

    types    parse the headers into the DataTypeManager (structs/enums/typedefs)
    protos   set each documented function's signature by address
    globals  retype and label each fixed-address engine global in the listing

`protos` and `globals` apply the types that `types` loads, so a full run is the
norm after a header change; naming a subset just saves time when only one kind
of declaration moved. Every phase is safe to re-run.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from hoshi_headers import (
    extract_globals,
    extract_protos,
    iter_headers,
    strip_bodies,
)

REPO = os.path.dirname(os.path.dirname(HERE))
HOSHI_INCLUDE = os.path.join(REPO, "externals", "hoshi", "include")
MASTER_HEADER = os.path.join(HERE, "hoshi_all.h")
STUBS = os.path.join(HERE, "stubs")
JAVA_SCRIPT = os.path.join(HERE, "HoshiSync.java")

CFG_DIR = os.path.expanduser("~/.config/ghidra-cli")
BRIDGE_SCRIPTS = os.path.join(CFG_DIR, "scripts")
CFG_FILE = os.path.join(CFG_DIR, "hoshi_sync.cfg")
REPORT_FILE = os.path.join(CFG_DIR, "hoshi_sync_report.txt")
GLOBALS_TSV = os.path.join(CFG_DIR, "hoshi_globals.tsv")
BRIDGE_STATE = os.path.expanduser("~/.local/share/ghidra-cli")

PHASES = ("types", "protos", "globals")

# Return types that Ghidra propagates to every caller once set, which is worth
# far more than the one function they name.
RETURN_TYPE_OVERRIDES = {
    "gmGetGlobalP": "GameData *",
}

_BRIDGE_CWD = None


def ghidra(args, project):
    return subprocess.run(
        ["ghidra", *args, "--project", project, "--json"],
        capture_output=True,
        text=True,
    )


def bridge_cwd(project):
    """Working directory of the running bridge JVM, starting it if needed.

    The ghidra-cli client only accepts a script path that exists relative to the
    bridge's working directory, while the bridge itself resolves the bare
    filename against its own script path - so a script has to be visible in both
    places at once. Linux only: the working directory comes from /proc.
    """
    global _BRIDGE_CWD
    if _BRIDGE_CWD:
        return _BRIDGE_CWD
    subprocess.run(
        ["ghidra", "start", "--project", project], capture_output=True, text=True
    )
    pids = []
    if os.path.isdir(BRIDGE_STATE):
        for fn in sorted(os.listdir(BRIDGE_STATE)):
            if fn.startswith("bridge-") and fn.endswith(".pid"):
                try:
                    with open(os.path.join(BRIDGE_STATE, fn)) as f:
                        pids.append(int(f.read().strip()))
                except (OSError, ValueError):
                    pass
    if not pids:
        found = subprocess.run(
            ["pgrep", "-f", "GhidraCliBridge"], capture_output=True, text=True
        )
        pids = [int(p) for p in found.stdout.split()]
    for pid in pids:
        try:
            _BRIDGE_CWD = os.readlink(f"/proc/{pid}/cwd")
            return _BRIDGE_CWD
        except OSError:
            continue
    sys.exit("could not locate the running ghidra-cli bridge process")


def run_bridge_script(task, settings, project):
    """Run HoshiSync.java in the bridge JVM and return its report text.

    The bridge forwards no script arguments and returns no script stdout, so the
    two travel through files instead.
    """
    os.makedirs(BRIDGE_SCRIPTS, exist_ok=True)
    with open(CFG_FILE, "w") as f:
        f.write(f"task={task}\nreport={REPORT_FILE}\n")
        f.writelines(f"{key}={value}\n" for key, value in settings.items())
    if os.path.exists(REPORT_FILE):
        os.remove(REPORT_FILE)

    name = os.path.basename(JAVA_SCRIPT)
    resolved = os.path.join(BRIDGE_SCRIPTS, name)  # where the bridge looks
    visible = os.path.join(bridge_cwd(project), name)  # what the client checks
    shutil.copyfile(JAVA_SCRIPT, resolved)
    shutil.copyfile(JAVA_SCRIPT, visible)
    try:
        r = ghidra(["script", "run", name], project)
    finally:
        if visible != resolved and os.path.exists(visible):
            os.remove(visible)

    if not os.path.exists(REPORT_FILE):
        sys.exit(f"HoshiSync {task} wrote no report:\n{r.stderr or r.stdout}")
    with open(REPORT_FILE) as f:
        return f.read()


def print_report(text, problem_re=None, limit=25):
    """Print a HoshiSync report: the summary, then any flagged detail lines."""
    head, marker, detail = text.partition("\n--- ")
    print("\n".join(f"  {ln}" for ln in head.rstrip().splitlines()))
    if not marker:
        return
    hits = [
        ln.strip() for ln in detail.splitlines() if problem_re and problem_re.search(ln)
    ]
    for line in hits[:limit]:
        print(f"  {line}")
    if len(hits) > limit:
        print(f"  ... {len(hits) - limit} more")
    print(f"  full report: {REPORT_FILE}")


_PARSE_PROBLEM = re.compile(r"error|exception|unable|failed|cannot", re.IGNORECASE)


def phase_types(project, dry_run):
    """Parse the hoshi headers into the program's DataTypeManager.

    Ghidra's C parser aborts on function bodies, so the headers are mirrored
    with every body and file-scope initializer replaced by `;`, and the parser
    searches that mirror ahead of the originals. Empty stubs shadow the system
    includes and the inline/networking headers we skip; hoshi_all.h pulls in the
    rest in dependency order.
    """
    if dry_run:
        print(f"[types] would parse {MASTER_HEADER}")
        print(
            f"[types] over {len(list(iter_headers(HOSHI_INCLUDE)))} "
            f"body-stripped hoshi headers"
        )
        return
    stripped = tempfile.mkdtemp(prefix="hoshi-stripped-")
    try:
        count = strip_bodies(HOSHI_INCLUDE, stripped)
        print(f"[types] parsing {count} body-stripped headers")
        report = run_bridge_script(
            "parse",
            {
                "includes": f"{STUBS},{stripped}",
                "files": MASTER_HEADER,
            },
            project,
        )
        print_report(report, _PARSE_PROBLEM)
    finally:
        shutil.rmtree(stripped, ignore_errors=True)


def fix_syntax(sig):
    """Bend a hoshi prototype into what Ghidra's signature parser accepts."""
    sig = sig.replace("const ", "")
    # pointer return: bind the `*` to the return type by spacing it off the name
    sig = re.sub(r"\*([A-Za-z_]\w*)\s*\(", r"* \1(", sig, count=1)
    # array params decay to pointers; the parser rejects `[]`
    sig = re.sub(r"([A-Za-z_]\w*)\s*\[\s*\d*\s*\]", r"*\1", sig)
    return sig


def plan_protos(project):
    """([(addr, signature, note)], [(addr, name)]) - signatures to apply, and
    documented addresses with no function in Ghidra.

    Ghidra sometimes carries the better name, so an existing real name wins over
    hoshi's and only the types are taken from the header.
    """
    r = ghidra(
        ["function", "list", "--fields", "address,name", "--limit", "60000"], project
    )
    if r.returncode != 0:
        sys.exit(f"ghidra function list failed:\n{r.stderr or r.stdout}")
    known = {
        f["address"].lower().replace("0x", ""): f["name"] for f in json.loads(r.stdout)
    }

    plan, skipped = [], []
    for addr, name, sig, _loc in extract_protos(HOSHI_INCLUDE):
        gname = known.get(addr.replace("0x", ""))
        if gname is None:
            skipped.append((addr, name))
            continue
        if gname == name or gname.startswith(("FUN_", "undefined", "zz_")):
            note = "match" if gname == name else f"named ({gname} -> {name})"
        else:
            sig = re.sub(r"\b" + re.escape(name) + r"\s*\(", gname + "(", sig, count=1)
            note = f"kept ghidra name ({gname})"
        plan.append((addr, fix_syntax(sig), note))
    return plan, skipped


def phase_protos(project, dry_run):
    plan, skipped = plan_protos(project)
    print(
        f"[protos] {len(plan)} documented signatures, "
        f"{len(skipped)} addresses with no function in Ghidra"
    )
    if dry_run:
        for name, rtype in RETURN_TYPE_OVERRIDES.items():
            print(f"  [getter                ] {name} -> {rtype}")
        for addr, sig, note in plan:
            print(f"  [{note:22}] {addr}  {sig}")
        for addr, name in skipped:
            print(f"  [no function          ] {addr}  {name}")
        return

    ok, failures = 0, []
    for name, rtype in RETURN_TYPE_OVERRIDES.items():
        r = ghidra(["function", "set-return-type", name, "--type", rtype], project)
        if r.returncode == 0 and "return_type_set" in r.stdout:
            ok += 1
        else:
            failures.append((name, rtype, (r.stderr or r.stdout).strip()))
    for addr, sig, _note in plan:
        r = ghidra(["function", "set-signature", addr, "--signature", sig], project)
        if r.returncode == 0 and re.search(
            r'"status"\s*:\s*"(signature_set|updated)"', r.stdout
        ):
            ok += 1
        else:
            failures.append((addr, sig, (r.stderr or r.stdout).strip()))

    print(f"  applied={ok} failed={len(failures)}")
    for target, sig, err in failures:
        print(f"  FAIL {target}  {sig}\n       {err}")


def phase_globals(project, dry_run):
    """Retype and label the engine globals hoshi pins to literal addresses.

    Array extents are not encoded in the declarations, so an indexed global gets
    a single element unless its name is listed in ARRAY_SIZES.
    """
    rows = extract_globals(HOSHI_INCLUDE)
    print(f"[globals] {len(rows)} fixed-address declarations")
    if dry_run:
        for addr, base, stars, count, name in rows:
            decl = base + (" " + "*" * stars if stars else "")
            print(f"  {addr}  {name:30} {decl}{f'[{count}]' if count > 1 else ''}")
        return

    os.makedirs(CFG_DIR, exist_ok=True)
    with open(GLOBALS_TSV, "w") as f:
        f.writelines("\t".join(str(col) for col in row) + "\n" for row in rows)
    print_report(run_bridge_script("globals", {"data": GLOBALS_TSV}, project))


def persist(project, program):
    """Flush the in-memory program to disk and reopen it.

    The bridge never saves on its own, and `ghidra analyze` does not reliably
    write back freshly-created listing data; closing the program does.
    """
    ghidra(["program", "close", "--program", program], project)
    ghidra(["program", "open", "--program", program], project)
    print(f"[save] {program} written to disk")


def main(argv):
    p = argparse.ArgumentParser(
        prog="scripts/ghidra/sync.py",
        description="Push hoshi types, signatures and globals into Ghidra.",
    )
    p.add_argument(
        "phases", nargs="*", help=f"phases to run (default: {' '.join(PHASES)})"
    )
    p.add_argument("--project", default="kar-decomp")
    p.add_argument("--program", default="kar.dol")
    p.add_argument(
        "--dry-run",
        action="store_true",
        help="report what would be pushed without changing Ghidra",
    )
    args = p.parse_args(argv[1:])

    requested = args.phases or list(PHASES)
    unknown = [ph for ph in requested if ph not in PHASES]
    if unknown:
        p.error(
            f"unknown phase(s): {', '.join(unknown)}; choose from {', '.join(PHASES)}"
        )
    if not os.path.isdir(HOSHI_INCLUDE):
        sys.exit(f"missing {HOSHI_INCLUDE}")

    runners = {"types": phase_types, "protos": phase_protos, "globals": phase_globals}
    for phase in PHASES:
        if phase in requested:
            runners[phase](args.project, args.dry_run)
    if not args.dry_run:
        persist(args.project, args.program)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

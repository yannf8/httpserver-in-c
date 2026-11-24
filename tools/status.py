#!/usr/bin/env python3
"""The ladder: which sections pass, which fail, and where you are.

    make status              unit + end-to-end
    make status FAST=1       unit tests only (skips starting servers)
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SYLLABUS = os.path.join(ROOT, "tools", "syllabus.tsv")

MODULE_TITLES = {
    "0": "Setup and the syscall interface",
    "1": "File descriptors and I/O discipline",
    "2": "Sockets",
    "3": "Speaking HTTP/1.1",
    "4": "A blocking server, end to end",
    "5": "Concurrency: non-blocking I/O and epoll",
    "6": "Measuring and shaving latency",
    "7": "Hardening",
}


def load_syllabus():
    rows = []
    with open(SYLLABUS) as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 4:
                continue
            rows.append({"id": parts[0], "kind": parts[1], "title": parts[2], "doc": parts[3]})
    return rows


def collect(cmd):
    """Runs a --machine runner and returns {section: [statuses]}."""
    try:
        proc = subprocess.run(cmd, capture_output=True, cwd=ROOT, timeout=900)
    except subprocess.TimeoutExpired:
        print(f"warning: {' '.join(cmd)} timed out", file=sys.stderr)
        return {}, "timeout"
    results = {}
    for line in proc.stdout.decode("utf-8", "replace").splitlines():
        if not line.startswith("RESULT\t"):
            continue
        _, section, status, *rest = line.split("\t")
        results.setdefault(section, []).append(status)
    err = proc.stderr.decode("utf-8", "replace").strip()
    return results, err


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fast", action="store_true", help="unit tests only")
    args = ap.parse_args()

    rows = load_syllabus()

    unit, unit_err = collect([os.path.join(ROOT, "build", "unit_tests"), "--machine"])
    integ, integ_err = ({}, "")
    if not args.fast:
        integ, integ_err = collect([sys.executable, "tests/integration/run.py", "--machine"])

    results = {}
    for src in (unit, integ):
        for section, statuses in src.items():
            results.setdefault(section, []).extend(statuses)

    tty = sys.stdout.isatty()
    red = "\033[31m" if tty else ""
    green = "\033[32m" if tty else ""
    yellow = "\033[33m" if tty else ""
    dim = "\033[2m" if tty else ""
    bold = "\033[1m" if tty else ""
    rst = "\033[0m" if tty else ""

    you_are_here = None
    total_pass = total_tests = 0

    lines = []
    current_module = None
    for row in rows:
        section = row["id"]
        module = section.split(".")[0]
        if module != current_module:
            current_module = module
            lines.append(("module", module, MODULE_TITLES.get(module, "")))

        statuses = results.get(section, [])
        npass = sum(1 for s in statuses if s == "PASS")
        nskip = sum(1 for s in statuses if s == "SKIP")
        n = len(statuses)
        total_pass += npass
        total_tests += n

        if row["kind"] == "manual":
            state = "manual"
        elif n == 0:
            state = "notests"
        elif npass + nskip == n:
            state = "pass"
        elif npass == 0:
            state = "todo"
        else:
            state = "partial"

        if you_are_here is None and state in ("todo", "partial"):
            you_are_here = section

        lines.append(("section", row, (state, npass, n)))

    print(f"\n{bold}httpserver-in-c — where you are{rst}")
    if args.fast:
        print(f"{dim}(unit tests only; run `make status` for the end-to-end sections too){rst}")

    for kind, a, b in lines:
        if kind == "module":
            print(f"\n{bold}Module {a} — {b}{rst}")
            continue
        row, (state, npass, n) = a, b
        mark = {
            "pass": f"{green}✓{rst}",
            "partial": f"{yellow}◐{rst}",
            "todo": f"{red}✗{rst}",
            "manual": f"{dim}·{rst}",
            "notests": f"{dim}?{rst}",
        }[state]
        count = f"{npass}/{n}" if n else ("by hand" if state == "manual" else "-")
        here = f"   {bold}<- you are here{rst}" if row["id"] == you_are_here else ""
        print(f"  {mark} {row['id']:<5} {row['title']:<44} {dim}{count:>8}{rst}{here}")

    print(f"\n{green}{total_pass}{rst}/{total_tests} checks passing")
    if you_are_here:
        row = next(r for r in rows if r["id"] == you_are_here)
        print(f"Next: {bold}{you_are_here} {row['title']}{rst}")
        print(f"      theory  : {row['doc']}  (section {you_are_here})")
        if row["kind"] == "unit":
            print(f"      run     : make test SECTION={you_are_here}")
        else:
            print(f"      run     : make integration SECTION={you_are_here}")
        print(f"      hints   : docs/HINTS.md  (section {you_are_here})")
    else:
        print(f"{green}Every automated section is green.{rst} "
              "The manual sections in modules 0, 6 and 7 are where the interesting "
              "work is now — see docs/06-low-latency.md and docs/08-going-further.md.")

    for err in (unit_err, integ_err):
        if err and err != "timeout":
            print(f"\n{dim}runner stderr: {err[:400]}{rst}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

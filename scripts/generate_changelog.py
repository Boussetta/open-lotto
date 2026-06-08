#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Wissem Boussetta
# SPDX-License-Identifier: MIT

"""Generate markdown changelog sections from conventional commits."""

import argparse
import re
import subprocess
import sys
from collections import OrderedDict

TYPE_TITLES = OrderedDict(
    [
        ("feat", "Features"),
        ("fix", "Fixes"),
        ("perf", "Performance"),
        ("refactor", "Refactors"),
        ("docs", "Documentation"),
        ("test", "Tests"),
        ("build", "Build"),
        ("ci", "CI"),
        ("chore", "Chores"),
    ]
)

CC_RE = re.compile(r"^(?P<type>[a-z]+)(\([^)]*\))?(?P<breaking>!)?:\s+(?P<desc>.+)$")


def run_git(args):
    proc = subprocess.run(["git", *args], capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or "git command failed")
    return proc.stdout.strip()


def detect_from_ref(to_ref):
    try:
        return run_git(["describe", "--tags", "--abbrev=0", f"{to_ref}^"])
    except RuntimeError:
        return ""


def collect_commits(from_ref, to_ref):
    rev_range = f"{from_ref}..{to_ref}" if from_ref else to_ref
    output = run_git(["log", rev_range, "--pretty=format:%h\t%s"])
    if not output:
        return []
    commits = []
    for line in output.splitlines():
        if "\t" not in line:
            continue
        short, subject = line.split("\t", 1)
        commits.append((short, subject.strip()))
    return commits


def render(from_ref, to_ref, commits):
    sections = OrderedDict((k, []) for k in TYPE_TITLES.keys())
    sections["other"] = []

    for short, subject in commits:
        match = CC_RE.match(subject)
        if match:
            ctype = match.group("type")
            desc = match.group("desc")
            if match.group("breaking"):
                desc = f"{desc} (BREAKING)"
            sections.get(ctype, sections["other"]).append((desc, short))
        else:
            sections["other"].append((subject, short))

    lines = []
    if from_ref:
        lines.append(f"Generated from `{from_ref}..{to_ref}`")
    else:
        lines.append(f"Generated from `{to_ref}`")
    lines.append("")

    emitted = False
    for ctype, title in TYPE_TITLES.items():
        entries = sections[ctype]
        if not entries:
            continue
        emitted = True
        lines.append(f"### {title}")
        for desc, short in entries:
            lines.append(f"- {desc} ({short})")
        lines.append("")

    if sections["other"]:
        emitted = True
        lines.append("### Other")
        for desc, short in sections["other"]:
            lines.append(f"- {desc} ({short})")
        lines.append("")

    if not emitted:
        lines.append("- No commits found in the selected range.")

    return "\n".join(lines).rstrip() + "\n"


def main():
    parser = argparse.ArgumentParser(description="Generate changelog markdown from conventional commits")
    parser.add_argument("--from-ref", default="", help="Git ref to start from (exclusive)")
    parser.add_argument("--to-ref", default="HEAD", help="Git ref to end at (inclusive)")
    parser.add_argument("--output", default="", help="Output file path (default: stdout)")
    args = parser.parse_args()

    from_ref = args.from_ref or detect_from_ref(args.to_ref)
    commits = collect_commits(from_ref, args.to_ref)
    rendered = render(from_ref, args.to_ref, commits)

    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(rendered)
    else:
        sys.stdout.write(rendered)


if __name__ == "__main__":
    main()

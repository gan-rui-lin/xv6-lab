#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Remove manual numeric prefixes from Typst headings.
E.g. "==== 9.3.2 Title" -> "==== Title"

Rules:
- Match lines starting with one or more '=' (Typst heading levels), e.g. '=', '==', '===', ...
- After '=' and spaces, remove a numeric prefix like '1', '1.2', '9.3.2', optionally followed by punctuation: '.', ')', '、', ':'
- Preserve the original spaces between '=' and the title text.
"""

import argparse
import pathlib
import re
import sys
from typing import Match

# Heading pattern:
#   1) ^(=+)            -> leading '=' heading marks
#   2) ([ \t]+)        -> spaces after heading marks
#   3) (\d+(?:\.\d+)*) -> numeric prefix (e.g., 9.3.2)
#   4) (?:[.)、:])?     -> optional punctuation
#   5) ([ \t]+)        -> spaces after numeric prefix
#   6) (.*)             -> the rest of the title text
HEADING_NUM_RE = re.compile(r"^(=+)([ \t]+)(\d+(?:\.\d+)*)(?:[.)、:])?([ \t]+)(.*)$")

# Also handle cases where there is no extra spaces after numeric, e.g. '=== 1.Title'
HEADING_NUM_COMPACT_RE = re.compile(r"^(=+)([ \t]+)(\d+(?:\.\d+)*)(?:[.)、:])?(.*)$")


def strip_line(line: str) -> str:
    m: Match[str] | None = HEADING_NUM_RE.match(line)
    if m:
        eqs, pre_spaces, num, post_spaces, title = m.groups()
        return f"{eqs}{pre_spaces}{title}"
    m2: Match[str] | None = HEADING_NUM_COMPACT_RE.match(line)
    if m2:
        eqs, pre_spaces, num, rest = m2.groups()
        # If rest starts directly with text, keep one space boundary from original pre_spaces
        return f"{eqs}{pre_spaces}{rest.lstrip()}"
    return line


def convert_text(text: str) -> str:
    out_lines = [strip_line(l) for l in text.splitlines()]
    return "\n".join(out_lines)


def main() -> int:
    ap = argparse.ArgumentParser(description="Remove manual numeric prefixes from Typst headings (==== 9.3.2 Title -> ==== Title)")
    ap.add_argument("input", nargs="+", help="Input file path(s). Use '-' to read from stdin.")
    ap.add_argument("-o", "--output", help="Output file path (only valid with single input). Default: stdout unless --in-place")
    ap.add_argument("-i", "--in-place", action="store_true", help="Overwrite input file(s) in place")
    args = ap.parse_args()

    if args.output and (args.in_place or len(args.input) != 1):
        print("--output is only allowed with exactly one input and without --in-place", file=sys.stderr)
        return 2

    if args.input == ["-"]:
        if args.in_place:
            print("--in-place is not allowed with stdin", file=sys.stderr)
            return 2
        data = sys.stdin.read()
        result = convert_text(data)
        if args.output:
            pathlib.Path(args.output).write_text(result, encoding="utf-8")
        else:
            sys.stdout.write(result)
        return 0

    for idx, inp in enumerate(args.input):
        p = pathlib.Path(inp)
        data = p.read_text(encoding="utf-8")
        result = convert_text(data)
        if args.in_place:
            p.write_text(result, encoding="utf-8")
        elif args.output:
            pathlib.Path(args.output).write_text(result, encoding="utf-8")
        else:
            # When multiple inputs and no --in-place/--output, print with file separators
            if len(args.input) > 1:
                sys.stdout.write(f"\n===== {p} =====\n")
            sys.stdout.write(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

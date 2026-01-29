#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
import pathlib
import re
import sys

PATTERN = re.compile(r"^(#{1,6})([ \t]*)", re.MULTILINE)

def convert_text(text: str) -> str:
    def repl(m: re.Match) -> str:
        hashes = m.group(1)
        spaces = m.group(2)
        return "=" * len(hashes) + spaces
    return PATTERN.sub(repl, text)


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Convert Markdown headings (#... Title) to Typst (=... Title) while preserving spaces before title text.")
    ap.add_argument("input", help="Input file path, or '-' for stdin")
    ap.add_argument("-o", "--output", help="Output file path (default: stdout unless --in-place)")
    ap.add_argument("-i", "--in-place", action="store_true", help="Overwrite input file in place")
    args = ap.parse_args()

    if args.input == "-":
        if args.in_place:
            print("--in-place is not allowed with stdin", file=sys.stderr)
            return 2
        data = sys.stdin.read()
        out = convert_text(data)
        if args.output:
            pathlib.Path(args.output).write_text(out, encoding="utf-8")
        else:
            sys.stdout.write(out)
        return 0

    in_path = pathlib.Path(args.input)
    data = in_path.read_text(encoding="utf-8")
    out = convert_text(data)

    if args.in_place:
        in_path.write_text(out, encoding="utf-8")
    elif args.output:
        pathlib.Path(args.output).write_text(out, encoding="utf-8")
    else:
        sys.stdout.write(out)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

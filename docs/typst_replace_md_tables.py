#!/usr/bin/env python3
"""
Replace Markdown pipe tables embedded in Typst files with Typst `#figure(table(...))`.

Heuristics:
- Detect a Markdown table as:
  - a row containing '|' (starts with optional whitespace + '|')
  - followed by a separator row like |---|---|
- Replace the contiguous block of table rows.
- Skip replacements inside fenced code blocks (```).
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import List, Tuple

import md_table_to_typst as m2t


def _starts_fence(line: str) -> bool:
    return line.lstrip().startswith("```")


def _is_row(line: str) -> bool:
    s = line.lstrip()
    return s.startswith("|") and "|" in s[1:]


def convert_file(path: Path) -> Tuple[bool, str]:
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines(True)

    out: List[str] = []
    changed = False
    in_fence = False
    i = 0
    while i < len(lines):
        line = lines[i]
        if _starts_fence(line):
            in_fence = not in_fence
            out.append(line)
            i += 1
            continue

        if (not in_fence) and _is_row(line):
            if i + 1 < len(lines) and m2t.is_separator_line(lines[i + 1]):
                indent = line[: len(line) - len(line.lstrip())]
                j = i
                block: List[str] = []
                while j < len(lines) and _is_row(lines[j]):
                    block.append(lines[j])
                    j += 1

                # Convert this single table block.
                table = m2t.parse_md_table(block)
                rendered = m2t.render_typst_table(
                    table,
                    wrap_figure=True,
                    align="center",
                    inset="10pt",
                    row_gutter="auto",
                    col_spec=None,
                    title=None,
                )
                rendered_lines = [(indent + l if l.strip() else l) for l in rendered.splitlines(True)]
                out.extend(rendered_lines)
                changed = True
                i = j
                continue

        out.append(line)
        i += 1

    new_text = "".join(out)
    return changed, new_text


def main(argv: List[str]) -> int:
    p = argparse.ArgumentParser(description="Replace Markdown tables in Typst files with Typst tables.")
    p.add_argument("paths", nargs="+", help="Typst files to process.")
    args = p.parse_args(argv)

    any_changed = False
    for pth in args.paths:
        path = Path(pth)
        changed, new_text = convert_file(path)
        if changed:
            path.write_text(new_text, encoding="utf-8")
            any_changed = True
            print(f"updated: {path}")
        else:
            print(f"no change: {path}")

    return 0 if any_changed else 0


if __name__ == "__main__":
    raise SystemExit(main(__import__("sys").argv[1:]))


#!/usr/bin/env python3
"""
Convert a Markdown pipe table into a Typst `table(...)` (optionally wrapped in `#figure(...)`).

Example:
  python3 docs/md_table_to_typst.py < table.md
  python3 docs/md_table_to_typst.py docs/文件.md --section "实现程度对照表"
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Optional


SEPARATOR_RE = re.compile(r"^\s*\|?\s*:?-{3,}:?\s*(\|\s*:?-{3,}:?\s*)+\|?\s*$")


def split_md_row(line: str) -> List[str]:
    """
    Split a markdown table row by '|' while allowing escaped '\\|'.
    Keeps content as-is (minus outer pipes/whitespace trimming per cell).
    """
    line = line.strip()
    if line.startswith("|"):
        line = line[1:]
    if line.endswith("|"):
        line = line[:-1]

    cells: List[str] = []
    current: List[str] = []
    escape = False
    for ch in line:
        if escape:
            current.append(ch)
            escape = False
            continue
        if ch == "\\":
            escape = True
            current.append(ch)
            continue
        if ch == "|":
            cells.append("".join(current).strip())
            current = []
        else:
            current.append(ch)
    cells.append("".join(current).strip())
    return cells


def is_separator_line(line: str) -> bool:
    return bool(SEPARATOR_RE.match(line.rstrip("\n")))


def typst_escape_cell(text: str) -> str:
    # Keep it as plain text inside Typst content blocks: [ ... ].
    # Escape brackets and backslashes to avoid accidentally closing/opening blocks.
    return (
        text.replace("\\", "\\\\")
        .replace("[", "\\[")
        .replace("]", "\\]")
    )


@dataclass
class MdTable:
    header: List[str]
    rows: List[List[str]]

    @property
    def ncols(self) -> int:
        return len(self.header)


def parse_md_table(lines: Iterable[str]) -> MdTable:
    # Find first non-empty table header row containing '|'
    it = iter(lines)
    for line in it:
        if "|" not in line:
            continue
        if line.strip() == "":
            continue
        header = split_md_row(line)
        # Next non-empty line should be separator
        for sep in it:
            if sep.strip() == "":
                continue
            if not is_separator_line(sep):
                raise ValueError("Markdown table separator line not found after header.")
            break

        rows: List[List[str]] = []
        for rowline in it:
            if rowline.strip() == "":
                break
            if "|" not in rowline:
                break
            row = split_md_row(rowline)
            rows.append(row)
        # Normalize to header width
        n = len(header)
        norm_rows: List[List[str]] = []
        for r in rows:
            if len(r) < n:
                norm_rows.append(r + [""] * (n - len(r)))
            else:
                norm_rows.append(r[:n])
        return MdTable(header=header, rows=norm_rows)
    raise ValueError("No Markdown pipe table found in input.")


def render_typst_table(
    table: MdTable,
    *,
    wrap_figure: bool,
    align: str,
    inset: str,
    row_gutter: str,
    col_spec: Optional[str],
    title: Optional[str],
) -> str:
    if col_spec is None:
        columns = ", ".join(["auto"] * table.ncols)
        col_spec = f"({columns})"

    items: List[str] = []
    for cell in table.header:
        items.append(f'    [{typst_escape_cell(cell)}],')
    for row in table.rows:
        for cell in row:
            items.append(f'    [{typst_escape_cell(cell)}],')

    lines: List[str] = []
    if wrap_figure:
        lines.append("#figure(")
        if title:
            lines.append(f'  caption: [{typst_escape_cell(title)}],')
        lines.append("  table(")
    else:
        lines.append("#table(")

    lines.append(f"    align: {align},")
    lines.append(f"    columns: {col_spec},")
    lines.append(f"    row-gutter: {row_gutter},")
    lines.append(f"    inset: {inset},")
    lines.extend(items)

    if wrap_figure:
        lines.append("  ),")
        lines.append(")")
    else:
        lines.append(")")
    return "\n".join(lines) + "\n"


def main(argv: List[str]) -> int:
    p = argparse.ArgumentParser(description="Convert Markdown pipe table to Typst table().")
    p.add_argument("path", nargs="?", help="Markdown file path (defaults to stdin).")
    p.add_argument("--no-figure", action="store_true", help="Do not wrap in #figure(...).")
    p.add_argument("--align", default="center", help="Typst table align (default: center).")
    p.add_argument("--inset", default="10pt", help="Typst table inset (default: 10pt).")
    p.add_argument("--row-gutter", default="auto", help="Typst row-gutter (default: auto).")
    p.add_argument(
        "--columns",
        default=None,
        help='Typst columns spec, e.g. "(auto, auto, auto)" or "(1fr, 2fr)". Default is auto per column.',
    )
    p.add_argument("--title", default=None, help="Optional figure caption title.")
    args = p.parse_args(argv)

    if args.path:
        text = Path(args.path).read_text(encoding="utf-8")
        lines = text.splitlines(True)
    else:
        lines = sys.stdin.read().splitlines(True)

    t = parse_md_table(lines)
    out = render_typst_table(
        t,
        wrap_figure=not args.no_figure,
        align=args.align,
        inset=args.inset,
        row_gutter=args.row_gutter,
        col_spec=args.columns,
        title=args.title,
    )
    sys.stdout.write(out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

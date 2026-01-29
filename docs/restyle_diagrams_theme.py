#!/usr/bin/env python3
"""
Restyle existing Graphviz .dot diagrams under docs/diagrams to match the
final-architecture color theme.

Targets the auto-generated "single node label" diagrams created by
typst_diagramify.py (digraph Diagram { ... n0 [label="..."]; }).

It preserves the label content, but rewrites graph/node/edge styling and
regenerates the corresponding .png via `dot`.
"""

from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path
from typing import List


LABEL_RE = re.compile(r'n0\s*\[\s*label\s*=\s*"(?P<label>(?:\\.|[^"\\])*)"\s*\]\s*;?', re.S)


THEME_DOT_TEMPLATE = """digraph Diagram {{
  graph [
    rankdir=TB,
    bgcolor="transparent",
    pad="0.25",
    nodesep="0.50",
    ranksep="0.60"
  ];
  edge [color="#2f8f8a", penwidth=1.2, arrowsize=0.8];
  node [
    shape=box,
    style="rounded,filled",
    fillcolor="#bfe6e3",
    color="#2f8f8a",
    fontcolor="#0f172a",
    fontname="Noto Sans Mono CJK SC",
    fontsize=12,
    margin="0.22,0.16"
  ];
  n0 [label="{label}"];
}}
"""


def is_candidate(dot_text: str) -> bool:
    if "<TABLE" in dot_text or "label=<" in dot_text:
        return False
    return bool(LABEL_RE.search(dot_text))


def restyle_dot(dot_path: Path) -> bool:
    text = dot_path.read_text(encoding="utf-8")
    if not is_candidate(text):
        return False

    m = LABEL_RE.search(text)
    if not m:
        return False

    label = m.group("label")
    new_text = THEME_DOT_TEMPLATE.format(label=label)
    if new_text == text:
        return False

    dot_path.write_text(new_text, encoding="utf-8")
    png_path = dot_path.with_suffix(".png")
    subprocess.check_call(["dot", "-Tpng", str(dot_path), "-o", str(png_path)])
    return True


def main(argv: List[str]) -> int:
    p = argparse.ArgumentParser(description="Restyle docs/diagrams/*.dot with a unified theme.")
    p.add_argument("--dir", default="docs/diagrams", help="Diagram directory (default: docs/diagrams).")
    args = p.parse_args(argv)

    d = Path(args.dir)
    changed = 0
    total = 0
    for dot_path in sorted(d.glob("*.dot")):
        total += 1
        if restyle_dot(dot_path):
            changed += 1
    print(f"restyled {changed}/{total} dot files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(__import__('sys').argv[1:]))


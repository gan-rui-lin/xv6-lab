#!/usr/bin/env python3
"""
Upgrade existing docs/diagrams/*.dot that are single-node ASCII-label diagrams
into multi-node block layouts when recognizable.

This rewrites the .dot in-place and regenerates the matching .png.
"""

from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path
from typing import List, Optional

from typst_diagramify import dot_for_multinode


LABEL_RE = re.compile(r'n0\s*\[\s*label\s*=\s*"(?P<label>(?:\\.|[^"\\])*)"\s*\]\s*;?', re.S)


def _unescape_label(label: str) -> str:
    # reverse of escape_dot_label: \l -> \n, unescape \" and \\ minimally
    label = label.replace("\\l", "\n")
    label = label.replace("\\\"", "\"")
    label = label.replace("\\\\", "\\")
    return label


def extract_ascii_block(dot_text: str) -> Optional[str]:
    if "label=<" in dot_text or "<TABLE" in dot_text:
        return None
    m = LABEL_RE.search(dot_text)
    if not m:
        return None
    return _unescape_label(m.group("label"))


def upgrade_one(dot_path: Path) -> bool:
    text = dot_path.read_text(encoding="utf-8")
    block = extract_ascii_block(text)
    if not block:
        return False

    dot = dot_for_multinode(block)
    if dot is None:
        return False

    dot_path.write_text(dot, encoding="utf-8")
    png_path = dot_path.with_suffix(".png")
    subprocess.check_call(["dot", "-Tpng", str(dot_path), "-o", str(png_path)])
    return True


def main(argv: List[str]) -> int:
    p = argparse.ArgumentParser(description="Upgrade ASCII-label diagrams to multi-node layouts.")
    p.add_argument("--dir", default="docs/diagrams", help="Diagram directory (default: docs/diagrams).")
    args = p.parse_args(argv)
    d = Path(args.dir)

    changed = 0
    total = 0
    for dot_path in sorted(d.glob("*.dot")):
        total += 1
        if upgrade_one(dot_path):
            changed += 1
    print(f"upgraded {changed}/{total} dot files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(__import__("sys").argv[1:]))


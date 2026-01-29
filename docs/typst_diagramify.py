#!/usr/bin/env python3
"""
Find ASCII/Unicode pseudo-flow diagrams inside fenced code blocks (``` ... ```)
in Typst docs and convert them to Graphviz .dot + .png, then replace the code
block with `#figure(image("..."))`.

This is intentionally heuristic-based: it targets blocks containing box-drawing
characters or many arrow symbols, or the CPU scheduler flow diagram pattern.
"""

from __future__ import annotations

import argparse
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Tuple


BOX_CHARS = set("┌┐└┘─│┬┴┼")
ARROW_CHARS = set("↓→←↑")


def is_fence(line: str) -> bool:
    return line.lstrip().startswith("```")


def block_is_diagram(block: str) -> bool:
    if any(ch in block for ch in BOX_CHARS):
        return True
    arrow_count = sum(1 for ch in block if ch in ARROW_CHARS)
    if "CPU 0" in block and "CPU 1" in block and "CPU 2" in block and "scheduler()" in block:
        return True
    return arrow_count >= 6


def escape_dot_label(text: str) -> str:
    # Preserve alignment: keep spaces, use left-justified line breaks.
    text = text.rstrip("\n")
    text = text.replace("\\", "\\\\").replace('"', '\\"')
    # Graphviz supports \l for left-justified line breaks; ensure final \l.
    lines = text.splitlines()
    return "\\l".join(lines) + "\\l"

def _dot_prelude(fontname: str = "Noto Sans CJK SC") -> str:
    return (
        "digraph Diagram {\n"
        "  graph [\n"
        "    rankdir=TB,\n"
        "    bgcolor=\"transparent\",\n"
        "    pad=\"0.25\",\n"
        "    nodesep=\"0.50\",\n"
        "    ranksep=\"0.60\"\n"
        "  ];\n"
        "  edge [color=\"#2f8f8a\", penwidth=1.2, arrowsize=0.8];\n"
        "  node [\n"
        "    shape=box,\n"
        "    style=\"rounded,filled\",\n"
        "    fillcolor=\"#bfe6e3\",\n"
        "    color=\"#2f8f8a\",\n"
        "    fontcolor=\"#0f172a\",\n"
        f"    fontname=\"{fontname}\",\n"
        "    fontsize=12,\n"
        "    margin=\"0.22,0.16\"\n"
        "  ];\n"
    )


def _dot_end() -> str:
    return "}\n"


def _is_boxed_pipeline(lines: List[str]) -> bool:
    return any("┌" in ln and "┐" in ln for ln in lines) and any("└" in ln and "┘" in ln for ln in lines)


def _extract_boxes(lines: List[str]) -> List[List[str]]:
    """
    Extract boxed blocks drawn with Unicode box-drawing characters.
    Returns list of boxes, each box as list of content lines (trimmed).
    """
    boxes: List[List[str]] = []
    i = 0
    while i < len(lines):
        ln = lines[i]
        if "┌" in ln and "┐" in ln:
            left = ln.find("┌")
            right = ln.rfind("┐")
            if left < 0 or right < 0 or right <= left:
                i += 1
                continue
            j = i + 1
            content: List[str] = []
            while j < len(lines):
                ln2 = lines[j]
                # stop at first bottom border-ish line
                if "└" in ln2 and "┘" in ln2:
                    break
                if "│" in ln2:
                    inner = ln2[left : right + 1].strip()
                    if inner.startswith("│"):
                        inner = inner[1:]
                    if inner.endswith("│"):
                        inner = inner[:-1]
                    inner = inner.strip()
                    if inner:
                        content.append(inner)
                j += 1
            if content:
                boxes.append(content)
            if j < len(lines):
                i = j + 1
                continue
        i += 1
    return boxes


def dot_for_multinode(block_text: str) -> Optional[str]:
    """
    Try to convert common pseudo-diagrams into multi-node dot.
    Return dot string if recognized; otherwise None.
    """
    raw_lines = [ln.rstrip("\n") for ln in block_text.splitlines()]
    lines = [ln for ln in raw_lines if ln.strip() != ""]

    # Multi-CPU scheduler flow (3 columns)
    if "CPU 0" in block_text and "CPU 1" in block_text and "CPU 2" in block_text and "scheduler()" in block_text:
        steps = [
            "scheduler()",
            "遍历进程表",
            "获取 proc 锁",
            "选择最高优先级\\nRUNNABLE 进程",
        ]
        dot = _dot_prelude()
        for cpu in range(3):
            for si, s in enumerate(steps):
                dot += f"  cpu{cpu}_s{si} [label=\"CPU {cpu}\\n{s}\"];\n"
        for si in range(len(steps)):
            dot += "  { rank=same; " + " ".join([f"cpu{c}_s{si}" for c in range(3)]) + " }\n"
        for cpu in range(3):
            for si in range(len(steps) - 1):
                dot += f"  cpu{cpu}_s{si} -> cpu{cpu}_s{si+1};\n"
        dot += _dot_end()
        return dot

    # Memory request split (specific pattern)
    if (
        "内存请求" in block_text
        and "小对象" in block_text
        and "大对象" in block_text
        and "Slab" in block_text
        and "Buddy" in block_text
        and "物理内存池" in block_text
    ):
        dot = _dot_prelude()
        dot += "  root [label=\"内存请求\"];\n"
        dot += "  small [label=\"小对象 (≤ 2KB)\"];\n"
        dot += "  large [label=\"大对象 (> 2KB)\"];\n"
        dot += "  slab [label=\"Slab 分配器\\n(对象级分配)\"];\n"
        dot += "  buddy [label=\"Buddy 分配器\\n(页级分配)\"];\n"
        dot += "  pool [label=\"物理内存池\"];\n"
        dot += "  root -> small;\n  root -> large;\n"
        dot += "  small -> slab;\n  large -> buddy;\n"
        dot += "  slab -> pool;\n  buddy -> pool;\n"
        dot += "  { rank=same; small; large; }\n"
        dot += "  { rank=same; slab; buddy; }\n"
        dot += _dot_end()
        return dot

    # Boxed vertical pipelines
    if _is_boxed_pipeline(raw_lines):
        boxes = _extract_boxes(raw_lines)
        if len(boxes) >= 2:
            dot = _dot_prelude(fontname="Noto Sans Mono CJK SC")
            node_ids: List[str] = []
            for idx, content in enumerate(boxes, start=1):
                nid = f"b{idx}"
                node_ids.append(nid)
                label = escape_dot_label("\n".join(content))
                dot += f"  {nid} [label=\"{label}\"];\n"
            for a, b in zip(node_ids, node_ids[1:]):
                dot += f"  {a} -> {b};\n"
            dot += _dot_end()
            return dot

    # Arrow-separated vertical steps (split into blocks between standalone arrow lines)
    # This avoids relying on monospace alignment for arrows.
    if "↓" in block_text:
        def is_arrow_line(s: str) -> bool:
            t = s.strip()
            if not t:
                return True
            # pure arrows / pipes / box joints
            return all(ch in ARROW_CHARS or ch in {"|", " ", "┌", "┐", "└", "┘", "─", "│", "┬", "┴", "┼"} for ch in t)

        segments: List[List[str]] = []
        cur: List[str] = []
        for ln in raw_lines:
            if is_arrow_line(ln):
                if cur:
                    segments.append(cur)
                    cur = []
                continue
            if ln.strip() == "↓":
                if cur:
                    segments.append(cur)
                    cur = []
                continue
            cur.append(ln.strip())
        if cur:
            segments.append(cur)

        # Reasonable segment count for a diagram
        if 2 <= len(segments) <= 10:
            dot = _dot_prelude()
            ids: List[str] = []
            for idx, seg in enumerate(segments, start=1):
                nid = f"step{idx}"
                ids.append(nid)
                dot += f"  {nid} [label=\"{escape_dot_label('\\n'.join(seg))}\"];\n"
            for a, b in zip(ids, ids[1:]):
                dot += f"  {a} -> {b};\n"
            dot += _dot_end()
            return dot

    return None


def dot_for_ascii_diagram(label: str) -> str:
    return (
        "digraph Diagram {\n"
        "  graph [\n"
        "    rankdir=TB,\n"
        "    bgcolor=\"transparent\",\n"
        "    pad=\"0.25\",\n"
        "    nodesep=\"0.50\",\n"
        "    ranksep=\"0.60\"\n"
        "  ];\n"
        "  edge [color=\"#2f8f8a\", penwidth=1.2, arrowsize=0.8];\n"
        "  node [\n"
        "    shape=box,\n"
        "    style=\"rounded,filled\",\n"
        "    fillcolor=\"#bfe6e3\",\n"
        "    color=\"#2f8f8a\",\n"
        "    fontcolor=\"#0f172a\",\n"
        "    fontname=\"Noto Sans Mono CJK SC\",\n"
        "    fontsize=12,\n"
        "    margin=\"0.22,0.16\"\n"
        "  ];\n"
        f"  n0 [label=\"{label}\"];\n"
        "}\n"
    )


def prefix_for_file(path: Path) -> str:
    # Keep file names ASCII to avoid toolchain/font issues.
    mapping = {
        "进程": "process",
        "内存": "memory",
        "文件": "file",
    }
    return mapping.get(path.stem, re.sub(r"[^a-zA-Z0-9]+", "-", path.stem).strip("-") or "doc")


@dataclass
class Replacement:
    start_line_idx: int
    end_line_idx: int  # exclusive
    figure_text: str


def process_file(path: Path, out_dir: Path) -> Tuple[bool, List[Path]]:
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines(True)

    prefix = prefix_for_file(path)
    out_dir.mkdir(parents=True, exist_ok=True)

    replacements: List[Replacement] = []
    generated: List[Path] = []

    in_block = False
    block_start: Optional[int] = None
    block_lines: List[str] = []
    fence_indent: str = ""

    diagram_index = 1

    for i, line in enumerate(lines):
        if is_fence(line):
            if not in_block:
                in_block = True
                block_start = i
                block_lines = []
                fence_indent = line[: len(line) - len(line.lstrip())]
            else:
                # Closing fence.
                assert block_start is not None
                block_text = "".join(block_lines)
                if block_is_diagram(block_text):
                    base = f"{prefix}-diagram-{diagram_index:02d}"
                    diagram_index += 1

                    dot_path = out_dir / f"{base}.dot"
                    png_path = out_dir / f"{base}.png"
                    dot = dot_for_multinode(block_text)
                    if dot is None:
                        label = escape_dot_label(block_text)
                        dot = dot_for_ascii_diagram(label)
                    dot_path.write_text(dot, encoding="utf-8")
                    subprocess.check_call(
                        ["dot", "-Tpng", str(dot_path), "-o", str(png_path)],
                    )
                    generated.extend([dot_path, png_path])

                    rel_png = png_path.relative_to(path.parent)
                    fig = (
                        f"{fence_indent}#figure(\n"
                        f"{fence_indent}  image(\"{rel_png.as_posix()}\"),\n"
                        f"{fence_indent})\n"
                    )
                    replacements.append(
                        Replacement(start_line_idx=block_start, end_line_idx=i + 1, figure_text=fig)
                    )

                in_block = False
                block_start = None
                block_lines = []
                fence_indent = ""
            continue

        if in_block:
            block_lines.append(line)

    if not replacements:
        return False, []

    # Apply replacements from bottom to top to keep indices stable.
    new_lines = lines[:]
    for rep in reversed(replacements):
        new_lines[rep.start_line_idx : rep.end_line_idx] = [rep.figure_text]

    path.write_text("".join(new_lines), encoding="utf-8")
    return True, generated


def main(argv: List[str]) -> int:
    p = argparse.ArgumentParser(description="Convert ASCII flow diagrams in Typst fenced blocks to dot/png figures.")
    p.add_argument("paths", nargs="+", help="Typst files to process.")
    p.add_argument("--out-dir", default="docs/diagrams", help="Output directory for dot/png (default: docs/diagrams).")
    args = p.parse_args(argv)

    out_dir = Path(args.out_dir)
    any_changed = False
    for pth in args.paths:
        path = Path(pth)
        changed, generated = process_file(path, out_dir)
        if changed:
            any_changed = True
            print(f"updated: {path} ({len(generated)//2} diagrams)")
        else:
            print(f"no change: {path}")
    return 0 if any_changed else 0


if __name__ == "__main__":
    raise SystemExit(main(__import__("sys").argv[1:]))

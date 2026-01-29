#!/usr/bin/env bash
set -euo pipefail

# Batch-convert Markdown headings (# -> =) to Typst-style for selected files.
# Outputs to *-new.md in the same directory.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOCS_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
CONVERTER="${SCRIPT_DIR}/convert_md_headings.py"

if [[ ! -f "${CONVERTER}" ]]; then
  echo "Converter not found: ${CONVERTER}" >&2
  exit 1
fi

files=(
  "进程.md"
  "内存.md"
  "文件.md"
)

cd "${DOCS_DIR}" >/dev/null

for f in "${files[@]}"; do
  if [[ ! -f "${f}" ]]; then
    echo "Skip: not found -> ${DOCS_DIR}/${f}" >&2
    continue
  fi
  base="${f%.md}"
  out="${base}-new.md"
  echo "Converting ${f} -> ${out}"
  python3 "${CONVERTER}" "${f}" -o "${out}"

done

echo "Done."
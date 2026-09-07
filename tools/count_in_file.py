#!/usr/bin/env python3
"""Fail unless a file contains EXACTLY N occurrences of a literal.

Replaces `tests/file_count.cmake`, which silently checked almost nothing on any
binary file. Measured 2026-09-07: CMake's `file(READ)` does load the whole
file, but `string(REGEX MATCHALL)` stops at the first NUL byte -- and an FBX has
one at byte 20, inside `Kaydara FBX Binary  \\0`. So the gate saw twenty bytes,
found zero matches, and passed. It reported a file containing four copies of
`.../data/skins/../textures/skin/african_deep.png` as clean.

`file_contains.cmake` is not affected: `string(FIND)` scans the whole buffer.
That is why one gate caught the mutation and its sibling did not.

Counted here in Python over BYTES, which has no such trap, and where a count of
zero is a legitimate assertion -- "this string must not appear" is how the
absence of an unnormalised path is checked.

Usage:
    count_in_file.py --file <path> --needle <text> --count <n>
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--file", required=True, type=Path)
    ap.add_argument("--needle", required=True,
                    help="a LITERAL, not a regular expression")
    ap.add_argument("--count", required=True, type=int)
    args = ap.parse_args()

    if not args.file.exists():
        print(f"no such file: {args.file}", file=sys.stderr)
        return 1

    # Bytes, not text: these files are FBX, GLB and USDZ as often as they are
    # .usda, and decoding one as UTF-8 would raise before it could be counted.
    data = args.file.read_bytes()
    found = data.count(args.needle.encode())
    if found != args.count:
        print(f"{args.file}: {args.needle!r} occurs {found} times, "
              f"expected {args.count}", file=sys.stderr)
        return 1

    print(f"{args.file.name}: {args.needle!r} occurs {found} times")
    return 0


if __name__ == "__main__":
    sys.exit(main())
